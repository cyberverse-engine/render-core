/**
 * @file detail/gpu_data_backend.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief DiligentEngine resource backend implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/detail/gpu_data_backend.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Fence.h>
#include <GPUUploadManager.h>
#include <GraphicsAccessories.hpp>

#include "cyberverse/render_core/gpu_data_manager.hpp"

namespace cyberverse {
namespace render_core {
namespace detail {

namespace {

std::uint64_t texture_row_pitch(const texture_submit_desc& desc) noexcept {
  if (desc.row_pitch != 0) {
    return desc.row_pitch;
  }

  const Diligent::TextureFormatAttribs& attribs = Diligent::GetTextureFormatAttribs(desc.format);
  const std::uint64_t block_width = attribs.BlockWidth != 0 ? attribs.BlockWidth : 1;
  const std::uint64_t block_size = attribs.GetElementSize();
  return ((static_cast<std::uint64_t>(desc.width) + block_width - 1) / block_width) * block_size;
}

} // namespace

struct diligent_gpu_data_backend::pending_upload {
  Diligent::ITexture* texture = nullptr;
  Diligent::IBuffer* buffer = nullptr;
  gpu_data_upload_state state = gpu_data_upload_state::pending;
  std::string error;
  Diligent::RefCntAutoPtr<Diligent::ITexture> staging_texture;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
};

struct diligent_gpu_data_backend::deferred_upload {
  Diligent::RefCntAutoPtr<Diligent::ITexture> staging_texture;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
};

struct diligent_gpu_data_backend::upload_store {
  std::unordered_map<std::uint64_t, pending_upload> uploads;
  std::vector<deferred_upload> deferred;
  std::uint64_t next_id = 1;
};

diligent_gpu_data_backend::diligent_gpu_data_backend(Diligent::IRenderDevice* device,
                                                     Diligent::IDeviceContext* context,
                                                     Diligent::IGPUUploadManager* upload_manager)
    : device_{ device }
    , context_{ context }
    , upload_manager_{ upload_manager }
    , uploads_{ std::make_unique<upload_store>() } {
  if (device_ == nullptr) {
    throw std::invalid_argument("diligent_gpu_data_backend requires a non-null render device");
  }
}

diligent_gpu_data_backend::~diligent_gpu_data_backend() = default;

Diligent::ITexture* diligent_gpu_data_backend::create_texture(const texture_submit_desc& desc) {
  Diligent::TextureDesc texture_desc;
  texture_desc.Name = desc.debug_name.empty() ? nullptr : desc.debug_name.c_str();
  texture_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  texture_desc.Width = desc.width;
  texture_desc.Height = desc.height;
  texture_desc.ArraySize = desc.array_size;
  texture_desc.Format = desc.format;
  texture_desc.MipLevels = desc.mip_levels;
  texture_desc.BindFlags = desc.bind_flags;
  texture_desc.Usage = desc.usage;

  Diligent::TextureData initial_data;
  Diligent::TextureSubResData subresource;
  // Diligent requires one entry per subresource. v0.1 supports an initial
  // upload for a single-mip, single-slice texture; larger textures are created
  // empty and filled through the future upload path.
  if (desc.data != nullptr && desc.mip_levels == 1 && desc.array_size == 1) {
    subresource.pData = desc.data;
    subresource.Stride = texture_row_pitch(desc);
    initial_data.pSubResources = &subresource;
    initial_data.NumSubresources = 1;
  }

  Diligent::ITexture* texture = nullptr;
  const Diligent::TextureData* data = initial_data.NumSubresources != 0 ? &initial_data : nullptr;
  device_->CreateTexture(texture_desc, data, &texture);

  if (texture != nullptr && desc.target_state != Diligent::RESOURCE_STATE_UNKNOWN) {
    texture->SetState(desc.target_state);
  }
  return texture;
}

Diligent::IBuffer* diligent_gpu_data_backend::create_buffer(const buffer_submit_desc& desc) {
  Diligent::BufferDesc buffer_desc;
  buffer_desc.Name = desc.debug_name.empty() ? nullptr : desc.debug_name.c_str();
  buffer_desc.Size = desc.size;
  buffer_desc.BindFlags = desc.bind_flags;
  buffer_desc.Usage = desc.usage;
  buffer_desc.Mode = desc.mode;
  buffer_desc.ElementByteStride = desc.element_byte_stride;

  Diligent::BufferData initial_data;
  const Diligent::BufferData* data = nullptr;
  if (desc.data != nullptr) {
    initial_data.pData = desc.data;
    initial_data.DataSize = desc.size;
    data = &initial_data;
  }

  Diligent::IBuffer* buffer = nullptr;
  device_->CreateBuffer(buffer_desc, data, &buffer);
  return buffer;
}

void diligent_gpu_data_backend::destroy_texture(Diligent::ITexture* texture) noexcept {
  if (texture != nullptr) {
    texture->Release();
  }
}

void diligent_gpu_data_backend::destroy_buffer(Diligent::IBuffer* buffer) noexcept {
  if (buffer != nullptr) {
    buffer->Release();
  }
}

gpu_data_upload_ticket diligent_gpu_data_backend::request_texture_upload(const texture_submit_desc& desc,
                                                                         std::string& error) {
  Diligent::ITexture* texture = nullptr;
  Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
  Diligent::RefCntAutoPtr<Diligent::IFence> fence;
  try {
    texture_submit_desc upload_desc = desc;
    if (context_ != nullptr && desc.data != nullptr) {
      upload_desc.usage = Diligent::USAGE_DEFAULT;
      upload_desc.data = nullptr;
    }
    texture = create_texture(upload_desc);
    if (texture == nullptr) {
      error = "texture creation failed";
      return {};
    }

    if (context_ != nullptr && desc.data != nullptr) {
      Diligent::TextureDesc staging_desc;
      staging_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
      staging_desc.Width = desc.width;
      staging_desc.Height = desc.height;
      staging_desc.Format = desc.format;
      staging_desc.MipLevels = 1;
      staging_desc.ArraySize = 1;
      staging_desc.Usage = Diligent::USAGE_STAGING;
      staging_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
      staging_desc.BindFlags = Diligent::BIND_NONE;
      Diligent::ITexture* raw_staging = nullptr;
      device_->CreateTexture(staging_desc, nullptr, &raw_staging);
      if (raw_staging == nullptr) {
        destroy_texture(texture);
        error = "staging texture creation failed";
        return {};
      }
      staging.Attach(raw_staging);

      Diligent::MappedTextureSubresource mapped;
      context_->MapTextureSubresource(staging, 0, 0, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, nullptr, mapped);
      if (mapped.pData == nullptr) {
        destroy_texture(texture);
        error = "staging texture map failed";
        return {};
      }
      const std::uint64_t source_pitch = texture_row_pitch(desc);
      auto* destination = static_cast<std::byte*>(mapped.pData);
      const auto* source = static_cast<const std::byte*>(desc.data);
      const std::uint64_t row_bytes = source_pitch;
      for (std::uint32_t row = 0; row < desc.height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * mapped.Stride,
                    source + static_cast<std::size_t>(row) * row_bytes,
                    static_cast<std::size_t>(row_bytes));
      }
      context_->UnmapTextureSubresource(staging, 0, 0);

      Diligent::CopyTextureAttribs copy;
      copy.pSrcTexture = staging;
      copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      copy.pDstTexture = texture;
      copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
      context_->CopyTexture(copy);

      Diligent::FenceDesc fence_desc;
      fence_desc.Type = Diligent::FENCE_TYPE_GENERAL;
      Diligent::IFence* raw_fence = nullptr;
      device_->CreateFence(fence_desc, &raw_fence);
      if (raw_fence == nullptr) {
        destroy_texture(texture);
        error = "upload fence creation failed";
        return {};
      }
      fence.Attach(raw_fence);
      context_->EnqueueSignal(fence, 1);
      context_->Flush();
    }
  } catch (const std::exception& exception) {
    if (texture != nullptr) {
      destroy_texture(texture);
    }
    error = exception.what();
    return {};
  } catch (...) {
    if (texture != nullptr) {
      destroy_texture(texture);
    }
    error = "texture creation threw an unknown exception";
    return {};
  }
  if (texture == nullptr) {
    error = "texture creation failed";
    return {};
  }

  const std::uint64_t id = uploads_->next_id++;
  pending_upload upload;
  upload.texture = texture;
  upload.state = fence != nullptr ? gpu_data_upload_state::pending : gpu_data_upload_state::ready;
  upload.staging_texture = std::move(staging);
  upload.fence = std::move(fence);
  uploads_->uploads.emplace(id, std::move(upload));
  return gpu_data_upload_ticket{ id };
}

gpu_data_upload_ticket diligent_gpu_data_backend::request_buffer_upload(const buffer_submit_desc& desc,
                                                                        std::string& error) {
  Diligent::IBuffer* buffer = nullptr;
  try {
    if (upload_manager_ != nullptr && context_ != nullptr && desc.data != nullptr) {
      buffer_submit_desc upload_desc = desc;
      upload_desc.usage = Diligent::USAGE_DEFAULT;
      upload_desc.data = nullptr;
      buffer = create_buffer(upload_desc);

      if (buffer != nullptr) {
        Diligent::ScheduleBufferUpdateInfo update;
        update.pContext = context_;
        update.pDstBuffer = buffer;
        update.DstOffset = 0;
        update.NumBytes = static_cast<Diligent::Uint32>(desc.size);
        update.pSrcData = desc.data;
        upload_manager_->ScheduleBufferUpdate(update);
        upload_manager_->RenderThreadUpdate(context_);
      }
    } else {
      buffer = create_buffer(desc);
    }
  } catch (const std::exception& exception) {
    error = exception.what();
    return {};
  } catch (...) {
    error = "buffer creation threw an unknown exception";
    return {};
  }
  if (buffer == nullptr) {
    error = "buffer creation failed";
    return {};
  }

  const std::uint64_t id = uploads_->next_id++;
  uploads_->uploads.emplace(id, pending_upload{ nullptr, buffer, gpu_data_upload_state::ready, {} });
  return gpu_data_upload_ticket{ id };
}

gpu_data_upload_state diligent_gpu_data_backend::poll_texture_upload(gpu_data_upload_ticket ticket,
                                                                     Diligent::ITexture*& texture,
                                                                     std::string& error) {
  texture = nullptr;
  if (!ticket.is_valid()) {
    error = "invalid texture upload ticket";
    return gpu_data_upload_state::failed;
  }
  const auto found = uploads_->uploads.find(ticket.id);
  if (found == uploads_->uploads.end()) {
    error = "texture upload ticket was not found";
    return gpu_data_upload_state::failed;
  }
  if (found->second.state == gpu_data_upload_state::failed) {
    error = found->second.error;
    uploads_->uploads.erase(found);
    return gpu_data_upload_state::failed;
  }
  if (found->second.fence != nullptr) {
    uploads_->deferred.push_back(
        deferred_upload{ std::move(found->second.staging_texture), std::move(found->second.fence) });
  }
  texture = found->second.texture;
  found->second.texture = nullptr;
  uploads_->uploads.erase(found);
  return gpu_data_upload_state::ready;
}

gpu_data_upload_state diligent_gpu_data_backend::poll_buffer_upload(gpu_data_upload_ticket ticket,
                                                                    Diligent::IBuffer*& buffer,
                                                                    std::string& error) {
  buffer = nullptr;
  if (!ticket.is_valid()) {
    error = "invalid buffer upload ticket";
    return gpu_data_upload_state::failed;
  }
  const auto found = uploads_->uploads.find(ticket.id);
  if (found == uploads_->uploads.end()) {
    error = "buffer upload ticket was not found";
    return gpu_data_upload_state::failed;
  }
  if (found->second.state == gpu_data_upload_state::failed) {
    error = found->second.error;
    uploads_->uploads.erase(found);
    return gpu_data_upload_state::failed;
  }
  buffer = found->second.buffer;
  found->second.buffer = nullptr;
  uploads_->uploads.erase(found);
  return gpu_data_upload_state::ready;
}

void diligent_gpu_data_backend::cancel_texture_upload(gpu_data_upload_ticket ticket) noexcept {
  if (!ticket.is_valid()) {
    return;
  }
  const auto found = uploads_->uploads.find(ticket.id);
  if (found == uploads_->uploads.end()) {
    return;
  }
  if (found->second.texture != nullptr) {
    destroy_texture(found->second.texture);
  }
  uploads_->uploads.erase(found);
}

void diligent_gpu_data_backend::cancel_buffer_upload(gpu_data_upload_ticket ticket) noexcept {
  if (!ticket.is_valid()) {
    return;
  }
  const auto found = uploads_->uploads.find(ticket.id);
  if (found == uploads_->uploads.end()) {
    return;
  }
  if (found->second.buffer != nullptr) {
    destroy_buffer(found->second.buffer);
  }
  uploads_->uploads.erase(found);
}

void diligent_gpu_data_backend::tick() noexcept {
  auto& deferred = uploads_->deferred;
  deferred.erase(std::remove_if(deferred.begin(),
                                deferred.end(),
                                [](const deferred_upload& upload) {
                                  return upload.fence == nullptr || upload.fence->GetCompletedValue() >= 1;
                                }),
                 deferred.end());
}

std::uint64_t diligent_gpu_data_backend::memory_capacity_bytes() const noexcept {
  const Diligent::AdapterMemoryInfo& memory = device_->GetAdapterInfo().Memory;
  return memory.LocalMemory != 0 ? memory.LocalMemory : memory.UnifiedMemory;
}

} // namespace detail
} // namespace render_core
} // namespace cyberverse
