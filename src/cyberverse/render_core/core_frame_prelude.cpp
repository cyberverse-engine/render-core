/**
 * @file core_frame_prelude.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief render-core frame-start service prelude implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/core_frame_prelude.hpp"

#include "cyberverse/render_core/render_device.hpp"

namespace cyberverse {
namespace render_core {

core_frame_prelude::core_frame_prelude(render_device& device) noexcept : device_{ device } {}

void core_frame_prelude::on_frame_start(double frame_delta, const runtime::frame_budget& budget) {
  (void)frame_delta;
  device_.gpu_data().tick(budget);
  device_.pipelines().tick(budget);
}

} // namespace render_core
} // namespace cyberverse
