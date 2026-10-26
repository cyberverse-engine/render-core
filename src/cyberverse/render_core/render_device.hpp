/**
 * @file render_device.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief DiligentEngine device and swap chain
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <GPUUploadManager.h>
#include <RefCntAutoPtr.hpp>

#include "cyberverse/platform/window.hpp"
#include "cyberverse/render_core/frame_context.hpp"
#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/pipeline_manager.hpp"
#include "cyberverse/render_core/shader_manager.hpp"

// Backends compiled into this build. CMake defines these for the DiligentEngine
// backend targets that are available on the platform; consumers that include
// the headers without the CMake target fall back to no compiled backend.
#ifndef CYBERVERSE_RENDER_CORE_BACKEND_VULKAN
#define CYBERVERSE_RENDER_CORE_BACKEND_VULKAN 0
#endif

#ifndef CYBERVERSE_RENDER_CORE_BACKEND_D3D12
#define CYBERVERSE_RENDER_CORE_BACKEND_D3D12 0
#endif

#ifndef CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER
#define CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER 0
#endif

namespace cyberverse {
namespace render_core {

/**
 * @brief Graphics backend used by render_device
 *
 */
enum class render_backend {
  vulkan, ///< Vulkan (MoltenVK on macOS)
  d3d12,  ///< Direct3D 12 (Windows only)
};

/**
 * @brief Human-readable backend name
 *
 * @param backend Backend
 * @return std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(render_backend backend) noexcept {
  switch (backend) {
  case render_backend::d3d12:
    return "d3d12";
  case render_backend::vulkan:
  default:
    return "vulkan";
  }
}

/**
 * @brief Whether a backend was compiled into this build
 *
 * D3D12 is never available outside Windows. A platform may also be built
 * without Vulkan, for example when the Vulkan SDK is absent on Windows.
 *
 * @param backend Backend
 * @return true
 * @return false
 */
[[nodiscard]] constexpr bool is_backend_available(render_backend backend) noexcept {
  switch (backend) {
  case render_backend::d3d12:
    return CYBERVERSE_RENDER_CORE_BACKEND_D3D12 != 0;
  case render_backend::vulkan:
  default:
    return CYBERVERSE_RENDER_CORE_BACKEND_VULKAN != 0;
  }
}

/**
 * @brief Device and swap chain creation description
 *
 * width and height describe the swap chain, not the platform window. A HiDPI
 * caller should pass the framebuffer size from platform::window::width() /
 * height() so the renderer matches the backing store.
 *
 * The backend defaults to Direct3D 12 on Windows builds that include it,
 * otherwise Vulkan. Use is_backend_available() before constructing the device
 * when the application needs a specific backend.
 *
 */
struct render_device_desc {
  platform::native_window_handle window{}; ///< Native window from platform

#if defined(_WIN32) && CYBERVERSE_RENDER_CORE_BACKEND_D3D12
  render_backend backend = render_backend::d3d12; ///< Backend, D3D12 preferred on Windows
#elif CYBERVERSE_RENDER_CORE_BACKEND_VULKAN
  render_backend backend = render_backend::vulkan; ///< Backend
#else
  render_backend backend = render_backend::d3d12; ///< Backend
#endif
  std::uint32_t width = 1280;     ///< Swap chain width in pixels
  std::uint32_t height = 720;     ///< Swap chain height in pixels
  std::uint32_t buffer_count = 3; ///< Swap chain buffer count
  Diligent::TEXTURE_FORMAT color_format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
  Diligent::TEXTURE_FORMAT depth_format = Diligent::TEX_FORMAT_D32_FLOAT;
  bool vsync = true;                                       ///< Present sync interval
  bool enable_validation = false;                          ///< Enable backend validation
  bool create_swap_chain = true;                           ///< Create a swap chain for the window
  std::uint32_t adapter_id = Diligent::DEFAULT_ADAPTER_ID; ///< Adapter selection
  std::string debug_name = "Cyberverse Render Core";       ///< Application/debug name
};

/**
 * @brief Validate a device description
 *
 * @param desc Description
 * @throws std::invalid_argument If the window handle or swap chain description is unusable
 */
void validate_render_device_desc(const render_device_desc& desc);

/**
 * @brief DiligentEngine render device, immediate context, and swap chain
 *
 * render_device owns the Diligent device, the immediate context, the swap
 * chain, the GPU data manager, and the shader manager. It is created from a
 * platform native window handle; the caller keeps the platform window alive for
 * the device lifetime.
 *
 * The class is not thread-safe. All methods must be called from the render
 * thread.
 *
 */
