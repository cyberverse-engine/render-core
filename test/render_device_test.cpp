/**
 * @file render_device_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render device validation tests
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "cyberverse/platform.hpp"
#include "cyberverse/render_core/detail/gpu_data_backend.hpp"
#include "cyberverse/render_core/render_device.hpp"
#include "cyberverse/runtime/frame_clock.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::detail::diligent_gpu_data_backend;
using cyberverse::runtime::steady_frame_clock;
namespace platform = cyberverse::platform;

namespace {

platform::native_window_handle make_native_handle() {
  platform::native_window_handle handle;
  handle.backend = platform::backend_type::glfw;
#if defined(_WIN32)
  handle.window = reinterpret_cast<void*>(0x1000);
#elif defined(__APPLE__)
  handle.view = reinterpret_cast<void*>(0x1000);
  handle.surface = reinterpret_cast<void*>(0x2000);
#elif defined(__linux__)
  handle.window = reinterpret_cast<void*>(0x1000);
  handle.display = reinterpret_cast<void*>(0x2000);
#else
  handle.window = reinterpret_cast<void*>(0x1000);
#endif
  return handle;
}

render_device_desc make_valid_desc() {
  render_device_desc desc;
  desc.window = make_native_handle();
  desc.width = 1280;
  desc.height = 720;
  return desc;
}

} // namespace

TEST_CASE("render_backend names are stable", "[render-core][render-device]") {
  REQUIRE(to_string(render_backend::vulkan) == "vulkan");
  REQUIRE(to_string(render_backend::d3d12) == "d3d12");
}

TEST_CASE("render_backend availability matches the build", "[render-core][render-device]") {
  REQUIRE(is_backend_available(render_backend::vulkan) == (CYBERVERSE_RENDER_CORE_BACKEND_VULKAN != 0));
  REQUIRE(is_backend_available(render_backend::d3d12) == (CYBERVERSE_RENDER_CORE_BACKEND_D3D12 != 0));

  if (is_backend_available(render_backend::vulkan)) {
    render_device_desc desc = make_valid_desc();
    desc.backend = render_backend::vulkan;
    REQUIRE_NOTHROW(validate_render_device_desc(desc));
  }

  if (is_backend_available(render_backend::d3d12)) {
    render_device_desc desc = make_valid_desc();
    desc.backend = render_backend::d3d12;
    REQUIRE_NOTHROW(validate_render_device_desc(desc));
  } else {
    render_device_desc desc = make_valid_desc();
    desc.backend = render_backend::d3d12;
    REQUIRE_THROWS_AS(validate_render_device_desc(desc), std::invalid_argument);
  }
}

TEST_CASE("render_device_desc validates a native window", "[render-core][render-device]") {
  REQUIRE_NOTHROW(validate_render_device_desc(make_valid_desc()));

  render_device_desc no_handle = make_valid_desc();
  no_handle.window = {};
  REQUIRE_THROWS_AS(validate_render_device_desc(no_handle), std::invalid_argument);

  render_device_desc headless = make_valid_desc();
  headless.window.backend = platform::backend_type::headless;
  REQUIRE_THROWS_AS(validate_render_device_desc(headless), std::invalid_argument);

  render_device_desc zero_width = make_valid_desc();
  zero_width.width = 0;
  REQUIRE_THROWS_AS(validate_render_device_desc(zero_width), std::invalid_argument);

  render_device_desc zero_height = make_valid_desc();
  zero_height.height = 0;
  REQUIRE_THROWS_AS(validate_render_device_desc(zero_height), std::invalid_argument);

  render_device_desc few_buffers = make_valid_desc();
  few_buffers.buffer_count = 1;
  REQUIRE_THROWS_AS(validate_render_device_desc(few_buffers), std::invalid_argument);

  render_device_desc no_format = make_valid_desc();
  no_format.color_format = Diligent::TEX_FORMAT_UNKNOWN;
  REQUIRE_THROWS_AS(validate_render_device_desc(no_format), std::invalid_argument);
}

TEST_CASE("render_device::create validates before touching the GPU", "[render-core][render-device]") {
  render_device_desc desc = make_valid_desc();
  desc.window.backend = platform::backend_type::headless;
  REQUIRE_THROWS_AS(render_device::create(desc), std::invalid_argument);

  desc = make_valid_desc();
  desc.width = 0;
  REQUIRE_THROWS_AS(render_device::create(desc), std::invalid_argument);
}

TEST_CASE("diligent_gpu_data_backend requires a device", "[render-core][render-device]") {
  REQUIRE_THROWS_AS(diligent_gpu_data_backend{ static_cast<Diligent::IRenderDevice*>(nullptr) }, std::invalid_argument);
}

TEST_CASE("render_device creates a swap chain on a real window", "[render-core][render-device][gpu]") {
  if (std::getenv("CYBERVERSE_RENDER_CORE_TEST_GPU") == nullptr) {
    SKIP("set CYBERVERSE_RENDER_CORE_TEST_GPU=1 to run the window/GPU integration test");
  }

  bool ran = false;
  for (render_backend backend : { render_backend::vulkan, render_backend::d3d12 }) {
    if (!is_backend_available(backend)) {
      continue;
    }
    INFO("backend: " << to_string(backend));
    ran = true;

    auto platform = platform::make_platform(platform::backend_type::glfw);
    platform::window_config window_config;
    window_config.width = 320;
    window_config.height = 240;
    window_config.title = "render-core test";
    window_config.vsync = false;
    auto window = platform->create_window(window_config);

    render_device_desc desc;
    desc.backend = backend;
    desc.window = window->native_handle();
    desc.width = static_cast<std::uint32_t>(window->width());
    desc.height = static_cast<std::uint32_t>(window->height());
    desc.vsync = false;
    auto device = render_device::create(desc);

    REQUIRE(device != nullptr);
    REQUIRE(device->device() != nullptr);
    REQUIRE(device->immediate_context() != nullptr);
    REQUIRE(device->swap_chain() != nullptr);
    REQUIRE(device->backend_name() == to_string(backend));
    REQUIRE(device->width() == desc.width);
    REQUIRE(device->height() == desc.height);

    frame_context ctx = device->begin_frame(0.016);
    REQUIRE(ctx.is_valid());
    REQUIRE(ctx.color_target() != nullptr);
    REQUIRE(ctx.depth_target() != nullptr);
    REQUIRE(device->frame_index() == 0);
    REQUIRE(ctx.has_device());
    REQUIRE(&ctx.device() == device.get());
    REQUIRE(ctx.device().device() == device->device());
    REQUIRE(ctx.frame_budget().is_unlimited());
    REQUIRE(ctx.gpu_data() == &device->gpu_data());
    REQUIRE(ctx.shaders() == &device->shaders());
    REQUIRE(&ctx.device().pipelines() == &device->pipelines());
    REQUIRE(device->pipelines().pso_count() == 0);
    REQUIRE(device->gpu_data().live_resource_count() == 0);
    REQUIRE(device->shaders().size() == 0);

    cyberverse::runtime::steady_frame_clock budget_clock;
    frame_context limited = device->begin_frame(0.016, 0.0, cyberverse::runtime::frame_budget{ 1.0, budget_clock });
    REQUIRE_FALSE(limited.frame_budget().is_unlimited());
    REQUIRE(limited.frame_budget().has_remaining());
    REQUIRE(device->frame_index() == 0);
    REQUIRE(limited.frame_index() == ctx.frame_index());

    const shader_key shader_key =
        make_shader_key(backend == render_backend::vulkan ? "smoke.vert" : "smoke.hlsl", shader_stage::vertex);
    const std::string shader_source = backend == render_backend::vulkan
                                          ? "#version 450\nvoid main() { gl_Position = vec4(0.0); }\n"
                                          : "float4 main(float4 p : POSITION) : SV_POSITION { return p; }\n";
    Diligent::IShader* shader = device->shaders().get_or_create_from_source(shader_key, shader_source, "smoke");
    INFO("shader log: " << device->shaders().get_compile_log(shader_key));
    REQUIRE(shader != nullptr);
    REQUIRE(device->shaders().get_or_create_from_source(shader_key, shader_source, "smoke") == shader);
    REQUIRE(device->shaders().size() == 1);

    device->present();
    REQUIRE(device->frame_index() == 1);
    device->wait_idle();
    device->resize(desc.width, desc.height);
    REQUIRE(device->width() == desc.width);

    device->release_all_resources();
    REQUIRE(device->gpu_data().live_resource_count() == 0);
    REQUIRE(device->shaders().size() == 0);
    REQUIRE(device->pipelines().pso_count() == 0);
    REQUIRE(device->pipelines().binding_rule_count() == 0);
    REQUIRE(device->pipelines().srb_count() == 0);
  }
  REQUIRE(ran);
}
