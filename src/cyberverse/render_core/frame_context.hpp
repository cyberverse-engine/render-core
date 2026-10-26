/**
 * @file frame_context.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame context
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>

#include <RenderDevice.h>

#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/shader_manager.hpp"
#include "cyberverse/runtime/frame_budget.hpp"

namespace cyberverse {
namespace render_core {

class render_device;

/**
 * @brief Viewport description in framebuffer pixels
 *
 */
struct viewport {
  float top_left_x = 0.0f;
  float top_left_y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float min_depth = 0.0f;
  float max_depth = 1.0f;

  /**
   * @brief Whether the viewport can be bound
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_valid() const noexcept {
    return width > 0.0f && height > 0.0f && min_depth >= 0.0f && max_depth >= min_depth && max_depth <= 1.0f;
  }

  /**
   * @brief Convert to a Diligent viewport
   *
   * @return Diligent::Viewport
   */
  [[nodiscard]] Diligent::Viewport to_diligent() const noexcept {
    Diligent::Viewport result;
    result.TopLeftX = top_left_x;
    result.TopLeftY = top_left_y;
    result.Width = width;
    result.Height = height;
    result.MinDepth = min_depth;
    result.MaxDepth = max_depth;
    return result;
  }

  friend bool operator==(const viewport&, const viewport&) noexcept = default;
};

/**
 * @brief Per-frame render state shared by render phrases
 *
 * A frame_context is a non-owning snapshot created for one frame. It points at
 * the render device and its immediate context, but owns neither the device nor
 * any GPU object. The render device must outlive the frame context. It is valid
 * only for that frame; render targets and the camera SRB are re-bound on the
 * next frame.
 *
 * A default-constructed or detached frame context has no device. Check
 * has_device() before calling device().
 *
 */
class frame_context {
public:
  frame_context() = default;

  /**
   * @brief Construct a frame context
   *
   * @param device Render device that owns this frame's context and managers
   * @param device_context Immediate device context
   * @param color_target Color render target view
   * @param depth_target Writable depth-stencil view
   * @param depth_read_only Optional read-only depth view for the transparent phrase
   * @param vp Viewport
   * @param frame_index Frame index
   * @param delta_time Frame delta in seconds
   * @param alpha Fixed-step interpolation alpha
   * @param camera_srb Optional camera shader resource binding
   * @param budget Frame time budget; defaults to unlimited
   */
  frame_context(render_device* device,
                Diligent::IDeviceContext* device_context,
                Diligent::ITextureView* color_target,
                Diligent::ITextureView* depth_target,
                Diligent::ITextureView* depth_read_only,
                viewport vp,
                std::uint64_t frame_index,
                double delta_time,
                double alpha,
                Diligent::IShaderResourceBinding* camera_srb = nullptr,
                runtime::frame_budget budget = runtime::frame_budget::unlimited()) noexcept
      : device_{ device }
      , device_context_{ device_context }
      , color_target_{ color_target }
      , depth_target_{ depth_target }
      , depth_read_only_{ depth_read_only }
      , viewport_{ vp }
      , camera_srb_{ camera_srb }
      , frame_index_{ frame_index }
      , delta_time_{ delta_time }
      , alpha_{ alpha }
      , budget_{ budget } {}

  /**
   * @brief Immediate device context
   *
   * @return Diligent::IDeviceContext*
   */
  [[nodiscard]] Diligent::IDeviceContext* device_context() const noexcept { return device_context_; }

  /**
   * @brief Set the immediate device context
   *
   * @param context Immediate device context
   */
  void set_device_context(Diligent::IDeviceContext* context) noexcept { device_context_ = context; }

  /**
   * @brief Render device that owns this frame's context and managers
   *
   * Must only be called when has_device() is true.
   *
   * @return render_device&
   */
  [[nodiscard]] render_device& device() const noexcept;

  /**
   * @brief Whether this frame context is attached to a render device
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool has_device() const noexcept { return device_ != nullptr; }

  /**
   * @brief Application-level GPU data manager
   *
   * Returns null when this is a detached frame context.
   *
   * @return gpu_data_manager*
   */
  [[nodiscard]] gpu_data_manager* gpu_data() const noexcept;

  /**
   * @brief Application-level shader manager
   *
   * Returns null when this is a detached frame context.
   *
   * @return shader_manager*
   */
  [[nodiscard]] shader_manager* shaders() const noexcept;

