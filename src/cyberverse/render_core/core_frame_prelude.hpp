/**
 * @file core_frame_prelude.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief render-core frame-start service prelude
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "cyberverse/runtime/frame_prelude.hpp"

namespace cyberverse {
namespace render_core {

class render_device;

/**
 * @brief Drives render-core services at frame start
 *
 * Register one core_frame_prelude with the application's frame_pipeline. It
 * ticks the GPU data manager and pipeline manager using the frame budget passed
 * for the current frame. It does not cache the budget or frame delta, create a
 * frame context, or present.
 *
 */
class core_frame_prelude final : public runtime::frame_prelude {
public:
  /**
   * @brief Construct a prelude for a render device
   *
   * @param device Render device; must outlive the prelude
   */
  explicit core_frame_prelude(render_device& device) noexcept;

  /**
   * @brief Tick render-core services for this frame
   *
   * @param frame_delta Current frame delta; unused by the core services
   * @param budget Current frame budget
   */
  void on_frame_start(double frame_delta, const runtime::frame_budget& budget) override;

private:
  render_device& device_;
};

} // namespace render_core
} // namespace cyberverse