class render_device final {
public:
  /**
   * @brief Create a render device
   *
   * @param desc Device description
   * @param drsn_loader Optional DRSN loader used by the owned pipeline manager.
   *        It must outlive the render device. Pass null when DRSN is not used.
   * @return std::unique_ptr<render_device>
   * @throws std::invalid_argument If desc is invalid
   * @throws std::runtime_error If Diligent cannot create the device or swap chain
   */
  [[nodiscard]] static std::unique_ptr<render_device>
  create(const render_device_desc& desc, Diligent::IRenderStateNotationLoader* drsn_loader = nullptr);

  ~render_device();

  render_device(const render_device&) = delete;
  render_device(render_device&&) = delete;

  render_device& operator=(const render_device&) = delete;
  render_device& operator=(render_device&&) = delete;

  [[nodiscard]] Diligent::IRenderDevice* device() const noexcept { return device_; }

  [[nodiscard]] Diligent::IDeviceContext* immediate_context() const noexcept { return context_; }

  [[nodiscard]] Diligent::ISwapChain* swap_chain() const noexcept { return swap_chain_; }

  /**
   * @brief Application-level GPU data manager
   *
   * @return gpu_data_manager&
   */
  [[nodiscard]] gpu_data_manager& gpu_data() noexcept { return *gpu_data_; }

  /**
   * @brief Application-level GPU data manager
   *
   * @return const gpu_data_manager&
   */
  [[nodiscard]] const gpu_data_manager& gpu_data() const noexcept { return *gpu_data_; }

  /**
   * @brief Application-level shader manager
   *
   * @return shader_manager&
   */
  [[nodiscard]] shader_manager& shaders() noexcept { return *shaders_; }

  /**
   * @brief Application-level shader manager
   *
   * @return const shader_manager&
   */
  [[nodiscard]] const shader_manager& shaders() const noexcept { return *shaders_; }

  /**
   * @brief Application-level pipeline manager
   *
   * @return pipeline_manager&
   */
  [[nodiscard]] pipeline_manager& pipelines() noexcept { return *pipelines_; }

  /**
   * @brief Application-level pipeline manager
   *
   * @return const pipeline_manager&
   */
  [[nodiscard]] const pipeline_manager& pipelines() const noexcept { return *pipelines_; }

  [[nodiscard]] const render_device_desc& desc() const noexcept { return desc_; }

  [[nodiscard]] std::string_view backend_name() const noexcept { return to_string(desc_.backend); }

  /**
   * @brief Current swap chain width in pixels
   *
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t width() const noexcept { return desc_.width; }

  /**
   * @brief Current swap chain height in pixels
   *
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t height() const noexcept { return desc_.height; }

  /**
   * @brief Number of frames presented since creation
   *
   * @return std::uint64_t
   */
  [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

  /**
   * @brief Start a frame and return its context
   *
   * Binds the current back buffer and depth buffer. Multiple stages may call
   * begin_frame in the same presented frame; the frame index only advances in
   * present(). The returned context is valid until the next begin_frame(),
   * resize(), or present().
   *
   * @param delta_time Frame delta in seconds
   * @param alpha Fixed-step interpolation alpha
   * @param budget Frame time budget; defaults to unlimited
   * @return frame_context
   */
  frame_context
  begin_frame(double delta_time, double alpha = 0.0, runtime::frame_budget budget = runtime::frame_budget::unlimited());

  /**
   * @brief Present the current back buffer
   *
   * @param vsync Optional override of render_device_desc::vsync
   */
  void present(bool vsync);

  /**
   * @brief Present with the configured vsync setting
   *
   */
  void present() { present(desc_.vsync); }

  /**
   * @brief Resize the swap chain
   *
   * Zero sizes are ignored. The new size is visible in width() / height() and
   * in the next begin_frame() viewport.
   *
   * @param width New width in pixels
   * @param height New height in pixels
   */
  void resize(std::uint32_t width, std::uint32_t height);

  /**
   * @brief Wait until the GPU is idle
   *
   */
  void wait_idle();

  /**
   * @brief Release cached pipeline and shader resources
   *
   * Waits for the GPU to become idle, releases all pipeline-manager SRB refs,
   * and clears the shader cache. GPU data is reference-counted; callers must
   * drop their texture_data_ref / buffer_data_ref objects to release it.
   *
   */
  void release_all_resources();

private:
  explicit render_device(const render_device_desc& desc, Diligent::IRenderStateNotationLoader* drsn_loader = nullptr);

  void initialize_vulkan();
  void initialize_d3d12();

  render_device_desc desc_;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> swap_chain_;
  Diligent::RefCntAutoPtr<Diligent::IGPUUploadManager> upload_manager_;
  std::unique_ptr<gpu_data_manager> gpu_data_;
  std::unique_ptr<shader_manager> shaders_;
  std::unique_ptr<pipeline_manager> pipelines_;
  std::uint64_t frame_index_ = 0;
};

} // namespace render_core
} // namespace cyberverse
