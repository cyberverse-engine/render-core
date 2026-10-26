/**
 * @file shader_manager_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader manager tests
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "cyberverse/render_core/detail/shader_compiler.hpp"
#include "cyberverse/render_core/shader_manager.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::detail::diligent_shader_compiler;
using cyberverse::render_core::detail::shader_compiler;

namespace {

class fake_shader_compiler final : public shader_compiler {
public:
  Diligent::IShader* compile(const shader_key& key,
                             std::string_view source,
                             const std::filesystem::path& source_path,
                             std::string& compile_log) override {
    ++compile_calls;
    last_key = key;
    last_source.assign(source);
    last_path = source_path;

    if (throw_next) {
      throw_next = false;
      throw std::runtime_error{ "fake compiler failure" };
    }
    if (fail) {
      compile_log = "fake failure";
      return nullptr;
    }

    compile_log = "fake ok";
    auto* shader = reinterpret_cast<Diligent::IShader*>(next_id++ * 64u);
    compiled.push_back(shader);
    return shader;
  }

  Diligent::IShader*
  compile_bytecode(const shader_key& key, std::span<const std::byte> bytecode, std::string& compile_log) override {
    ++bytecode_calls;
    last_key = key;
    last_bytecode.assign(bytecode.begin(), bytecode.end());
    compile_log = "fake bytecode ok";
    auto* shader = reinterpret_cast<Diligent::IShader*>(next_id++ * 64u);
    compiled.push_back(shader);
    return shader;
  }

  void destroy(Diligent::IShader* shader) noexcept override { destroyed.push_back(shader); }

  std::uintptr_t next_id = 1;
  int compile_calls = 0;
  bool fail = false;
  bool throw_next = false;
  shader_key last_key;
  std::string last_source;
  std::filesystem::path last_path;
  std::vector<Diligent::IShader*> compiled;
  std::vector<Diligent::IShader*> destroyed;
  int bytecode_calls = 0;
  std::vector<std::byte> last_bytecode;
};

class temp_dir {
public:
  temp_dir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("render_core_shader_manager_" + std::to_string(stamp) + "_" + std::to_string(counter_++));
    std::filesystem::create_directories(path_);
  }

  ~temp_dir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  temp_dir(const temp_dir&) = delete;
  temp_dir(temp_dir&&) = delete;
  temp_dir& operator=(const temp_dir&) = delete;
  temp_dir& operator=(temp_dir&&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  [[nodiscard]] std::filesystem::path file(std::string_view name) const {
    return path_ / std::filesystem::path{ name };
  }

private:
  std::filesystem::path path_;
  static inline std::atomic<std::uint64_t> counter_{ 0 };
};

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream file{ path, std::ios::binary | std::ios::trunc };
  REQUIRE(file.is_open());
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

TEST_CASE("diligent_shader_compiler requires a device", "[render-core][shader-manager]") {
  REQUIRE_THROWS_AS(diligent_shader_compiler{ nullptr }, std::invalid_argument);
}

TEST_CASE("shader_manager caches file shaders", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("opaque.hlsl");
  write_text(path, "float4 main() : SV_Target { return 0; }");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("opaque.hlsl", shader_stage::pixel);

  Diligent::IShader* first = manager.get_or_create_from_file(key, path);
  REQUIRE(first != nullptr);
  REQUIRE(first == compiler.compiled.at(0));
  REQUIRE(compiler.compile_calls == 1);
  REQUIRE(compiler.last_path == path);
  REQUIRE(compiler.last_source.find("SV_Target") != std::string::npos);
  REQUIRE(manager.get_compile_log(key) == "fake ok");
  REQUIRE(manager.contains(key));

  Diligent::IShader* second = manager.get_or_create_from_file(key, path);
  REQUIRE(second == first);
  REQUIRE(compiler.compile_calls == 1);
  REQUIRE(compiler.destroyed.empty());

  const shader_manager_stats stats = manager.get_stats();
  REQUIRE(stats.shader_count == 1);
  REQUIRE(stats.compiles == 1);
  REQUIRE(stats.cache_hits == 1);
  REQUIRE(stats.source_reads == 2);
  REQUIRE(stats.compile_failures == 0);
}

TEST_CASE("shader_manager reloads changed file shaders", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("opaque.hlsl");
  write_text(path, "version 1");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("opaque.hlsl", shader_stage::pixel);

  Diligent::IShader* first = manager.get_or_create_from_file(key, path);
  REQUIRE(first != nullptr);
  REQUIRE_FALSE(manager.is_source_changed(key));

  write_text(path, "version 2");
  REQUIRE(manager.is_source_changed(key));

  REQUIRE(manager.reload(key, false));
  REQUIRE(compiler.compile_calls == 2);
  REQUIRE(compiler.destroyed == std::vector<Diligent::IShader*>{ first });

  Diligent::IShader* second = manager.get(key);
  REQUIRE(second != nullptr);
  REQUIRE(second != first);
  REQUIRE_FALSE(manager.is_source_changed(key));
  REQUIRE_FALSE(manager.reload(key, false));

  REQUIRE(manager.reload(key, true));
  REQUIRE(compiler.compile_calls == 3);
  REQUIRE(manager.get_stats().reloads == 2);
}

TEST_CASE("shader_manager keeps the last good shader after a failed reload", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("opaque.hlsl");
  write_text(path, "version 1");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("opaque.hlsl", shader_stage::pixel);

  Diligent::IShader* first = manager.get_or_create_from_file(key, path);
  REQUIRE(first != nullptr);

  write_text(path, "version 2");
  compiler.fail = true;

  Diligent::IShader* after_failure = manager.get_or_create_from_file(key, path);
  REQUIRE(after_failure == first);
  REQUIRE(compiler.destroyed.empty());
  REQUIRE(manager.get_compile_log(key) == "fake failure");
  REQUIRE(manager.get_stats().compile_failures == 1);

  REQUIRE_FALSE(manager.reload(key, false));
  REQUIRE(manager.get(key) == first);
  REQUIRE(manager.get_stats().compile_failures == 2);

  compiler.fail = false;
  REQUIRE(manager.reload(key, false));
  REQUIRE(manager.get(key) != first);
  REQUIRE(compiler.destroyed == std::vector<Diligent::IShader*>{ first });
}

TEST_CASE("shader_manager handles compiler exceptions", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("broken.hlsl");
  write_text(path, "broken");

  fake_shader_compiler compiler;
  compiler.throw_next = true;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("broken.hlsl", shader_stage::vertex);

  REQUIRE(manager.get_or_create_from_file(key, path) == nullptr);
  REQUIRE(manager.get_compile_log(key).find("exception") != std::string::npos);
  REQUIRE(manager.get_stats().compile_failures == 1);
}

TEST_CASE("shader_manager handles missing files", "[render-core][shader-manager]") {
  temp_dir directory;
  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("missing.hlsl", shader_stage::vertex);

  REQUIRE(manager.get_or_create_from_file(key, directory.file("missing.hlsl")) == nullptr);
  REQUIRE(compiler.compile_calls == 0);
  REQUIRE_FALSE(manager.contains(key));
  REQUIRE_FALSE(manager.is_source_changed(key));
  REQUIRE_FALSE(manager.reload(key, false));
}

TEST_CASE("shader_manager caches in-memory sources", "[render-core][shader-manager]") {
  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("memory.hlsl", shader_stage::compute, { { "MODE", "1" } });

  Diligent::IShader* first = manager.get_or_create_from_source(key, "source 1", "memory");
  REQUIRE(first != nullptr);
  REQUIRE(compiler.last_key.name == "memory.hlsl");
  REQUIRE(compiler.last_key.defines.at(0).first == "MODE");

  REQUIRE(manager.get_or_create_from_source(key, "source 1", "memory") == first);
  REQUIRE(compiler.compile_calls == 1);
  REQUIRE_FALSE(manager.reload(key, false));

  Diligent::IShader* second = manager.get_or_create_from_source(key, "source 2");
  REQUIRE(second != nullptr);
  REQUIRE(second != first);
  REQUIRE(compiler.compile_calls == 2);
  REQUIRE(compiler.destroyed == std::vector<Diligent::IShader*>{ first });
  REQUIRE(manager.get_or_create_from_source(key, "source 2") == second);
  REQUIRE(manager.get_stats().cache_hits == 2);
}

TEST_CASE("shader_manager normalizes directly constructed keys", "[render-core][shader-manager]") {
  fake_shader_compiler compiler;
  shader_manager manager{ compiler };

  shader_key unsorted;
  unsorted.name = "normalized.hlsl";
  unsorted.stage = shader_stage::vertex;
  unsorted.defines = { { "B", "2" }, { "A", "1" } };

  const shader_key sorted = make_shader_key("normalized.hlsl", shader_stage::vertex, { { "A", "1" }, { "B", "2" } });

  Diligent::IShader* first = manager.get_or_create_from_source(unsorted, "source");
  Diligent::IShader* second = manager.get_or_create_from_source(sorted, "source");
  REQUIRE(first != nullptr);
  REQUIRE(second == first);
  REQUIRE(compiler.compile_calls == 1);
}

TEST_CASE("shader_manager remove and clear destroy shaders", "[render-core][shader-manager]") {
  fake_shader_compiler compiler;
  shader_manager manager{ compiler };

  const shader_key first_key = make_shader_key("a.hlsl", shader_stage::vertex);
  const shader_key second_key = make_shader_key("b.hlsl", shader_stage::pixel);
  Diligent::IShader* first = manager.get_or_create_from_source(first_key, "a");
  Diligent::IShader* second = manager.get_or_create_from_source(second_key, "b");
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(manager.size() == 2);

  REQUIRE(manager.remove(first_key));
  REQUIRE_FALSE(manager.remove(first_key));
  REQUIRE(compiler.destroyed == std::vector<Diligent::IShader*>{ first });
  REQUIRE(manager.size() == 1);

  manager.clear();
  REQUIRE(compiler.destroyed == std::vector<Diligent::IShader*>{ first, second });
  REQUIRE(manager.size() == 0);
}

TEST_CASE("shader_manager destructor destroys cached shaders", "[render-core][shader-manager]") {
  fake_shader_compiler compiler;
  {
    shader_manager manager{ compiler };
    REQUIRE(manager.get_or_create_from_source(make_shader_key("a.hlsl", shader_stage::vertex), "a") != nullptr);
  }
  REQUIRE(compiler.destroyed.size() == 1);
}

TEST_CASE("shader_manager is thread safe", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("threaded.hlsl");
  write_text(path, "threaded source");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("threaded.hlsl", shader_stage::vertex);

  std::vector<Diligent::IShader*> results(8, nullptr);
  std::vector<std::thread> threads;
  threads.reserve(results.size());
  for (std::size_t index = 0; index < results.size(); ++index) {
    threads.emplace_back([&manager, &key, &path, &results, index] {
      results[index] = manager.get_or_create_from_file(key, path);
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  REQUIRE(compiler.compile_calls == 1);
  for (Diligent::IShader* result : results) {
    REQUIRE(result == results.front());
    REQUIRE(result != nullptr);
  }
  REQUIRE(manager.size() == 1);
}

TEST_CASE("shader_manager reports a deleted source as changed", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto path = directory.file("deleted.hlsl");
  write_text(path, "source");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("deleted.hlsl", shader_stage::vertex);

  REQUIRE(manager.get_or_create_from_file(key, path) != nullptr);
  REQUIRE_FALSE(manager.is_source_changed(key));

  std::error_code error;
  std::filesystem::remove(path, error);
  REQUIRE_FALSE(error);
  REQUIRE(manager.is_source_changed(key));
}

TEST_CASE("shader_manager expands relative includes", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto include_path = directory.file("common.hlsli");
  const auto shader_path = directory.file("include_test.hlsl");
  write_text(include_path, "#define VALUE 7\n");
  write_text(shader_path, "#include \"common.hlsli\"\nfloat4 main() : SV_Target { return VALUE; }\n");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("include_test.hlsl", shader_stage::pixel);

  REQUIRE(manager.get_or_create_from_file(key, shader_path) != nullptr);
  REQUIRE(compiler.last_source.find("#define VALUE 7") != std::string::npos);
  REQUIRE(compiler.last_source.find("#include") == std::string::npos);
  REQUIRE(manager.get_stats().include_files_read >= 1);
}

TEST_CASE("shader_manager reports missing includes without crashing", "[render-core][shader-manager]") {
  temp_dir directory;
  const auto shader_path = directory.file("missing_include.hlsl");
  write_text(shader_path, "#include \"missing.hlsli\"\nfloat4 main() : SV_Target { return 0; }\n");

  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("missing_include.hlsl", shader_stage::pixel);

  REQUIRE(manager.get_or_create_from_file(key, shader_path) == nullptr);
  REQUIRE(compiler.compile_calls == 0);
  REQUIRE(manager.get_compile_log(key).find("include not found") != std::string::npos);
}

TEST_CASE("shader_manager caches bytecode shaders", "[render-core][shader-manager]") {
  fake_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("bytecode.vert", shader_stage::vertex);
  const std::byte bytecode[] = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };

  Diligent::IShader* first = manager.get_or_create_from_bytecode(key, bytecode);
  REQUIRE(first != nullptr);
  REQUIRE(compiler.bytecode_calls == 1);
  REQUIRE(compiler.last_bytecode.size() == 3);
  REQUIRE(manager.get_or_create_from_bytecode(key, bytecode) == first);
  REQUIRE(compiler.bytecode_calls == 1);
  REQUIRE(manager.get_stats().bytecode_compiles == 1);
}
