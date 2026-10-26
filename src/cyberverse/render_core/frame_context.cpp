/**
 * @file frame_context.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame context implementation
 * @version 0.1
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/frame_context.hpp"

#include <exception>

#include "cyberverse/render_core/render_device.hpp"

namespace cyberverse {
namespace render_core {

render_device& frame_context::device() const noexcept {
  if (device_ == nullptr) {
    std::terminate();
  }
  return *device_;
}

gpu_data_manager* frame_context::gpu_data() const noexcept {
  return device_ != nullptr ? &device_->gpu_data() : nullptr;
}

shader_manager* frame_context::shaders() const noexcept { return device_ != nullptr ? &device_->shaders() : nullptr; }

} // namespace render_core
} // namespace cyberverse
