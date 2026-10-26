/**
 * @file gpu_data_manager.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief GPU data manager implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/gpu_data_manager.hpp"

#include <cassert>
#include <utility>

#include "cyberverse/render_core/detail/gpu_data_backend.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <utility>

#include <GraphicsAccessories.hpp>

#include "cyberverse/runtime/this_thread.hpp"

namespace cyberverse {
namespace render_core {

namespace {

constexpr std::size_t max_versions_per_entry = 5;

bool is_texture_desc_valid(const texture_submit_desc& desc) noexcept {
  return desc.width > 0 && desc.height > 0 && desc.mip_levels > 0 && desc.array_size > 0 &&
         desc.format != Diligent::TEX_FORMAT_UNKNOWN;
}

bool is_buffer_desc_valid(const buffer_submit_desc& desc) noexcept { return desc.size > 0; }

std::uint64_t texture_row_pitch(const texture_submit_desc& desc) noexcept {
  if (desc.row_pitch != 0) {
    return desc.row_pitch;
  }
  const Diligent::TextureFormatAttribs& attribs = Diligent::GetTextureFormatAttribs(desc.format);
  const std::uint64_t block_width = attribs.BlockWidth != 0 ? attribs.BlockWidth : 1;
  const std::uint64_t block_size = attribs.GetElementSize();
  return ((static_cast<std::uint64_t>(desc.width) + block_width - 1) / block_width) * block_size;
}

std::unique_ptr<std::byte[]> copy_texture_upload_data(const texture_submit_desc& desc, std::uint64_t& size) {
  size = 0;
  if (desc.data == nullptr || desc.mip_levels != 1 || desc.array_size != 1) {
    return {};
  }
  const Diligent::TextureFormatAttribs& attribs = Diligent::GetTextureFormatAttribs(desc.format);
  const std::uint64_t block_height = attribs.BlockHeight != 0 ? attribs.BlockHeight : 1;
  const std::uint64_t rows = (static_cast<std::uint64_t>(desc.height) + block_height - 1) / block_height;
  const std::uint64_t pitch = texture_row_pitch(desc);
  size = pitch * rows;
  if (size == 0) {
    return {};
  }
  auto data = std::make_unique<std::byte[]>(static_cast<std::size_t>(size));
  std::memcpy(data.get(), desc.data, static_cast<std::size_t>(size));
  return data;
}

std::unique_ptr<std::byte[]> copy_buffer_upload_data(const buffer_submit_desc& desc, std::uint64_t& size) {
  size = 0;
  if (desc.data == nullptr || desc.size == 0) {
    return {};
  }
  auto data = std::make_unique<std::byte[]>(static_cast<std::size_t>(desc.size));
  std::memcpy(data.get(), desc.data, static_cast<std::size_t>(desc.size));
  size = desc.size;
  return data;
}

std::uint64_t texture_resource_size(const texture_submit_desc& desc) noexcept {
  const Diligent::TextureFormatAttribs& attribs = Diligent::GetTextureFormatAttribs(desc.format);
  const std::uint64_t block_width = attribs.BlockWidth != 0 ? attribs.BlockWidth : 1;
  const std::uint64_t block_height = attribs.BlockHeight != 0 ? attribs.BlockHeight : 1;
  const std::uint64_t block_size = attribs.GetElementSize();
  std::uint64_t total = 0;
  for (std::uint32_t mip = 0; mip < desc.mip_levels; ++mip) {
    const std::uint64_t width = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(desc.width) >> mip);
    const std::uint64_t height = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(desc.height) >> mip);
    const std::uint64_t blocks_x = (width + block_width - 1) / block_width;
    const std::uint64_t blocks_y = (height + block_height - 1) / block_height;
    total += blocks_x * blocks_y * block_size;
  }
  return total * static_cast<std::uint64_t>(desc.array_size);
}

} // namespace

gpu_data_manager::gpu_data_manager(std::unique_ptr<detail::gpu_data_backend> backend) noexcept
    : owned_backend_{ std::move(backend) } {
  assert(owned_backend_ != nullptr);
  const std::uint64_t capacity = owned_backend_->memory_capacity_bytes();
  memory_budget_bytes_ = capacity > 0 ? (capacity / 5) * 4 : 0;
}

gpu_data_manager::gpu_data_manager(Diligent::IRenderDevice* device,
                                   Diligent::IDeviceContext* context,
                                   Diligent::IGPUUploadManager* upload_manager)
    : owned_backend_{ std::make_unique<detail::diligent_gpu_data_backend>(device, context, upload_manager) } {
  const std::uint64_t capacity = owned_backend_->memory_capacity_bytes();
  memory_budget_bytes_ = capacity > 0 ? (capacity / 5) * 4 : 0;
}

gpu_data_manager::~gpu_data_manager() {
  for (auto& [uri, value] : entries_) {
    (void)uri;
    if (value != nullptr) {
      release_entry(*value);
    }
  }
}

void gpu_data_manager::assert_render_thread() const noexcept { assert(runtime::is_this_render_thread()); }

gpu_data_manager::entry* gpu_data_manager::entry_at(const resource_uri& uri) noexcept {
  const auto found = entries_.find(uri);
  return found != entries_.end() ? found->second.get() : nullptr;
}

const gpu_data_manager::entry* gpu_data_manager::entry_at(const resource_uri& uri) const noexcept {
  const auto found = entries_.find(uri);
  return found != entries_.end() ? found->second.get() : nullptr;
}

texture_data_ref gpu_data_manager::make_texture_ref(const resource_uri& uri) noexcept {
  entry* value = entry_at(uri);
  if (value == nullptr || value->type != gpu_data_type::texture) {
    return {};
  }
  value->ref_count.fetch_add(1, std::memory_order_acq_rel);
  return texture_data_ref{ this, value, gpu_data_type::texture };
}

buffer_data_ref gpu_data_manager::make_buffer_ref(const resource_uri& uri) noexcept {
  entry* value = entry_at(uri);
  if (value == nullptr || value->type != gpu_data_type::buffer) {
    return {};
  }
  value->ref_count.fetch_add(1, std::memory_order_acq_rel);
  return buffer_data_ref{ this, value, gpu_data_type::buffer };
}

void gpu_data_manager::retain_ref(void* pointer) noexcept {
  auto* value = static_cast<entry*>(pointer);
  if (value != nullptr) {
    value->ref_count.fetch_add(1, std::memory_order_acq_rel);
  }
}

void gpu_data_manager::release_ref(void* pointer) noexcept {
  auto* value = static_cast<entry*>(pointer);
  if (value == nullptr) {
    return;
  }
  const std::uint32_t previous = value->ref_count.fetch_sub(1, std::memory_order_acq_rel);
  if (previous == 1) {
    value->release_requested.store(true, std::memory_order_release);
  }
}

gpu_data_state gpu_data_manager::entry_state(void* pointer) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  return value != nullptr ? value->state : gpu_data_state::failed;
}

std::string_view gpu_data_manager::entry_error(void* pointer) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  return value != nullptr ? std::string_view{ value->error } : std::string_view{};
}

submit_version gpu_data_manager::entry_current_version(void* pointer) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  return value != nullptr ? value->current_version : 0;
}

std::optional<gpu_data_state> gpu_data_manager::entry_version_state(void* pointer,
                                                                    submit_version version) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  if (value == nullptr) {
    return std::nullopt;
  }
  for (const auto& candidate : value->versions) {
    if (candidate->version == version) {
      return candidate->state;
    }
  }
  return std::nullopt;
}

std::string_view gpu_data_manager::entry_version_error(void* pointer, submit_version version) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  if (value == nullptr) {
    return {};
  }
  for (const auto& candidate : value->versions) {
    if (candidate->version == version) {
      return candidate->error;
    }
  }
  return {};
}

Diligent::ITexture* gpu_data_manager::current_texture(void* pointer) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  if (value == nullptr || value->type != gpu_data_type::texture || value->current == nullptr ||
      value->current->state != gpu_data_state::ready) {
    return nullptr;
  }
  return static_cast<Diligent::ITexture*>(value->current->resource);
}

Diligent::IBuffer* gpu_data_manager::current_buffer(void* pointer) const noexcept {
  const auto* value = static_cast<const entry*>(pointer);
  if (value == nullptr || value->type != gpu_data_type::buffer || value->current == nullptr ||
      value->current->state != gpu_data_state::ready) {
    return nullptr;
  }
  return static_cast<Diligent::IBuffer*>(value->current->resource);
}

void gpu_data_manager::destroy_version(version& value) noexcept {
  value.upload_data.reset();
  value.upload_data_size = 0;
  value.texture_desc.data = nullptr;
  value.buffer_desc.data = nullptr;
  if (value.resource == nullptr) {
    return;
  }
  if (value.type == gpu_data_type::texture) {
    owned_backend_->destroy_texture(static_cast<Diligent::ITexture*>(value.resource));
  } else {
    owned_backend_->destroy_buffer(static_cast<Diligent::IBuffer*>(value.resource));
  }
  value.resource = nullptr;
  if (allocated_bytes_ >= value.size_bytes) {
    allocated_bytes_ -= value.size_bytes;
  } else {
    allocated_bytes_ = 0;
  }
  value.size_bytes = 0;
  ++resources_destroyed_;
}

void gpu_data_manager::release_entry(entry& value) noexcept {
  if (value.active_upload != nullptr && value.active_upload->upload_ticket != 0) {
    if (value.type == gpu_data_type::texture) {
      owned_backend_->cancel_texture_upload(detail::gpu_data_upload_ticket{ value.active_upload->upload_ticket });
    } else {
      owned_backend_->cancel_buffer_upload(detail::gpu_data_upload_ticket{ value.active_upload->upload_ticket });
    }
    value.active_upload->upload_ticket = 0;
  }
  for (auto& candidate : value.versions) {
    if (candidate->upload_ticket != 0) {
      if (value.type == gpu_data_type::texture) {
        owned_backend_->cancel_texture_upload(detail::gpu_data_upload_ticket{ candidate->upload_ticket });
      } else {
        owned_backend_->cancel_buffer_upload(detail::gpu_data_upload_ticket{ candidate->upload_ticket });
      }
      candidate->upload_ticket = 0;
    }
    destroy_version(*candidate);
  }
  value.versions.clear();
  value.current = nullptr;
  value.active_upload = nullptr;
}

void gpu_data_manager::erase_entry(const resource_uri& uri) noexcept {
  const auto found = entries_.find(uri);
  if (found == entries_.end()) {
    return;
  }
  release_entry(*found->second);
  entries_.erase(found);
}

void gpu_data_manager::update_entry_state(entry& value) noexcept {
  if (value.release_requested.load(std::memory_order_acquire) && value.ref_count.load(std::memory_order_acquire) == 0) {
    value.state = gpu_data_state::releasing;
    return;
  }
  if (value.current != nullptr && value.current->state == gpu_data_state::ready) {
    value.state = gpu_data_state::ready;
    value.error = value.current->error;
    return;
  }
  if (value.active_upload != nullptr) {
    value.state = value.active_upload->state;
    value.error = value.active_upload->error;
    return;
  }
  value.state = gpu_data_state::pending;
  value.error.clear();
  for (auto it = value.versions.rbegin(); it != value.versions.rend(); ++it) {
    if ((*it)->state == gpu_data_state::failed || (*it)->state == gpu_data_state::canceled) {
      value.state = (*it)->state;
      value.error = (*it)->error;
      return;
    }
    if ((*it)->state == gpu_data_state::pending || (*it)->state == gpu_data_state::uploading) {
      value.state = (*it)->state;
      value.error.clear();
      return;
    }
  }
}

void gpu_data_manager::cancel_superseded_upload(entry& value) noexcept {
  for (auto& candidate : value.versions) {
    if (candidate.get() == value.active_upload) {
      continue;
    }
    if (candidate->state != gpu_data_state::pending && candidate->state != gpu_data_state::uploading) {
      continue;
    }
    if (candidate->upload_ticket != 0) {
      if (value.type == gpu_data_type::texture) {
        owned_backend_->cancel_texture_upload(detail::gpu_data_upload_ticket{ candidate->upload_ticket });
      } else {
        owned_backend_->cancel_buffer_upload(detail::gpu_data_upload_ticket{ candidate->upload_ticket });
      }
      candidate->upload_ticket = 0;
    }
    destroy_version(*candidate);
    candidate->state = gpu_data_state::canceled;
    candidate->error = "superseded by a later submission";
    ++versions_canceled_;
  }
}

void gpu_data_manager::prune_versions(entry& value) noexcept {
  while (value.versions.size() > max_versions_per_entry) {
    auto victim = value.versions.end();
    for (auto it = value.versions.begin(); it != value.versions.end(); ++it) {
      if (it->get() != value.current && it->get() != value.active_upload) {
        victim = it;
        break;
      }
    }
    if (victim == value.versions.end()) {
      return;
    }
    if ((*victim)->upload_ticket != 0) {
      if (value.type == gpu_data_type::texture) {
        owned_backend_->cancel_texture_upload(detail::gpu_data_upload_ticket{ (*victim)->upload_ticket });
      } else {
        owned_backend_->cancel_buffer_upload(detail::gpu_data_upload_ticket{ (*victim)->upload_ticket });
      }
    }
    destroy_version(**victim);
    value.versions.erase(victim);
    ++versions_pruned_;
  }
}

bool gpu_data_manager::has_budget(const runtime::frame_budget& budget) const noexcept { return budget.has_remaining(); }

bool gpu_data_manager::make_room(std::uint64_t required_bytes) noexcept {
  if (memory_budget_bytes_ == 0) {
    return true;
  }
  if (required_bytes > memory_budget_bytes_) {
    return false;
  }
  while (allocated_bytes_ + required_bytes > memory_budget_bytes_) {
    auto victim = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      entry* candidate = it->second.get();
      if (candidate->ref_count.load(std::memory_order_acquire) != 0 ||
          !candidate->release_requested.load(std::memory_order_acquire)) {
        continue;
      }
      if (victim == entries_.end() || candidate->last_access < victim->second->last_access) {
        victim = it;
      }
    }
    if (victim == entries_.end()) {
      return false;
    }
    release_entry(*victim->second);
    entries_.erase(victim);
  }
  return true;
}

std::expected<std::tuple<texture_data_ref, submit_version>, std::error_code>
gpu_data_manager::submit_texture(const resource_uri& uri, texture_submit_desc desc, bool overwrite) noexcept {
  assert_render_thread();
  if (!uri.is_valid() || uri.type() != resource_type::texture || !is_texture_desc_valid(desc)) {
    return std::unexpected(make_error_code(render_core_errc::invalid_argument));
  }

  auto existing = entries_.find(uri);
  if (existing != entries_.end() && !overwrite) {
    return std::unexpected(make_error_code(render_core_errc::already_exists));
  }

  std::unique_ptr<std::byte[]> upload_data;
  std::uint64_t upload_data_size = 0;
  try {
    upload_data = copy_texture_upload_data(desc, upload_data_size);
  } catch (...) {
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }

  if (existing == entries_.end()) {
    try {
      auto value = std::make_unique<entry>();
      value->type = gpu_data_type::texture;
      value->uri = uri;
      existing = entries_.emplace(uri, std::move(value)).first;
    } catch (...) {
      return std::unexpected(make_error_code(render_core_errc::out_of_memory));
    }
  }

  entry& value = *existing->second;
  if (value.ref_count.load(std::memory_order_acquire) == 0) {
    value.release_requested.store(false, std::memory_order_release);
    value.release_tick = 0;
  }

  std::unique_ptr<version> version_value;
  try {
    version_value = std::make_unique<version>();
  } catch (...) {
    if (value.versions.empty() && value.current == nullptr) {
      erase_entry(uri);
    }
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }
  version_value->version = next_submit_version_.fetch_add(1, std::memory_order_relaxed);
  version_value->type = gpu_data_type::texture;
  version_value->state = gpu_data_state::pending;
  version_value->upload_data = std::move(upload_data);
  version_value->upload_data_size = upload_data_size;
  version_value->last_access = version_value->version;
  try {
    value.versions.push_back(std::move(version_value));
  } catch (...) {
    if (value.versions.empty() && value.current == nullptr) {
      erase_entry(uri);
    }
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }

  version* added = value.versions.back().get();
  added->priority = desc.upload_priority;
  added->texture_desc = std::move(desc);
  added->texture_desc.data = added->upload_data.get();
  value.active_upload = added;
  ++textures_submitted_;
  ++versions_submitted_;
  value.last_access = added->version;
  update_entry_state(value);

  texture_data_ref ref = make_texture_ref(uri);
  return std::make_tuple(std::move(ref), added->version);
}

std::expected<std::tuple<buffer_data_ref, submit_version>, std::error_code>
gpu_data_manager::submit_buffer(const resource_uri& uri, buffer_submit_desc desc, bool overwrite) noexcept {
  assert_render_thread();
  if (!uri.is_valid() || uri.type() != resource_type::buffer || !is_buffer_desc_valid(desc)) {
    return std::unexpected(make_error_code(render_core_errc::invalid_argument));
  }

  auto existing = entries_.find(uri);
  if (existing != entries_.end() && !overwrite) {
    return std::unexpected(make_error_code(render_core_errc::already_exists));
  }

  std::unique_ptr<std::byte[]> upload_data;
  std::uint64_t upload_data_size = 0;
  try {
    upload_data = copy_buffer_upload_data(desc, upload_data_size);
  } catch (...) {
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }

  if (existing == entries_.end()) {
    try {
      auto value = std::make_unique<entry>();
      value->type = gpu_data_type::buffer;
      value->uri = uri;
      existing = entries_.emplace(uri, std::move(value)).first;
    } catch (...) {
      return std::unexpected(make_error_code(render_core_errc::out_of_memory));
    }
  }

  entry& value = *existing->second;
  if (value.ref_count.load(std::memory_order_acquire) == 0) {
    value.release_requested.store(false, std::memory_order_release);
    value.release_tick = 0;
  }

  std::unique_ptr<version> version_value;
  try {
    version_value = std::make_unique<version>();
  } catch (...) {
    if (value.versions.empty() && value.current == nullptr) {
      erase_entry(uri);
    }
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }
  version_value->version = next_submit_version_.fetch_add(1, std::memory_order_relaxed);
  version_value->type = gpu_data_type::buffer;
  version_value->state = gpu_data_state::pending;
  version_value->upload_data = std::move(upload_data);
  version_value->upload_data_size = upload_data_size;
  version_value->last_access = version_value->version;
  try {
    value.versions.push_back(std::move(version_value));
  } catch (...) {
    if (value.versions.empty() && value.current == nullptr) {
      erase_entry(uri);
    }
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }

  version* added = value.versions.back().get();
  added->priority = desc.upload_priority;
  added->buffer_desc = std::move(desc);
  added->buffer_desc.data = added->upload_data.get();
  value.active_upload = added;
  ++buffers_submitted_;
  ++versions_submitted_;
  value.last_access = added->version;
  update_entry_state(value);

  buffer_data_ref ref = make_buffer_ref(uri);
  return std::make_tuple(std::move(ref), added->version);
}

texture_data_ref gpu_data_manager::get_texture(const resource_uri& uri) noexcept {
  assert_render_thread();
  entry* value = entry_at(uri);
  if (value == nullptr || value->type != gpu_data_type::texture ||
      value->release_requested.load(std::memory_order_acquire) ||
      value->ref_count.load(std::memory_order_acquire) == 0) {
    return {};
  }
  return make_texture_ref(uri);
}

buffer_data_ref gpu_data_manager::get_buffer(const resource_uri& uri) noexcept {
  assert_render_thread();
  entry* value = entry_at(uri);
  if (value == nullptr || value->type != gpu_data_type::buffer ||
      value->release_requested.load(std::memory_order_acquire) ||
      value->ref_count.load(std::memory_order_acquire) == 0) {
    return {};
  }
  return make_buffer_ref(uri);
}

gpu_data_state gpu_data_manager::get_state(const resource_uri& uri) const noexcept {
  assert_render_thread();
  const entry* value = entry_at(uri);
  if (value == nullptr) {
    return gpu_data_state::failed;
  }
  if (value->release_requested.load(std::memory_order_acquire) &&
      value->ref_count.load(std::memory_order_acquire) == 0) {
    return gpu_data_state::releasing;
  }
  return value->state;
}

std::uint64_t gpu_data_manager::memory_capacity_bytes() const noexcept {
  return owned_backend_->memory_capacity_bytes();
}

void gpu_data_manager::tick(const runtime::frame_budget& budget) {
  assert_render_thread();
  ++tick_count_;
  uploaded_bytes_last_tick_ = 0;
  owned_backend_->tick();

  std::vector<resource_uri> release_candidates;
  release_candidates.reserve(entries_.size());
  for (const auto& [uri, value] : entries_) {
    if (value->release_requested.load(std::memory_order_acquire) &&
        value->ref_count.load(std::memory_order_acquire) == 0) {
      release_candidates.push_back(uri);
    }
  }
  std::sort(
      release_candidates.begin(), release_candidates.end(), [&](const resource_uri& lhs, const resource_uri& rhs) {
        return entry_at(lhs)->last_access < entry_at(rhs)->last_access;
      });
  for (const resource_uri& uri : release_candidates) {
    entry* value = entry_at(uri);
    if (value == nullptr) {
      continue;
    }
    if (!has_budget(budget)) {
      continue;
    }
    if (value->release_tick == 0) {
      value->release_tick = tick_count_ > 0 ? tick_count_ - 1 : 0;
    }
    const bool release = release_policy_ == gpu_data_release_policy::immediate ||
                         (tick_count_ - value->release_tick >= retire_delay_ticks_ &&
                          (memory_budget_bytes_ == 0 || allocated_bytes_ > memory_budget_bytes_));
    if (release) {
      erase_entry(uri);
    }
  }

  std::vector<entry*> upload_order;
  upload_order.reserve(entries_.size());
  for (auto& [uri, value] : entries_) {
    (void)uri;
    upload_order.push_back(value.get());
  }
  std::sort(upload_order.begin(), upload_order.end(), [](const entry* lhs, const entry* rhs) {
    const gpu_data_upload_priority lhs_priority =
        lhs->active_upload != nullptr ? lhs->active_upload->priority : gpu_data_upload_priority::low;
    const gpu_data_upload_priority rhs_priority =
        rhs->active_upload != nullptr ? rhs->active_upload->priority : gpu_data_upload_priority::low;
    if (lhs_priority != rhs_priority) {
      return static_cast<std::uint8_t>(lhs_priority) > static_cast<std::uint8_t>(rhs_priority);
    }
    return lhs->last_access < rhs->last_access;
  });

  for (entry* value : upload_order) {
    auto it = entries_.find(value->uri);
    if (it == entries_.end()) {
      continue;
    }
    if (value->release_requested.load(std::memory_order_acquire) &&
        value->ref_count.load(std::memory_order_acquire) == 0) {
      continue;
    }
    if (!has_budget(budget)) {
      continue;
    }
    cancel_superseded_upload(*value);
    prune_versions(*value);

    if (value->active_upload != nullptr) {
      version& upload = *value->active_upload;
      if (upload.state == gpu_data_state::uploading) {
        void* resource = nullptr;
        std::string error;
        detail::gpu_data_upload_state state = detail::gpu_data_upload_state::failed;
        if (value->type == gpu_data_type::texture) {
          Diligent::ITexture* texture = nullptr;
          state = owned_backend_->poll_texture_upload(
              detail::gpu_data_upload_ticket{ upload.upload_ticket }, texture, error);
          resource = texture;
        } else {
          Diligent::IBuffer* buffer = nullptr;
          state =
              owned_backend_->poll_buffer_upload(detail::gpu_data_upload_ticket{ upload.upload_ticket }, buffer, error);
          resource = buffer;
        }
        upload.upload_ticket = 0;
        upload.upload_data.reset();
        upload.upload_data_size = 0;
        if (state == detail::gpu_data_upload_state::ready && resource != nullptr) {
          upload.resource = resource;
          upload.state = gpu_data_state::ready;
          upload.error.clear();
          upload.size_bytes = value->type == gpu_data_type::texture ? texture_resource_size(upload.texture_desc)
                                                                    : upload.buffer_desc.size;
          allocated_bytes_ += upload.size_bytes;
          ++versions_ready_;
          if (upload.version > value->current_version) {
            value->current = &upload;
            value->current_version = upload.version;
          }
          value->active_upload = nullptr;
        } else {
          upload.state = gpu_data_state::failed;
          upload.error = error.empty() ? "upload failed" : std::move(error);
          upload.upload_data.reset();
          upload.upload_data_size = 0;
          ++versions_failed_;
          value->active_upload = nullptr;
        }
      } else if (upload.state == gpu_data_state::pending) {
        const std::uint64_t required = value->type == gpu_data_type::texture
                                           ? texture_resource_size(upload.texture_desc)
                                           : upload.buffer_desc.size;
        if (!make_room(required)) {
          upload.state = gpu_data_state::failed;
          upload.error = "GPU memory budget exceeded";
          upload.upload_data.reset();
          upload.upload_data_size = 0;
          ++versions_failed_;
          value->active_upload = nullptr;
        } else {
          if (upload_byte_budget_per_tick_ != 0 && uploaded_bytes_last_tick_ != 0 &&
              uploaded_bytes_last_tick_ + required > upload_byte_budget_per_tick_) {
            continue;
          }
          std::string error;
          detail::gpu_data_upload_ticket ticket;
          if (value->type == gpu_data_type::texture) {
            ticket = owned_backend_->request_texture_upload(upload.texture_desc, error);
          } else {
            ticket = owned_backend_->request_buffer_upload(upload.buffer_desc, error);
          }
          if (ticket.is_valid()) {
            upload.upload_ticket = ticket.id;
            upload.state = gpu_data_state::uploading;
            upload.error.clear();
            uploaded_bytes_last_tick_ += required;
            uploaded_bytes_total_ += required;
          } else {
            upload.state = gpu_data_state::failed;
            upload.error = error.empty() ? "upload request failed" : std::move(error);
            upload.upload_data.reset();
            upload.upload_data_size = 0;
            ++versions_failed_;
            value->active_upload = nullptr;
          }
        }
      }
    }
    update_entry_state(*value);
  }
}

gpu_data_manager_stats gpu_data_manager::get_stats() const noexcept {
  gpu_data_manager_stats stats;
  stats.textures_submitted = textures_submitted_;
  stats.buffers_submitted = buffers_submitted_;
  stats.allocated_bytes = allocated_bytes_;
  stats.memory_budget_bytes = memory_budget_bytes_;
  stats.versions_submitted = versions_submitted_;
  stats.versions_ready = versions_ready_;
  stats.versions_failed = versions_failed_;
  stats.versions_canceled = versions_canceled_;
  stats.versions_pruned = versions_pruned_;
  stats.resources_destroyed = resources_destroyed_;
  stats.uploaded_bytes_last_tick = uploaded_bytes_last_tick_;
  stats.uploaded_bytes_total = uploaded_bytes_total_;
  stats.upload_byte_budget_per_tick = upload_byte_budget_per_tick_;

  for (const auto& [uri, entry_value] : entries_) {
    (void)uri;
    entry* value = entry_value.get();
    if (value->type == gpu_data_type::texture) {
      ++stats.texture_count;
    } else if (value->type == gpu_data_type::buffer) {
      ++stats.buffer_count;
    }
    stats.version_count += value->versions.size();
    for (const auto& upload : value->versions) {
      if (value->type == gpu_data_type::texture) {
        stats.texture_bytes += upload->size_bytes;
      } else {
        stats.buffer_bytes += upload->size_bytes;
      }
    }
    if (value->ref_count.load(std::memory_order_acquire) > 0) {
      ++stats.referenced_count;
    }
    if (value->state == gpu_data_state::ready) {
      ++stats.ready_count;
    } else if (value->state == gpu_data_state::failed) {
      ++stats.failed_count;
    } else if (value->state == gpu_data_state::pending) {
      ++stats.pending_count;
    } else if (value->state == gpu_data_state::uploading) {
      ++stats.uploading_count;
    } else if (value->state == gpu_data_state::canceled) {
      ++stats.canceled_count;
    } else if (value->state == gpu_data_state::releasing) {
      ++stats.releasing_count;
    }
    if (value->ref_count.load(std::memory_order_acquire) == 0) {
      for (const auto& upload : value->versions) {
        stats.evictable_bytes += upload->size_bytes;
      }
    }
  }
  return stats;
}

std::size_t gpu_data_manager::live_resource_count() const noexcept {
  std::size_t count = 0;
  for (const auto& [uri, value] : entries_) {
    (void)uri;
    if (value->ref_count.load(std::memory_order_acquire) > 0) {
      ++count;
    }
  }
  return count;
}

} // namespace render_core
} // namespace cyberverse
