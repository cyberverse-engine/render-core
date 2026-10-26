/**
 * @file fake_gpu_data_backend.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Deterministic GPU data backend for tests
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cyberverse/render_core/detail/gpu_data_backend.hpp"
#include "cyberverse/render_core/gpu_data_manager.hpp"

namespace cyberverse::render_core::test {

using detail::gpu_data_backend;
using detail::gpu_data_upload_state;
using detail::gpu_data_upload_ticket;

class fake_gpu_data_backend final : public gpu_data_backend {
public:
  Diligent::ITexture* create_texture(const texture_submit_desc& desc) override {
    ++texture_creates;
    last_texture = desc;
    if (throw_textures) {
      throw std::runtime_error{ "fake texture failure" };
    }
    if (fail_textures) {
      return nullptr;
    }
    auto* texture = reinterpret_cast<Diligent::ITexture*>(next_id++ * 64u);
    created_textures.push_back(texture);
    return texture;
  }

  Diligent::IBuffer* create_buffer(const buffer_submit_desc& desc) override {
    ++buffer_creates;
    last_buffer = desc;
    if (throw_buffers) {
      throw std::runtime_error{ "fake buffer failure" };
    }
    if (fail_buffers) {
      return nullptr;
    }
    auto* buffer = reinterpret_cast<Diligent::IBuffer*>(next_id++ * 64u);
    created_buffers.push_back(buffer);
    created_buffer_names.push_back(desc.debug_name);
    return buffer;
  }

  void destroy_texture(Diligent::ITexture* texture) noexcept override {
    ++texture_destroys;
    destroyed_textures.push_back(texture);
  }

  void destroy_buffer(Diligent::IBuffer* buffer) noexcept override {
    ++buffer_destroys;
    destroyed_buffers.push_back(buffer);
  }

  gpu_data_upload_ticket request_texture_upload(const texture_submit_desc& desc, std::string& error) override {
    ++texture_upload_requests;
    if (throw_textures) {
      error = "fake texture upload failure";
      return {};
    }
    if (fail_textures) {
      error = "fake texture upload failure";
      return {};
    }
    Diligent::ITexture* texture = create_texture(desc);
    if (texture == nullptr) {
      error = "fake texture creation failed";
      return {};
    }
    const std::uint64_t id = next_ticket_++;
    uploads.emplace(id, upload{ texture, nullptr, true });
    return gpu_data_upload_ticket{ id };
  }

  gpu_data_upload_ticket request_buffer_upload(const buffer_submit_desc& desc, std::string& error) override {
    ++buffer_upload_requests;
    if (throw_buffers) {
      error = "fake buffer upload failure";
      return {};
    }
    if (fail_buffers) {
      error = "fake buffer upload failure";
      return {};
    }
    Diligent::IBuffer* buffer = create_buffer(desc);
    if (buffer == nullptr) {
      error = "fake buffer creation failed";
      return {};
    }
    const std::uint64_t id = next_ticket_++;
    uploads.emplace(id, upload{ nullptr, buffer, false });
    return gpu_data_upload_ticket{ id };
  }

  gpu_data_upload_state
  poll_texture_upload(gpu_data_upload_ticket ticket, Diligent::ITexture*& texture, std::string& error) override {
    texture = nullptr;
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || !found->second.is_texture) {
      error = "fake texture upload ticket was not found";
      return gpu_data_upload_state::failed;
    }
    texture = found->second.texture;
    uploads.erase(found);
    return gpu_data_upload_state::ready;
  }

  gpu_data_upload_state
  poll_buffer_upload(gpu_data_upload_ticket ticket, Diligent::IBuffer*& buffer, std::string& error) override {
    buffer = nullptr;
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || found->second.is_texture) {
      error = "fake buffer upload ticket was not found";
      return gpu_data_upload_state::failed;
    }
    buffer = found->second.buffer;
    uploads.erase(found);
    return gpu_data_upload_state::ready;
  }

  void cancel_texture_upload(gpu_data_upload_ticket ticket) noexcept override {
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || !found->second.is_texture) {
      return;
    }
    if (found->second.texture != nullptr) {
      destroy_texture(found->second.texture);
    }
    uploads.erase(found);
  }

  void cancel_buffer_upload(gpu_data_upload_ticket ticket) noexcept override {
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || found->second.is_texture) {
      return;
    }
    if (found->second.buffer != nullptr) {
      destroy_buffer(found->second.buffer);
    }
    uploads.erase(found);
  }

  std::uint64_t memory_capacity_bytes() const noexcept override { return memory_capacity; }

  struct upload {
    Diligent::ITexture* texture = nullptr;
    Diligent::IBuffer* buffer = nullptr;
    bool is_texture = false;
  };

  std::uintptr_t next_id = 1;
  std::uint64_t next_ticket_ = 1;
  std::uint64_t memory_capacity = 0;
  std::unordered_map<std::uint64_t, upload> uploads;
  int texture_upload_requests = 0;
  int buffer_upload_requests = 0;
  int texture_creates = 0;
  int buffer_creates = 0;
  int texture_destroys = 0;
  int buffer_destroys = 0;
  bool fail_textures = false;
  bool fail_buffers = false;
  bool throw_textures = false;
  bool throw_buffers = false;
  std::optional<texture_submit_desc> last_texture;
  std::optional<buffer_submit_desc> last_buffer;
  std::vector<Diligent::ITexture*> created_textures;
  std::vector<Diligent::IBuffer*> created_buffers;
  std::vector<std::string> created_buffer_names;
  std::vector<Diligent::ITexture*> destroyed_textures;
  std::vector<Diligent::IBuffer*> destroyed_buffers;
};

inline texture_submit_desc make_texture_desc() {
  static const std::uint8_t pixels[4 * 4 * 4] = {};
  texture_submit_desc desc;
  desc.data = pixels;
  desc.width = 4;
  desc.height = 4;
  desc.format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  desc.debug_name = "test texture";
  return desc;
}

inline buffer_submit_desc make_buffer_desc() {
  static const std::uint32_t vertices[16] = {};
  buffer_submit_desc desc;
  desc.data = vertices;
  desc.size = sizeof(vertices);
  desc.debug_name = "test buffer";
  return desc;
}

} // namespace cyberverse::render_core::test