  /**
   * @brief Color render target
   *
   * @return Diligent::ITextureView*
   */
  [[nodiscard]] Diligent::ITextureView* color_target() const noexcept { return color_target_; }

  /**
   * @brief Writable depth-stencil target
   *
   * @return Diligent::ITextureView*
   */
  [[nodiscard]] Diligent::ITextureView* depth_target() const noexcept { return depth_target_; }

  /**
   * @brief Read-only depth target used by the transparent phrase
   *
   * Falls back to depth_target() when the renderer did not create a read-only
   * view.
   *
   * @return Diligent::ITextureView*
   */
  [[nodiscard]] Diligent::ITextureView* depth_read_only() const noexcept {
    return depth_read_only_ != nullptr ? depth_read_only_ : depth_target_;
  }

  /**
   * @brief Whether a read-only depth view was provided
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool has_depth_read_only() const noexcept { return depth_read_only_ != nullptr; }

  /**
   * @brief Bind render targets
   *
   * @param color_target Color target
   * @param depth_target Writable depth target
   * @param depth_read_only Optional read-only depth target
   */
  void set_render_targets(Diligent::ITextureView* color_target,
                          Diligent::ITextureView* depth_target,
                          Diligent::ITextureView* depth_read_only = nullptr) noexcept {
    color_target_ = color_target;
    depth_target_ = depth_target;
    depth_read_only_ = depth_read_only;
  }

  /**
   * @brief Viewport
   *
   * @return const viewport&
   */
  [[nodiscard]] const viewport& current_viewport() const noexcept { return viewport_; }

  /**
   * @brief Set the viewport
   *
   * @param vp Viewport
   */
  void set_viewport(const viewport& vp) noexcept { viewport_ = vp; }

  /**
   * @brief Camera shader resource binding
   *
   * @return Diligent::IShaderResourceBinding*
   */
  [[nodiscard]] Diligent::IShaderResourceBinding* camera_srb() const noexcept { return camera_srb_; }

  /**
   * @brief Set the camera shader resource binding
   *
   * @param srb Camera SRB
   */
  void set_camera_srb(Diligent::IShaderResourceBinding* srb) noexcept { camera_srb_ = srb; }

  /**
   * @brief Frame index
   *
   * @return std::uint64_t
   */
  [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

  /**
   * @brief Frame delta time in seconds
   *
   * @return double
   */
  [[nodiscard]] double delta_time() const noexcept { return delta_time_; }

  /**
   * @brief Frame time budget
   *
   * Defaults to an unlimited budget. A finite budget is bound to the clock
   * supplied when it was constructed; that clock must outlive this frame
   * context.
   *
   * @return const runtime::frame_budget&
   */
  [[nodiscard]] const runtime::frame_budget& frame_budget() const noexcept { return budget_; }

  /**
   * @brief Fixed-step interpolation alpha
   *
   * @return double
   */
  [[nodiscard]] double alpha() const noexcept { return alpha_; }

  /**
   * @brief Replace the frame information
   *
   * @param frame_index Frame index
   * @param delta_time Frame delta in seconds
   * @param alpha Fixed-step interpolation alpha
   */
  void set_frame_info(std::uint64_t frame_index, double delta_time, double alpha) noexcept {
    frame_index_ = frame_index;
    delta_time_ = delta_time;
    alpha_ = alpha;
  }

  /**
   * @brief Whether the frame context can be used
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_valid() const noexcept {
    return device_context_ != nullptr && viewport_.is_valid() && (color_target_ != nullptr || depth_target_ != nullptr);
  }

private:
  render_device* device_ = nullptr;
  Diligent::IDeviceContext* device_context_ = nullptr;

  Diligent::ITextureView* color_target_ = nullptr;
  Diligent::ITextureView* depth_target_ = nullptr;
  Diligent::ITextureView* depth_read_only_ = nullptr;

  viewport viewport_{};

  Diligent::IShaderResourceBinding* camera_srb_ = nullptr;

  std::uint64_t frame_index_ = 0;
  double delta_time_ = 0.0;
  double alpha_ = 0.0;

  runtime::frame_budget budget_ = runtime::frame_budget::unlimited();
};

} // namespace render_core
} // namespace cyberverse
