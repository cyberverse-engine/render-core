#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/frame_clock.hpp"
#include "cyberverse/runtime/this_thread.hpp"
#include "fake_gpu_data_backend.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::test::fake_gpu_data_backend;
using cyberverse::render_core::test::make_buffer_desc;
using cyberverse::render_core::test::make_texture_desc;

namespace {

cyberverse::runtime::frame_budget unlimited() { return cyberverse::runtime::frame_budget::unlimited(); }

struct fixture {
  std::unique_ptr<fake_gpu_data_backend> backend_ptr{ std::make_unique<fake_gpu_data_backend>() };
  fake_gpu_data_backend& backend = *backend_ptr;
  gpu_data_manager manager{ std::move(backend_ptr) };

  fixture() { cyberverse::runtime::set_this_as_render_thread(); }

  void tick() { manager.tick(unlimited()); }
  void complete_upload() {
    tick();
    tick();
  }
};

resource_uri uri(std::string_view value) {
  auto result = make_resource_uri(value);
  REQUIRE(result.has_value());
  return *result;
}

resource_uri unique_uri(resource_type type) {
  static std::uint64_t next = 1;
  const std::string prefix =
      type == resource_type::texture ? "@texture/cyberverse-engine/unique-" : "@buffer/cyberverse-engine/unique-";
  return uri(prefix + std::to_string(next++));
}

} // namespace

TEST_CASE("gpu_data_manager starts empty", "[render-core][gpu-data-manager]") {
  fixture value;
  const gpu_data_manager_stats stats = value.manager.get_stats();
  REQUIRE(stats.texture_count == 0);
  REQUIRE(stats.buffer_count == 0);
  REQUIRE(value.manager.live_resource_count() == 0);
  REQUIRE(value.manager.tick_count() == 0);
}

TEST_CASE("gpu_data_manager queues texture submissions until tick", "[render-core][gpu-data-manager]") {
  fixture value;
  auto result = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);

  REQUIRE(ref.is_valid());
  REQUIRE(ref.type() == gpu_data_type::texture);
  REQUIRE(ref.state() == gpu_data_state::pending);
  REQUIRE_FALSE(ref.is_ready());
  REQUIRE_FALSE(ref.get() != nullptr);
  REQUIRE(ref.current_version() == 0);
  REQUIRE(version == 1);
  REQUIRE(value.backend.texture_creates == 0);
  REQUIRE(value.manager.get_stats().texture_count == 1);

  value.tick();
  REQUIRE(value.backend.texture_creates == 1);
  REQUIRE(ref.state() == gpu_data_state::uploading);
  REQUIRE(ref.upload_state(version) == gpu_data_state::uploading);

  value.tick();
  REQUIRE(ref.state() == gpu_data_state::ready);
  REQUIRE(ref.is_ready());
  REQUIRE(ref.get() == value.backend.created_textures.at(0));
  REQUIRE(ref.current_version() == version);
  REQUIRE(ref.upload_state(version) == gpu_data_state::ready);
}

TEST_CASE("gpu_data_manager queues buffer submissions until tick", "[render-core][gpu-data-manager]") {
  fixture value;
  auto result = value.manager.submit_buffer(unique_uri(resource_type::buffer), make_buffer_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);

  REQUIRE(ref.type() == gpu_data_type::buffer);
  REQUIRE_FALSE(ref.get() != nullptr);
  REQUIRE(value.backend.buffer_creates == 0);

  value.complete_upload();
  REQUIRE(ref.is_ready());
  REQUIRE(ref.get() == value.backend.created_buffers.at(0));
  REQUIRE(ref.current_version() == version);
}

TEST_CASE("gpu_data_manager validates submit arguments immediately", "[render-core][gpu-data-manager]") {
  fixture value;

  auto no_uri = value.manager.submit_texture(resource_uri{}, make_texture_desc());
  REQUIRE_FALSE(no_uri.has_value());
  REQUIRE(no_uri.error() == make_error_code(render_core_errc::invalid_argument));
  REQUIRE_FALSE(value.manager.get_texture(resource_uri{}).is_valid());

  texture_submit_desc bad = make_texture_desc();
  bad.width = 0;
  auto texture = value.manager.submit_texture(unique_uri(resource_type::texture), bad);
  REQUIRE_FALSE(texture.has_value());
  REQUIRE(texture.error() == make_error_code(render_core_errc::invalid_argument));

  buffer_submit_desc bad_buffer = make_buffer_desc();
  bad_buffer.size = 0;
  auto buffer = value.manager.submit_buffer(unique_uri(resource_type::buffer), bad_buffer);
  REQUIRE_FALSE(buffer.has_value());
  REQUIRE(buffer.error() == make_error_code(render_core_errc::invalid_argument));

  const resource_uri texture_uri = uri("@texture/cyberverse-engine/mismatch");
  auto mismatch = value.manager.submit_buffer(texture_uri, make_buffer_desc());
  REQUIRE_FALSE(mismatch.has_value());
  REQUIRE(mismatch.error() == make_error_code(render_core_errc::invalid_argument));
  REQUIRE(value.backend.texture_creates == 0);
  REQUIRE(value.backend.buffer_creates == 0);
}

