/**
 * @file frame_context_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame context tests
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include "cyberverse/render_core/frame_context.hpp"
#include "cyberverse/runtime/frame_clock.hpp"

using namespace cyberverse::render_core;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_clock;

namespace {

class manual_clock final : public frame_clock {
public:
  double now() noexcept override { return now_; }
  void sleep_until(double) override {}
  void advance(double seconds) { now_ += seconds; }

  double now_ = 0.0;
};

Diligent::ITextureView* fake_view(std::uintptr_t value) noexcept {
  return reinterpret_cast<Diligent::ITextureView*>(value);
}

Diligent::IDeviceContext* fake_context(std::uintptr_t value) noexcept {
  return reinterpret_cast<Diligent::IDeviceContext*>(value);
}

} // namespace

TEST_CASE("viewport validates its extent", "[render-core][frame-context]") {
  viewport vp;
  REQUIRE_FALSE(vp.is_valid());

  vp.width = 1920.0f;
  vp.height = 1080.0f;
  REQUIRE(vp.is_valid());

  vp.min_depth = 0.1f;
  vp.max_depth = 0.9f;
  REQUIRE(vp.is_valid());

  vp.max_depth = 1.1f;
  REQUIRE_FALSE(vp.is_valid());

  vp.max_depth = 0.05f;
  REQUIRE_FALSE(vp.is_valid());

  vp.max_depth = 0.5f;
  vp.min_depth = -0.1f;
  REQUIRE_FALSE(vp.is_valid());
}

TEST_CASE("viewport converts to a Diligent viewport", "[render-core][frame-context]") {
  viewport vp;
  vp.top_left_x = 10.0f;
  vp.top_left_y = 20.0f;
  vp.width = 640.0f;
  vp.height = 480.0f;
  vp.min_depth = 0.25f;
  vp.max_depth = 0.75f;

  const Diligent::Viewport diligent = vp.to_diligent();
  REQUIRE(diligent.TopLeftX == 10.0f);
  REQUIRE(diligent.TopLeftY == 20.0f);
  REQUIRE(diligent.Width == 640.0f);
  REQUIRE(diligent.Height == 480.0f);
  REQUIRE(diligent.MinDepth == 0.25f);
  REQUIRE(diligent.MaxDepth == 0.75f);
}

TEST_CASE("frame_context defaults to invalid", "[render-core][frame-context]") {
  const frame_context ctx;
  REQUIRE_FALSE(ctx.is_valid());
  REQUIRE_FALSE(ctx.has_device());
  REQUIRE(ctx.device_context() == nullptr);
  REQUIRE(ctx.gpu_data() == nullptr);
  REQUIRE(ctx.shaders() == nullptr);
  REQUIRE(ctx.color_target() == nullptr);
  REQUIRE(ctx.depth_target() == nullptr);
  REQUIRE(ctx.depth_read_only() == nullptr);
  REQUIRE_FALSE(ctx.has_depth_read_only());
  REQUIRE(ctx.camera_srb() == nullptr);
  REQUIRE(ctx.frame_index() == 0);
  REQUIRE(ctx.delta_time() == 0.0);
  REQUIRE(ctx.alpha() == 0.0);
  REQUIRE(ctx.frame_budget().is_unlimited());
  REQUIRE(ctx.frame_budget().has_remaining());
}

TEST_CASE("frame_context exposes its frame state", "[render-core][frame-context]") {
  viewport vp;
  vp.width = 800.0f;
  vp.height = 600.0f;
  frame_context ctx{
    nullptr,
    fake_context(1),
    fake_view(2),
    fake_view(3),
    fake_view(4),
    vp,
    7,
    0.016,
    0.5,
    reinterpret_cast<Diligent::IShaderResourceBinding*>(5),
  };

  REQUIRE(ctx.device_context() == fake_context(1));
  REQUIRE_FALSE(ctx.has_device());
  REQUIRE(ctx.gpu_data() == nullptr);
  REQUIRE(ctx.shaders() == nullptr);
  REQUIRE(ctx.color_target() == fake_view(2));
  REQUIRE(ctx.depth_target() == fake_view(3));
  REQUIRE(ctx.depth_read_only() == fake_view(4));
  REQUIRE(ctx.has_depth_read_only());
  REQUIRE(ctx.camera_srb() == reinterpret_cast<Diligent::IShaderResourceBinding*>(5));
  REQUIRE(ctx.frame_index() == 7);
  REQUIRE(ctx.delta_time() == 0.016);
  REQUIRE(ctx.alpha() == 0.5);
  REQUIRE(ctx.frame_budget().is_unlimited());
  REQUIRE(ctx.is_valid());
}

TEST_CASE("frame_context exposes a finite frame budget", "[render-core][frame-context]") {
  manual_clock clock;
  viewport vp;
  vp.width = 1.0f;
  vp.height = 1.0f;
  frame_context ctx{
    nullptr, fake_context(1), fake_view(2), fake_view(3), nullptr, vp, 0, 0.0, 0.0, nullptr, frame_budget{ 1.0, clock },
  };

  REQUIRE_FALSE(ctx.frame_budget().is_unlimited());
  REQUIRE(ctx.frame_budget().has_remaining());
  REQUIRE(ctx.frame_budget().remaining_seconds() == 1.0);
  clock.advance(0.25);
  REQUIRE(ctx.frame_budget().remaining_seconds() == 0.75);
}

TEST_CASE("frame_context falls back to the writable depth view", "[render-core][frame-context]") {
  viewport vp;
  vp.width = 1.0f;
  vp.height = 1.0f;
  frame_context ctx{
    nullptr, fake_context(1), fake_view(2), fake_view(3), nullptr, vp, 0, 0.0, 0.0,
  };

  REQUIRE_FALSE(ctx.has_depth_read_only());
  REQUIRE(ctx.depth_read_only() == ctx.depth_target());
}

TEST_CASE("frame_context set_render_targets replaces all views", "[render-core][frame-context]") {
  frame_context ctx;
  ctx.set_render_targets(fake_view(10), fake_view(11), fake_view(12));
  REQUIRE(ctx.color_target() == fake_view(10));
  REQUIRE(ctx.depth_target() == fake_view(11));
  REQUIRE(ctx.depth_read_only() == fake_view(12));

  ctx.set_render_targets(nullptr, fake_view(20));
  REQUIRE(ctx.color_target() == nullptr);
  REQUIRE(ctx.depth_target() == fake_view(20));
  REQUIRE(ctx.depth_read_only() == fake_view(20));
}

TEST_CASE("frame_context can be rebuilt for each frame", "[render-core][frame-context]") {
  viewport vp;
  vp.width = 320.0f;
  vp.height = 240.0f;
  frame_context ctx;
  ctx.set_device_context(fake_context(1));
  ctx.set_viewport(vp);
  ctx.set_render_targets(fake_view(2), fake_view(3));
  ctx.set_frame_info(1, 0.016, 0.25);

  REQUIRE(ctx.is_valid());
  REQUIRE(ctx.current_viewport() == vp);
  REQUIRE(ctx.frame_index() == 1);

  vp.width = 640.0f;
  ctx.set_viewport(vp);
  REQUIRE(ctx.current_viewport().width == 640.0f);
}
