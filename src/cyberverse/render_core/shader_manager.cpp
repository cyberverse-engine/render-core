/**
 * @file shader_manager.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader manager implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/shader_manager.hpp"

#include "cyberverse/render_core/detail/shader_compiler.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cyberverse {
namespace render_core {

shader_manager::shader_manager(Diligent::IRenderDevice* device)
    : owned_compiler_{ std::make_unique<detail::diligent_shader_compiler>(device) }
    , compiler_{ owned_compiler_.get() } {}

shader_manager::shader_manager(detail::shader_compiler& compiler) noexcept : compiler_{ &compiler } {}

shader_manager::~shader_manager() { clear(); }

std::size_t shader_manager::hash_source(std::string_view source) noexcept {
  std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
  for (unsigned char value : source) {
    hash ^= static_cast<std::size_t>(value);
    hash *= static_cast<std::size_t>(1099511628211ull);
  }
  return hash;
}

std::size_t shader_manager::hash_bytecode(std::span<const std::byte> bytecode) noexcept {
  std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
  for (const std::byte value : bytecode) {
    hash ^= static_cast<std::size_t>(static_cast<unsigned char>(value));
    hash *= static_cast<std::size_t>(1099511628211ull);
  }
  return hash;
}

bool shader_manager::read_file(const std::filesystem::path& path,
                               std::string& source,
                               std::filesystem::file_time_type& mtime) noexcept {
  try {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
      return false;
    }

    std::error_code error;
    const std::filesystem::file_time_type write_time = std::filesystem::last_write_time(path, error);
    mtime = error ? std::filesystem::file_time_type{} : write_time;

    source = std::move(content);
    return true;
  } catch (...) {
    return false;
  }
}

Diligent::IShader* shader_manager::compile_locked(const shader_key& key, entry& target, std::string source) {
  ++compiles_;
  const std::size_t source_hash = hash_source(source);

  std::string compile_log;
  Diligent::IShader* shader = nullptr;
  try {
    shader = compiler_->compile(key, source, target.source_path, compile_log);
  } catch (...) {
    shader = nullptr;
    compile_log = "shader compiler threw an exception";
  }

  if (shader == nullptr) {
    ++compile_failures_;
    target.compile_log = std::move(compile_log);
    return nullptr;
  }

  if (target.shader != nullptr) {
    compiler_->destroy(target.shader);
  }

  target.shader = shader;
  target.source = std::move(source);
  target.source_hash = source_hash;
  target.compile_log = std::move(compile_log);
  target.bytecode.clear();
  target.bytecode_backed = false;

  if (!target.source_path.empty()) {
    std::error_code error;
    const std::filesystem::file_time_type write_time = std::filesystem::last_write_time(target.source_path, error);
    if (!error) {
      target.mtime = write_time;
    }
  }

  return shader;
}

Diligent::IShader*
shader_manager::compile_bytecode_locked(const shader_key& key, entry& target, std::span<const std::byte> bytecode) {
  ++compiles_;
  ++bytecode_compiles_;
  std::string compile_log;
  Diligent::IShader* shader = nullptr;
  try {
    shader = compiler_->compile_bytecode(key, bytecode, compile_log);
  } catch (...) {
    shader = nullptr;
    compile_log = "shader bytecode compiler threw an exception";
  }
  if (shader == nullptr) {
    ++compile_failures_;
    target.compile_log = std::move(compile_log);
    target.bytecode.assign(bytecode.begin(), bytecode.end());
    target.bytecode_backed = true;
    return nullptr;
  }
  if (target.shader != nullptr) {
    compiler_->destroy(target.shader);
  }
  target.shader = shader;
  target.source.clear();
  target.source_hash = hash_bytecode(bytecode);
  target.bytecode.assign(bytecode.begin(), bytecode.end());
  target.bytecode_backed = true;
  target.compile_log = std::move(compile_log);
  target.source_path.clear();
  return shader;
}

bool shader_manager::expand_includes(std::string_view source,
                                     const std::filesystem::path& source_path,
                                     std::string& expanded,
                                     std::string& log,
                                     std::vector<std::filesystem::path>& stack,
                                     std::size_t depth) const {
  if (depth > 32) {
    log = "shader include depth exceeded";
    return false;
  }

  std::string_view remaining = source;
  while (!remaining.empty()) {
    const std::size_t newline = remaining.find('\n');
    const std::string_view line = remaining.substr(0, newline);
    std::string_view next = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);

    std::size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t' || line[first] == '\r')) {
      ++first;
    }
    const std::string_view trimmed = line.substr(first);
    if (trimmed.size() >= 8 && trimmed.substr(0, 8) == "#include") {
      std::size_t path_start = 8;
      while (path_start < trimmed.size() && (trimmed[path_start] == ' ' || trimmed[path_start] == '\t')) {
        ++path_start;
      }
      if (path_start >= trimmed.size() || (trimmed[path_start] != '"' && trimmed[path_start] != '<')) {
        log = "malformed #include directive";
        return false;
      }
      const char closing = trimmed[path_start] == '"' ? '"' : '>';
      const std::size_t path_end = trimmed.find(closing, path_start + 1);
      if (path_end == std::string_view::npos) {
        log = "unterminated #include path";
        return false;
      }
      const std::string include_name{ trimmed.substr(path_start + 1, path_end - path_start - 1) };

      std::filesystem::path resolved;
      if (!source_path.empty()) {
        const std::filesystem::path candidate = source_path.parent_path() / include_name;
        std::error_code exists_error;
        if (std::filesystem::exists(candidate, exists_error)) {
          resolved = candidate;
        }
      }
      if (resolved.empty()) {
        for (const std::filesystem::path& directory : include_paths_) {
          const std::filesystem::path candidate = directory / include_name;
          std::error_code exists_error;
          if (std::filesystem::exists(candidate, exists_error)) {
            resolved = candidate;
            break;
          }
        }
      }
      if (resolved.empty()) {
        log = "shader include not found: " + include_name;
        return false;
      }

      std::error_code canonical_error;
      const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, canonical_error);
      const std::filesystem::path key = canonical_error ? resolved.lexically_normal() : canonical;
      if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
        log = "recursive shader include: " + key.string();
        return false;
      }

      std::string included_source;
      std::filesystem::file_time_type ignored_mtime{};
      if (!read_file(resolved, included_source, ignored_mtime)) {
        log = "failed to read shader include: " + resolved.string();
        return false;
      }
      ++include_files_read_;
      stack.push_back(key);
      if (!expand_includes(included_source, resolved, expanded, log, stack, depth + 1)) {
        return false;
      }
      stack.pop_back();
      expanded.push_back('\n');
    } else {
      expanded.append(line);
      expanded.push_back('\n');
    }

    remaining = next;
  }
  return true;
}

void shader_manager::destroy_entry(entry& value) noexcept {
  if (value.shader != nullptr) {
    compiler_->destroy(value.shader);
    value.shader = nullptr;
  }
}

Diligent::IShader* shader_manager::get_or_create_from_file(const shader_key& key, const std::filesystem::path& path) {
  const shader_key canonical = normalize_shader_key(key);

  std::string source;
  std::filesystem::file_time_type mtime{};

  std::lock_guard<std::mutex> lock{ mutex_ };

  if (!read_file(path, source, mtime)) {
    const auto existing = cache_.find(canonical);
    return existing != cache_.end() ? existing->second.shader : nullptr;
  }
  ++source_reads_;

  std::string expanded;
  std::string include_log;
  std::vector<std::filesystem::path> stack{ path.lexically_normal() };
  if (!expand_includes(source, path, expanded, include_log, stack, 0)) {
    const auto existing = cache_.find(canonical);
    entry& target = existing != cache_.end() ? existing->second : cache_[canonical];
    target.compile_log = std::move(include_log);
    target.source_path = path;
    return target.shader;
  }
  source = std::move(expanded);

  const std::size_t source_hash = hash_source(source);
  auto existing = cache_.find(canonical);
  if (existing != cache_.end() && existing->second.shader != nullptr && existing->second.source_hash == source_hash &&
      !existing->second.bytecode_backed) {
    existing->second.source_path = path;
    existing->second.source_name = canonical.name;
    existing->second.mtime = mtime;
    ++cache_hits_;
    return existing->second.shader;
  }

  entry& target = cache_[canonical];
  target.source_path = path;
  target.source_name = canonical.name;
  target.mtime = mtime;

  Diligent::IShader* shader = compile_locked(canonical, target, std::move(source));
  return shader != nullptr ? shader : target.shader;
}

Diligent::IShader*
shader_manager::get_or_create_from_source(const shader_key& key, std::string source, std::string source_name) {
  const shader_key canonical = normalize_shader_key(key);
  std::string expanded;
  std::string include_log;
  std::vector<std::filesystem::path> stack;

  std::lock_guard<std::mutex> lock{ mutex_ };
  if (!expand_includes(source, {}, expanded, include_log, stack, 0)) {
    auto existing = cache_.find(canonical);
    if (existing != cache_.end()) {
      existing->second.compile_log = std::move(include_log);
      return existing->second.shader;
    }
    entry& target = cache_[canonical];
    target.compile_log = std::move(include_log);
    return nullptr;
  }
  source = std::move(expanded);
  const std::size_t expanded_hash = hash_source(source);

  auto existing = cache_.find(canonical);
  if (existing != cache_.end() && existing->second.shader != nullptr && existing->second.source_hash == expanded_hash &&
      !existing->second.bytecode_backed) {
    if (!source_name.empty()) {
      existing->second.source_name = std::move(source_name);
    }
    ++cache_hits_;
    return existing->second.shader;
  }

  entry& target = cache_[canonical];
  target.source_path.clear();
  target.source_name = source_name.empty() ? canonical.name : std::move(source_name);

  Diligent::IShader* shader = compile_locked(canonical, target, std::move(source));
  return shader != nullptr ? shader : target.shader;
}

Diligent::IShader* shader_manager::get_or_create_from_bytecode(const shader_key& key,
                                                               std::span<const std::byte> bytecode,
                                                               std::string source_name) {
  const shader_key canonical = normalize_shader_key(key);
  const std::size_t bytecode_hash = hash_bytecode(bytecode);

  std::lock_guard<std::mutex> lock{ mutex_ };

  auto existing = cache_.find(canonical);
  if (existing != cache_.end() && existing->second.shader != nullptr && existing->second.bytecode_backed &&
      existing->second.source_hash == bytecode_hash) {
    if (!source_name.empty()) {
      existing->second.source_name = std::move(source_name);
    }
    ++cache_hits_;
    return existing->second.shader;
  }

  entry& target = cache_[canonical];
  target.source_path.clear();
  target.source_name = source_name.empty() ? canonical.name : std::move(source_name);
  Diligent::IShader* shader = compile_bytecode_locked(canonical, target, bytecode);
  return shader != nullptr ? shader : target.shader;
}

void shader_manager::add_include_path(std::filesystem::path path) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock{ mutex_ };
  include_paths_.push_back(std::move(path));
}

void shader_manager::clear_include_paths() {
  std::lock_guard<std::mutex> lock{ mutex_ };
  include_paths_.clear();
}

bool shader_manager::reload(const shader_key& key, bool force) {
  const shader_key canonical = normalize_shader_key(key);

  std::string source;
  std::filesystem::file_time_type mtime{};

  std::lock_guard<std::mutex> lock{ mutex_ };

  const auto existing = cache_.find(canonical);
  if (existing == cache_.end() || existing->second.source_path.empty()) {
    return false;
  }

  if (!read_file(existing->second.source_path, source, mtime)) {
    return false;
  }
  ++source_reads_;

  std::string expanded;
  std::string include_log;
  std::vector<std::filesystem::path> stack{ existing->second.source_path.lexically_normal() };
  if (!expand_includes(source, existing->second.source_path, expanded, include_log, stack, 0)) {
    existing->second.compile_log = std::move(include_log);
    return false;
  }
  source = std::move(expanded);
  const std::size_t source_hash = hash_source(source);
  if (!force && source_hash == existing->second.source_hash) {
    existing->second.mtime = mtime;
    return false;
  }

  existing->second.source_name = canonical.name;
  Diligent::IShader* shader = compile_locked(canonical, existing->second, std::move(source));
  if (shader == nullptr) {
    return false;
  }

  ++reloads_;
  return true;
}

bool shader_manager::is_source_changed(const shader_key& key) const {
  const shader_key canonical = normalize_shader_key(key);

  std::string source;
  std::filesystem::file_time_type mtime{};

  std::lock_guard<std::mutex> lock{ mutex_ };

  const auto existing = cache_.find(canonical);
  if (existing == cache_.end() || existing->second.source_path.empty()) {
    return false;
  }

  if (!read_file(existing->second.source_path, source, mtime)) {
    return true;
  }
  ++source_reads_;

  std::string expanded;
  std::string include_log;
  std::vector<std::filesystem::path> stack{ existing->second.source_path.lexically_normal() };
  if (!expand_includes(source, existing->second.source_path, expanded, include_log, stack, 0)) {
    return true;
  }
  return hash_source(expanded) != existing->second.source_hash;
}

Diligent::IShader* shader_manager::get(const shader_key& key) const {
  const shader_key canonical = normalize_shader_key(key);

  std::lock_guard<std::mutex> lock{ mutex_ };
  const auto existing = cache_.find(canonical);
  return existing != cache_.end() ? existing->second.shader : nullptr;
}

bool shader_manager::contains(const shader_key& key) const { return get(key) != nullptr; }

std::string shader_manager::get_compile_log(const shader_key& key) const {
  const shader_key canonical = normalize_shader_key(key);

  std::lock_guard<std::mutex> lock{ mutex_ };
  const auto existing = cache_.find(canonical);
  return existing != cache_.end() ? existing->second.compile_log : std::string{};
}

bool shader_manager::remove(const shader_key& key) {
  const shader_key canonical = normalize_shader_key(key);

  std::lock_guard<std::mutex> lock{ mutex_ };
  const auto existing = cache_.find(canonical);
  if (existing == cache_.end()) {
    return false;
  }

  destroy_entry(existing->second);
  cache_.erase(existing);
  return true;
}

void shader_manager::clear() {
  std::lock_guard<std::mutex> lock{ mutex_ };
  for (auto& [key, value] : cache_) {
    (void)key;
    destroy_entry(value);
  }
  cache_.clear();
}

std::size_t shader_manager::size() const {
  std::lock_guard<std::mutex> lock{ mutex_ };
  return cache_.size();
}

shader_manager_stats shader_manager::get_stats() const {
  std::lock_guard<std::mutex> lock{ mutex_ };

  shader_manager_stats stats;
  stats.shader_count = cache_.size();
  stats.source_reads = source_reads_;
  stats.compiles = compiles_;
  stats.compile_failures = compile_failures_;
  stats.cache_hits = cache_hits_;
  stats.reloads = reloads_;
  stats.include_files_read = include_files_read_;
  stats.bytecode_compiles = bytecode_compiles_;
  return stats;
}

} // namespace render_core
} // namespace cyberverse