TEST_CASE("gpu_data_manager query API returns refs", "[render-core][gpu-data-manager][resource-uri]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/query-texture");
  const resource_uri buffer_uri = uri("@buffer/cyberverse-engine/query-buffer");

  auto texture = value.manager.submit_texture(texture_uri, make_texture_desc());
  auto buffer = value.manager.submit_buffer(buffer_uri, make_buffer_desc());
  REQUIRE(texture.has_value());
  REQUIRE(buffer.has_value());

  texture_data_ref texture_ref = value.manager.get_texture(texture_uri);
  buffer_data_ref buffer_ref = value.manager.get_buffer(buffer_uri);
  REQUIRE(texture_ref.is_valid());
  REQUIRE(buffer_ref.is_valid());
  REQUIRE(texture_ref.state() == gpu_data_state::pending);
  REQUIRE(value.manager.get_state(texture_uri) == gpu_data_state::pending);

  value.complete_upload();
  REQUIRE(texture_ref.is_ready());
  REQUIRE(buffer_ref.is_ready());
  REQUIRE(texture_ref.get() == value.backend.created_textures.at(0));
  REQUIRE(buffer_ref.get() == value.backend.created_buffers.at(0));

  const resource_uri missing = uri("@texture/cyberverse-engine/missing");
  REQUIRE_FALSE(value.manager.get_texture(missing).is_valid());
  REQUIRE(value.manager.get_state(missing) == gpu_data_state::failed);
}

TEST_CASE("gpu_data_manager overwrite keeps old current until new upload completes",
          "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/overwrite");

  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());
  auto [first_ref, first_version] = std::move(*first);
  value.complete_upload();
  REQUIRE(first_ref.is_ready());
  REQUIRE(first_ref.current_version() == first_version);
  REQUIRE(first_ref.get() == value.backend.created_textures.at(0));

  auto second = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(second.has_value());
  auto [second_ref, second_version] = std::move(*second);
  REQUIRE(second_version > first_version);
  REQUIRE(second_ref.is_valid());
  REQUIRE(second_ref.current_version() == first_version);
  REQUIRE(second_ref.get() == value.backend.created_textures.at(0));
  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::pending);

  value.tick();
  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::uploading);
  REQUIRE(second_ref.get() == value.backend.created_textures.at(0));

  value.tick();
  REQUIRE(second_ref.current_version() == second_version);
  REQUIRE(second_ref.get() == value.backend.created_textures.at(1));
  REQUIRE(second_ref.upload_state(first_version) == gpu_data_state::ready);
}

TEST_CASE("gpu_data_manager cancels the previous unfinished upload", "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/cancel");

  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());
  auto [first_ref, first_version] = std::move(*first);

  auto second = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(second.has_value());
  auto [second_ref, second_version] = std::move(*second);

  value.tick();
  REQUIRE(first_ref.upload_state(first_version) == gpu_data_state::canceled);
  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::uploading);
  REQUIRE(second_ref.get() == nullptr);

  value.tick();
  REQUIRE(second_ref.is_ready());
  REQUIRE(second_ref.current_version() == second_version);
}

TEST_CASE("gpu_data_manager keeps upload errors on the submitted version", "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/upload-error");

  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());
  auto [first_ref, first_version] = std::move(*first);
  value.complete_upload();
  REQUIRE(first_ref.is_ready());

  value.backend.fail_textures = true;
  auto second = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(second.has_value());
  auto [second_ref, second_version] = std::move(*second);
  value.tick();
  value.tick();

  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::failed);
  REQUIRE_FALSE(second_ref.upload_error(second_version).empty());
  REQUIRE(second_ref.current_version() == first_version);
  REQUIRE(second_ref.get() == value.backend.created_textures.at(0));
  REQUIRE(second_ref.state() == gpu_data_state::ready);
}

