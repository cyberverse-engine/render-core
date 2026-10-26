#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/gpu_data_ref.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/this_thread.hpp"
#include "fake_gpu_data_backend.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::test::fake_gpu_data_backend;
using cyberverse::render_core::test::make_buffer_desc;

TEST_CASE("default gpu_data_ref is invalid", "[render-core][gpu-data-ref]") {
  const gpu_data_ref ref;
  REQUIRE_FALSE(ref.is_valid());
  REQUIRE_FALSE(ref.is_ready());
  REQUIRE_FALSE(static_cast<bool>(ref));
  REQUIRE(ref.type() == gpu_data_type::undefined);
  REQUIRE(ref.current_version() == 0);
  REQUIRE_FALSE(ref.upload_state(1).has_value());
}

TEST_CASE("gpu_data_ref copy and move preserve the logical resource", "[render-core][gpu-data-ref]") {
  cyberverse::runtime::set_this_as_render_thread();
  auto backend = std::make_unique<fake_gpu_data_backend>();
  fake_gpu_data_backend& backend_ref = *backend;
  gpu_data_manager manager{ std::move(backend) };
  const auto unlimited = cyberverse::runtime::frame_budget::unlimited();
  auto uri_result = make_resource_uri("@buffer/cyberverse-engine/ref-test");
  REQUIRE(uri_result.has_value());

  auto result = manager.submit_buffer(*uri_result, make_buffer_desc());
  REQUIRE(result.has_value());
  auto [ref, version] = std::move(*result);
  manager.tick(unlimited);
  manager.tick(unlimited);
  REQUIRE(ref.is_ready());
  REQUIRE(ref.current_version() == version);
  REQUIRE(ref.upload_state(version) == gpu_data_state::ready);

  buffer_data_ref copy = ref;
  buffer_data_ref moved = std::move(copy);
  REQUIRE(moved.get() == ref.get());

  ref.reset();
  REQUIRE(backend_ref.buffer_destroys == 0);
  REQUIRE(moved.get() != nullptr);

  moved.reset();
  REQUIRE(backend_ref.buffer_destroys == 0);
  manager.tick(unlimited);
  REQUIRE(backend_ref.buffer_destroys == 1);
}
