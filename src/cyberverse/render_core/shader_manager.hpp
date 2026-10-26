/**
 * @file shader_manager.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader manager
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <RenderDevice.h>

#include "cyberverse/render_core/shader_key.hpp"

namespace cyberverse {
namespace render_core {

namespace detail {
class shader_compiler;
} // namespace detail

/**
 * @brief shader_manager statistics snapshot
 *
 */
struct shader_manager_stats {
  std::size_t shader_count = 0;
  std::uint64_t source_reads = 0;
  std::uint64_t compiles = 0;
  std::uint64_t compile_failures = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t reloads = 0;
  std::uint64_t include_files_read = 0;
  std::uint64_t bytecode_compiles = 0;
};

/**
 * @brief Thread-safe shader cache and hot-reload manager
 *
 * Keys identify shader permutations; values own the compiled IShader. File
 * sources are read, hashed, and compiled on demand. A failed recompile keeps
 * the last good shader, so a broken edit does not take the renderer down.
 *
 * The manager is thread-safe: every public method locks an internal mutex. The
 * compiler runs under that lock, so compilation is serialized.
 *
 * Include files are not resolved by v0.1. The source is compiled from memory;
 * shaders that need #include directives should be preprocessed or a future
 * stream-factory-backed compile path should be used.
 *
 */
class shader_manager {
public:
  /**
   * @brief Construct a manager with a Diligent compiler
   *
   * @param device Render device. Must not be null.
   * @throws std::invalid_argument If device is null
   */
  explicit shader_manager(Diligent::IRenderDevice* device);

  /**
   * @brief Construct a manager over a caller-owned compiler
   *
   * @param compiler Compiler. Must outlive the manager.
   */
  explicit shader_manager(detail::shader_compiler& compiler) noexcept;

  ~shader_manager();

  shader_manager(const shader_manager&) = delete;
  shader_manager(shader_manager&&) = delete;

  shader_manager& operator=(const shader_manager&) = delete;
  shader_manager& operator=(shader_manager&&) = delete;

  /**
   * @brief Get or create a shader from a file
   *
   * Reads the file and compiles it if the key is missing or the content hash
   * changed. If compilation fails, the last good shader is returned when one
   * exists; otherwise null is returned.
   *
   * @param key Shader key
   * The manager owns the returned shader. Do not release it. The pointer stays
   * valid until the same key is reloaded/removed/cleared or the manager is
   * destroyed.
   *
   * @param path Source file path
   * @return Diligent::IShader* Cached or newly compiled shader
   */
  [[nodiscard]] Diligent::IShader* get_or_create_from_file(const shader_key& key, const std::filesystem::path& path);

  /**
   * @brief Get or create a shader from in-memory source
   *
   * @param key Shader key
   * The manager owns the returned shader. Do not release it.
   *
   * @param source Source text
   * @param source_name Debug name for diagnostics; defaults to key.name
   * @return Diligent::IShader*
   */
  [[nodiscard]] Diligent::IShader*
  get_or_create_from_source(const shader_key& key, std::string source, std::string source_name = {});

  /**
   * @brief Get or create a shader from precompiled bytecode
   *
   * Bytecode-backed entries are never reloaded from a source file. The supplied
   * bytecode is retained for cache identity and diagnostics.
   *
   * @param key Shader key
   * @param bytecode Backend shader bytecode
   * @param source_name Debug name; defaults to key.name
   * @return Diligent::IShader*
   */
  [[nodiscard]] Diligent::IShader*
  get_or_create_from_bytecode(const shader_key& key, std::span<const std::byte> bytecode, std::string source_name = {});

  /**
   * @brief Add a directory searched by #include directives
   *
   * Relative includes are first resolved against the including file's directory,
   * then against these directories in registration order.
   *
   * @param path Include directory
   */
  void add_include_path(std::filesystem::path path);

  /**
   * @brief Remove all include directories
   *
   */
  void clear_include_paths();

  /**
   * @brief Re-read and recompile a file-backed shader
   *
   * @param key Shader key
   * @param force Recompile even when the source hash is unchanged
   * @return true if a new shader was installed
   * @return false if the key/file is missing, compilation failed, or nothing changed
   */
  bool reload(const shader_key& key, bool force);

  /**
   * @brief Check whether a file-backed shader's source changed
   *
   * Reads and hashes the file but does not compile. Returns true when the file
   * is missing, since the cached shader no longer matches its source.
   *
   * @param key Shader key
   * @return true if the source changed
   * @return false if the key is unknown, not file-backed, or unchanged
   */
  [[nodiscard]] bool is_source_changed(const shader_key& key) const;

  /**
   * @brief Get a cached shader
   *
   * The pointer is invalidated by remove(), clear(), or a successful reload of
   * the same key.
   *
   * @param key Shader key
   * @return Diligent::IShader* Null when the key is absent or compilation failed
   */
  [[nodiscard]] Diligent::IShader* get(const shader_key& key) const;

  /**
   * @brief Whether a key has a compiled shader
   *
   * @param key Shader key
   * @return true
   * @return false
   */
  [[nodiscard]] bool contains(const shader_key& key) const;

  /**
   * @brief Copy the last compile log for a key
   *
   * @param key Shader key
   * @return std::string Empty when the key is unknown
   */
  [[nodiscard]] std::string get_compile_log(const shader_key& key) const;

  /**
   * @brief Remove and destroy one cached shader
   *
   * @param key Shader key
   * @return true if the key existed
   */
  bool remove(const shader_key& key);

  /**
   * @brief Remove and destroy every cached shader
   *
   */
  void clear();

  /**
   * @brief Number of cached keys, including failed compiles
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t size() const;

  /**
   * @brief Statistics snapshot
   *
   * @return shader_manager_stats
   */
  [[nodiscard]] shader_manager_stats get_stats() const;

private:
  struct entry {
    Diligent::IShader* shader = nullptr;
    std::string source;
    std::string source_name;
    std::filesystem::path source_path;
    std::size_t source_hash = 0;
    std::filesystem::file_time_type mtime{};
    std::string compile_log;
    std::vector<std::byte> bytecode;
    bool bytecode_backed = false;
  };

  static std::size_t hash_source(std::string_view source) noexcept;
  static std::size_t hash_bytecode(std::span<const std::byte> bytecode) noexcept;
  static bool
  read_file(const std::filesystem::path& path, std::string& source, std::filesystem::file_time_type& mtime) noexcept;

  Diligent::IShader* compile_locked(const shader_key& key, entry& target, std::string source);
  Diligent::IShader* compile_bytecode_locked(const shader_key& key, entry& target, std::span<const std::byte> bytecode);
  bool expand_includes(std::string_view source,
                       const std::filesystem::path& source_path,
                       std::string& expanded,
                       std::string& log,
                       std::vector<std::filesystem::path>& stack,
                       std::size_t depth) const;
  void destroy_entry(entry& value) noexcept;

  std::unique_ptr<detail::shader_compiler> owned_compiler_;
  detail::shader_compiler* compiler_ = nullptr;

  mutable std::mutex mutex_;
  std::unordered_map<shader_key, entry, shader_key_hash> cache_;
  std::vector<std::filesystem::path> include_paths_;

  mutable std::uint64_t source_reads_ = 0;
  mutable std::uint64_t compiles_ = 0;
  mutable std::uint64_t compile_failures_ = 0;
  mutable std::uint64_t cache_hits_ = 0;
  mutable std::uint64_t reloads_ = 0;
  mutable std::uint64_t include_files_read_ = 0;
  mutable std::uint64_t bytecode_compiles_ = 0;
};

} // namespace render_core
} // namespace cyberverse