TEST_CASE("gpu_data_manager overwrite=false rejects a live URI", "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/no-overwrite");
  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());

  auto second = value.manager.submit_texture(texture_uri, make_texture_desc(), false);
  REQUIRE_FALSE(second.has_value());
  REQUIRE(second.error() == make_error_code(render_core_errc::already_exists));
  REQUIRE(value.manager.get_stats().versions_submitted == 1);
}

TEST_CASE("gpu_data_manager reference lifetime defers destruction to tick", "[render-core][gpu-data-manager]") {
  fixture value;
  auto result = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);
  value.complete_upload();
  REQUIRE(ref.is_ready());
  Diligent::ITexture* raw = ref.get();

  texture_data_ref copy = ref;
  ref.reset();
  REQUIRE(value.backend.texture_destroys == 0);
  REQUIRE(copy.get() == raw);

  copy.reset();
  REQUIRE(value.backend.texture_destroys == 0);
  value.tick();
  REQUIRE(value.backend.texture_destroys == 1);
  REQUIRE(value.backend.destroyed_textures.at(0) == raw);
}

TEST_CASE("gpu_data_manager honors the retire delay for LRU", "[render-core][gpu-data-manager]") {
  fixture value;
  value.manager.set_retire_delay_ticks(2);
  auto result = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);
  value.complete_upload();
  ref.reset();

  value.tick();
  REQUIRE(value.backend.texture_destroys == 0);
  value.tick();
  REQUIRE(value.backend.texture_destroys == 1);
}

TEST_CASE("gpu_data_manager immediate release still runs in tick", "[render-core][gpu-data-manager]") {
  fixture value;
  value.manager.set_release_policy(gpu_data_release_policy::immediate);
  value.manager.set_retire_delay_ticks(10);
  auto result = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);
  value.complete_upload();
  ref.reset();

  REQUIRE(value.backend.texture_destroys == 0);
  value.tick();
  REQUIRE(value.backend.texture_destroys == 1);
}

TEST_CASE("gpu_data_manager keeps at most five version records", "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/five");

  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());
  auto [ref, first_version] = std::move(*first);
  value.complete_upload();

  submit_version oldest_removed = 0;
  for (int index = 0; index < 5; ++index) {
    auto next = value.manager.submit_texture(texture_uri, make_texture_desc());
    REQUIRE(next.has_value());
    (void)std::get<0>(*next);
    if (index == 0) {
      oldest_removed = std::get<1>(*next);
    }
  }

  value.tick();
  REQUIRE(ref.current_version() == first_version);
  REQUIRE_FALSE(ref.upload_state(oldest_removed).has_value());
  REQUIRE(value.manager.get_stats().versions_pruned >= 1);
  REQUIRE(ref.upload_state(first_version) == gpu_data_state::ready);
}

TEST_CASE("gpu_data_manager enforces its memory budget", "[render-core][gpu-data-manager]") {
  fixture value;
  value.manager.set_memory_budget_bytes(128);
  buffer_submit_desc desc = make_buffer_desc();
  desc.size = 100;

  auto live = value.manager.submit_buffer(unique_uri(resource_type::buffer), desc);
  REQUIRE(live.has_value());
  auto [live_ref, live_version] = std::move(*live);
  value.complete_upload();
  REQUIRE(live_ref.is_ready());

  auto second = value.manager.submit_buffer(unique_uri(resource_type::buffer), desc);
  REQUIRE(second.has_value());
  auto [second_ref, second_version] = std::move(*second);
  value.tick();
  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::failed);
  REQUIRE(second_ref.upload_error(second_version) == "GPU memory budget exceeded");
}

class stepping_clock final : public cyberverse::runtime::frame_clock {
public:
  double now() noexcept override {
    current += 0.4;
    return current;
  }

  void sleep_until(double) override {}

  double current = 0.0;
};

TEST_CASE("gpu_data_manager checks the tick budget before each release", "[render-core][gpu-data-manager]") {
  fixture value;
  stepping_clock clock;
  auto first = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  auto second = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  auto third = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(third.has_value());
  auto first_ref = std::move(std::get<0>(*first));
  auto second_ref = std::move(std::get<0>(*second));
  auto third_ref = std::move(std::get<0>(*third));
  value.complete_upload();
  first_ref.reset();
  second_ref.reset();
  third_ref.reset();

  cyberverse::runtime::frame_budget budget{ 1.0, clock };
  value.manager.tick(budget);
  REQUIRE(value.backend.texture_destroys > 0);
  REQUIRE(value.backend.texture_destroys < 3);

  value.manager.tick(unlimited());
  REQUIRE(value.backend.texture_destroys == 3);
}

