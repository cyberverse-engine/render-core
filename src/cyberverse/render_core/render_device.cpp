/**
 * @file render_device.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief DiligentEngine device and swap chain implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/render_device.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#if CYBERVERSE_RENDER_CORE_BACKEND_VULKAN
#include <EngineFactoryVk.h>
#endif

#if CYBERVERSE_RENDER_CORE_BACKEND_D3D12 && defined(_WIN32)
#include <EngineFactoryD3D12.h>
#endif

namespace cyberverse {
namespace render_core {

namespace {

#if defined(_WIN32)
using native_window_type = Diligent::Win32NativeWindow;
#elif defined(__APPLE__)
using native_window_type = Diligent::MacOSNativeWindow;
#elif defined(__linux__)
using native_window_type = Diligent::LinuxNativeWindow;
#else
using native_window_type = Diligent::NativeWindow;
#endif

native_window_type make_native_window(const platform::native_window_handle& handle) {
#if defined(_WIN32)
  if (handle.window == nullptr) {
    throw std::invalid_argument("render_device: Win32 window handle is null");
  }
  return native_window_type{ handle.window };
#elif defined(__APPLE__)
  if (handle.view == nullptr) {
    throw std::invalid_argument("render_device: macOS NSView handle is null");
  }
  return native_window_type{ handle.view };
#elif defined(__linux__)
  native_window_type window;
  if (handle.surface != nullptr) {
    if (handle.display == nullptr) {
      throw std::invalid_argument("render_device: Wayland surface requires a display handle");
    }
    window.pDisplay = handle.display;
    window.pWaylandSurface = handle.surface;
    return window;
  }
  if (handle.window == nullptr) {
    throw std::invalid_argument("render_device: X11 window handle is null");
  }
  window.WindowId = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle.window));
  window.pDisplay = handle.display;
  return window;
#else
  (void)handle;
  throw std::invalid_argument("render_device: unsupported platform native window");
#endif
}

#if defined(_WIN32)
constexpr bool has_native_window(const platform::native_window_handle& handle) noexcept {
  return handle.window != nullptr;
}
#elif defined(__APPLE__)
constexpr bool has_native_window(const platform::native_window_handle& handle) noexcept {
  return handle.view != nullptr;
}
#elif defined(__linux__)
constexpr bool has_native_window(const platform::native_window_handle& handle) noexcept {
  if (handle.surface != nullptr) {
    return handle.display != nullptr;
  }
  return handle.window != nullptr && handle.display != nullptr;
}
#else
constexpr bool has_native_window(const platform::native_window_handle& handle) noexcept {
  return handle.window != nullptr || handle.view != nullptr || handle.surface != nullptr;
}
#endif

Diligent::SwapChainDesc make_swap_chain_desc(const render_device_desc& desc) noexcept {
  Diligent::SwapChainDesc swap_chain_desc;
  swap_chain_desc.Width = desc.width;
  swap_chain_desc.Height = desc.height;
  swap_chain_desc.ColorBufferFormat = desc.color_format;
  swap_chain_desc.DepthBufferFormat = desc.depth_format;
  swap_chain_desc.BufferCount = desc.buffer_count;
  return swap_chain_desc;
}

void attach_or_throw(Diligent::RefCntAutoPtr<Diligent::IRenderDevice>& device,
                     Diligent::RefCntAutoPtr<Diligent::IDeviceContext>& context,
                     Diligent::IRenderDevice* raw_device,
                     Diligent::IDeviceContext* raw_context,
                     const char* backend) {
  if (raw_device == nullptr || raw_context == nullptr) {
    if (raw_device != nullptr) {
      raw_device->Release();
    }
    if (raw_context != nullptr) {
      raw_context->Release();
    }
    throw std::runtime_error(std::string{ "render_device: DiligentEngine could not create the " } + backend +
                             " device");
  }

  device.Attach(raw_device);
  context.Attach(raw_context);
}

} // namespace

void validate_render_device_desc(const render_device_desc& desc) {
  if (!is_backend_available(desc.backend)) {
    throw std::invalid_argument(std::string{ "render_device: the " } + std::string{ to_string(desc.backend) } +
                                " backend is not available in this build");
  }
  if (desc.window.backend == platform::backend_type::headless) {
    throw std::invalid_argument("render_device: a headless platform window has no native handle");
  }
  if (!has_native_window(desc.window)) {
    throw std::invalid_argument("render_device: the platform native window handle is empty");
  }
  if (desc.width == 0 || desc.height == 0) {
    throw std::invalid_argument("render_device: swap chain width and height must be non-zero");
  }
  if (desc.buffer_count < 2) {
    throw std::invalid_argument("render_device: the swap chain needs at least two buffers");
  }
  if (desc.color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    throw std::invalid_argument("render_device: swap chain color format must be specified");
  }
}

std::unique_ptr<render_device> render_device::create(const render_device_desc& desc,
                                                     Diligent::IRenderStateNotationLoader* drsn_loader) {
  return std::unique_ptr<render_device>{ new render_device{ desc, drsn_loader } };
}

render_device::render_device(const render_device_desc& desc, Diligent::IRenderStateNotationLoader* drsn_loader)
    : desc_{ desc } {
  validate_render_device_desc(desc_);

  switch (desc_.backend) {
  case render_backend::d3d12:
    initialize_d3d12();
    break;
  case render_backend::vulkan:
  default:
    initialize_vulkan();
    break;
  }

#if CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER
  if (context_ != nullptr) {
    Diligent::GPUUploadManagerCreateInfo upload_info;
    upload_info.pDevice = device_;
    upload_info.pContext = context_;
    Diligent::IGPUUploadManager* upload_manager = nullptr;
    Diligent::CreateGPUUploadManager(upload_info, &upload_manager);
    if (upload_manager != nullptr) {
      upload_manager_.Attach(upload_manager);
    }
  }
#endif
  gpu_data_ = std::make_unique<gpu_data_manager>(device_, context_, upload_manager_);
  shaders_ = std::make_unique<shader_manager>(device_);
  pipelines_ = std::make_unique<pipeline_manager>(device_, *gpu_data_, *shaders_, drsn_loader);
}

void render_device::initialize_vulkan() {
#if CYBERVERSE_RENDER_CORE_BACKEND_VULKAN
  Diligent::IEngineFactoryVk* factory = Diligent::LoadAndGetEngineFactoryVk();
  if (factory == nullptr) {
    throw std::runtime_error("render_device: DiligentEngine Vulkan factory is unavailable");
  }

  Diligent::EngineVkCreateInfo create_info;
  create_info.AdapterId = desc_.adapter_id;
  create_info.EnableValidation = desc_.enable_validation;

  Diligent::IRenderDevice* raw_device = nullptr;
  Diligent::IDeviceContext* raw_context = nullptr;
  factory->CreateDeviceAndContextsVk(create_info, &raw_device, &raw_context);
  attach_or_throw(device_, context_, raw_device, raw_context, "Vulkan");

  if (desc_.create_swap_chain) {
    const Diligent::SwapChainDesc swap_chain_desc = make_swap_chain_desc(desc_);
    const native_window_type window = make_native_window(desc_.window);

    Diligent::ISwapChain* raw_swap_chain = nullptr;
    factory->CreateSwapChainVk(device_, context_, swap_chain_desc, window, &raw_swap_chain);
    if (raw_swap_chain == nullptr) {
      throw std::runtime_error("render_device: DiligentEngine could not create the Vulkan swap chain");
    }
    swap_chain_.Attach(raw_swap_chain);
  }
#else
  throw std::runtime_error("render_device: the Vulkan backend is not available in this build");
#endif
}

void render_device::initialize_d3d12() {
#if CYBERVERSE_RENDER_CORE_BACKEND_D3D12 && defined(_WIN32)
  Diligent::IEngineFactoryD3D12* factory = Diligent::LoadAndGetEngineFactoryD3D12();
  if (factory == nullptr) {
    throw std::runtime_error("render_device: DiligentEngine Direct3D 12 factory is unavailable");
  }

  Diligent::EngineD3D12CreateInfo create_info;
  create_info.AdapterId = desc_.adapter_id;
  create_info.EnableValidation = desc_.enable_validation;

  Diligent::IRenderDevice* raw_device = nullptr;
  Diligent::IDeviceContext* raw_context = nullptr;
  factory->CreateDeviceAndContextsD3D12(create_info, &raw_device, &raw_context);
  attach_or_throw(device_, context_, raw_device, raw_context, "Direct3D 12");

  if (desc_.create_swap_chain) {
    const Diligent::SwapChainDesc swap_chain_desc = make_swap_chain_desc(desc_);
    const native_window_type window = make_native_window(desc_.window);
    const Diligent::FullScreenModeDesc full_screen_mode{};

    Diligent::ISwapChain* raw_swap_chain = nullptr;
    factory->CreateSwapChainD3D12(device_, context_, swap_chain_desc, full_screen_mode, window, &raw_swap_chain);
    if (raw_swap_chain == nullptr) {
      throw std::runtime_error("render_device: DiligentEngine could not create the Direct3D 12 swap chain");
    }
    swap_chain_.Attach(raw_swap_chain);
  }
#else
  throw std::runtime_error("render_device: the Direct3D 12 backend is only available on Windows builds");
#endif
}

render_device::~render_device() {
  if (device_ != nullptr) {
    device_->IdleGPU();
  }
}

frame_context render_device::begin_frame(double delta_time, double alpha, runtime::frame_budget budget) {
  Diligent::ITextureView* color_target = nullptr;
  Diligent::ITextureView* depth_target = nullptr;
  if (swap_chain_ != nullptr) {
    color_target = swap_chain_->GetCurrentBackBufferRTV();
    depth_target = swap_chain_->GetDepthBufferDSV();
  }

  viewport vp;
  vp.width = static_cast<float>(desc_.width);
  vp.height = static_cast<float>(desc_.height);

  frame_context ctx{
    this, context_, color_target, depth_target, nullptr, vp, frame_index_, delta_time, alpha, nullptr, budget,
  };
  return ctx;
}

void render_device::present(bool vsync) {
  if (swap_chain_ != nullptr) {
    swap_chain_->Present(vsync ? 1u : 0u);
  }
  ++frame_index_;
}

void render_device::resize(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0) {
    return;
  }
  desc_.width = width;
  desc_.height = height;
  if (swap_chain_ != nullptr) {
    swap_chain_->Resize(width, height);
  }
}

void render_device::wait_idle() {
  if (device_ != nullptr) {
    device_->IdleGPU();
  }
}

void render_device::release_all_resources() {
  wait_idle();
  pipelines_->release_all();
  shaders_->clear();
}

} // namespace render_core
} // namespace cyberverse