TEST_CASE("gpu_data_manager recovers memory budget room from evictable entries", "[render-core][gpu-data-manager]") {
  fixture value;
  value.manager.set_memory_budget_bytes(128);
  buffer_submit_desc desc = make_buffer_desc();
  desc.size = 100;

  auto first = value.manager.submit_buffer(unique_uri(resource_type::buffer), desc);
  REQUIRE(first.has_value());
  auto [first_ref, first_version] = std::move(*first);
  value.complete_upload();
  REQUIRE(first_ref.is_ready());
  first_ref.reset();

  auto second = value.manager.submit_buffer(unique_uri(resource_type::buffer), desc);
  REQUIRE(second.has_value());
  auto [second_ref, second_version] = std::move(*second);
  value.tick();
  value.tick();
  REQUIRE(second_ref.upload_state(second_version) == gpu_data_state::ready);
  REQUIRE(value.backend.buffer_destroys == 1);
}

TEST_CASE("gpu_data_manager destroys every version when the entry is released", "[render-core][gpu-data-manager]") {
  fixture value;
  const resource_uri texture_uri = uri("@texture/cyberverse-engine/release-all-versions");

  auto first = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(first.has_value());
  auto [ref, first_version] = std::move(*first);
  value.complete_upload();

  auto second = value.manager.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(second.has_value());
  auto second_ref = std::move(std::get<0>(*second));
  value.complete_upload();
  REQUIRE(second_ref.current_version() > first_version);

  ref.reset();
  second_ref.reset();
  value.tick();
  REQUIRE(value.backend.texture_destroys == 2);
}

TEST_CASE("gpu_data_manager starts higher-priority uploads first", "[render-core][gpu-data-manager]") {
  fixture value;
  buffer_submit_desc low = make_buffer_desc();
  low.upload_priority = gpu_data_upload_priority::low;
  low.debug_name = "low";
  buffer_submit_desc high = make_buffer_desc();
  high.upload_priority = gpu_data_upload_priority::high;
  high.debug_name = "high";

  auto low_result = value.manager.submit_buffer(unique_uri(resource_type::buffer), low);
  auto high_result = value.manager.submit_buffer(unique_uri(resource_type::buffer), high);
  REQUIRE(low_result.has_value());
  REQUIRE(high_result.has_value());

  value.tick();
  REQUIRE(value.backend.created_buffers.size() == 2);
  REQUIRE(value.backend.created_buffer_names.front() == "high");
}

TEST_CASE("gpu_data_manager enforces the per-tick upload byte budget", "[render-core][gpu-data-manager]") {
  fixture value;
  value.manager.set_upload_byte_budget_per_tick(sizeof(std::uint32_t) * 16);

  auto first = value.manager.submit_buffer(unique_uri(resource_type::buffer), make_buffer_desc());
  auto second = value.manager.submit_buffer(unique_uri(resource_type::buffer), make_buffer_desc());
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());

  value.tick();
  REQUIRE(value.backend.created_buffers.size() == 1);
  REQUIRE(value.manager.uploaded_bytes_last_tick() == sizeof(std::uint32_t) * 16);
  REQUIRE(value.manager.get_stats().uploaded_bytes_last_tick == sizeof(std::uint32_t) * 16);

  value.tick();
  REQUIRE(value.backend.created_buffers.size() == 2);
}

TEST_CASE("gpu_data_manager reports per-type byte and version statistics", "[render-core][gpu-data-manager]") {
  fixture value;
  auto texture = value.manager.submit_texture(unique_uri(resource_type::texture), make_texture_desc());
  auto buffer = value.manager.submit_buffer(unique_uri(resource_type::buffer), make_buffer_desc());
  REQUIRE(texture.has_value());
  REQUIRE(buffer.has_value());
  value.complete_upload();

  const gpu_data_manager_stats stats = value.manager.get_stats();
  REQUIRE(stats.texture_bytes > 0);
  REQUIRE(stats.buffer_bytes == sizeof(std::uint32_t) * 16);
  REQUIRE(stats.version_count == 2);
}
