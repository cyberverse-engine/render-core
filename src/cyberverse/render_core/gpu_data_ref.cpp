/**
 * @file gpu_data_ref.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief GPU data reference implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/gpu_data_ref.hpp"

#include <utility>

#include "cyberverse/render_core/gpu_data_manager.hpp"

namespace cyberverse {
namespace render_core {

gpu_data_ref::gpu_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept
    : manager_{ manager }, entry_{ entry }, type_{ type } {}

gpu_data_ref::gpu_data_ref(const gpu_data_ref& other) noexcept
    : manager_{ other.manager_ }, entry_{ other.entry_ }, type_{ other.type_ } {
  if (manager_ != nullptr) {
    manager_->retain_ref(entry_);
  }
}

gpu_data_ref::gpu_data_ref(gpu_data_ref&& other) noexcept
    : manager_{ other.manager_ }, entry_{ other.entry_ }, type_{ other.type_ } {
  other.manager_ = nullptr;
  other.entry_ = nullptr;
  other.type_ = gpu_data_type::undefined;
}

gpu_data_ref& gpu_data_ref::operator=(const gpu_data_ref& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  manager_ = other.manager_;
  entry_ = other.entry_;
  type_ = other.type_;
  if (manager_ != nullptr) {
    manager_->retain_ref(entry_);
  }
  return *this;
}

gpu_data_ref& gpu_data_ref::operator=(gpu_data_ref&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  manager_ = other.manager_;
  entry_ = other.entry_;
  type_ = other.type_;
  other.manager_ = nullptr;
  other.entry_ = nullptr;
  other.type_ = gpu_data_type::undefined;
  return *this;
}

gpu_data_ref::~gpu_data_ref() { reset(); }

void gpu_data_ref::reset() noexcept {
  if (manager_ != nullptr) {
    manager_->release_ref(entry_);
  }
  manager_ = nullptr;
  entry_ = nullptr;
  type_ = gpu_data_type::undefined;
}

bool gpu_data_ref::is_ready() const noexcept {
  return manager_ != nullptr && manager_->entry_state(entry_) == gpu_data_state::ready;
}

gpu_data_state gpu_data_ref::state() const noexcept {
  return manager_ != nullptr ? manager_->entry_state(entry_) : gpu_data_state::failed;
}

std::string_view gpu_data_ref::error() const noexcept {
  return manager_ != nullptr ? manager_->entry_error(entry_) : std::string_view{};
}

submit_version gpu_data_ref::current_version() const noexcept {
  return manager_ != nullptr ? manager_->entry_current_version(entry_) : 0;
}

std::optional<gpu_data_state> gpu_data_ref::upload_state(submit_version version) const noexcept {
  return manager_ != nullptr ? manager_->entry_version_state(entry_, version) : std::nullopt;
}

std::string_view gpu_data_ref::upload_error(submit_version version) const noexcept {
  return manager_ != nullptr ? manager_->entry_version_error(entry_, version) : std::string_view{};
}

texture_data_ref::texture_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept
    : gpu_data_ref{ manager, entry, type } {}

texture_data_ref::texture_data_ref(const texture_data_ref& other) noexcept : gpu_data_ref{ other } {}

texture_data_ref::texture_data_ref(texture_data_ref&& other) noexcept : gpu_data_ref{ std::move(other) } {}

texture_data_ref& texture_data_ref::operator=(const texture_data_ref& other) noexcept {
  gpu_data_ref::operator=(other);
  return *this;
}

texture_data_ref& texture_data_ref::operator=(texture_data_ref&& other) noexcept {
  gpu_data_ref::operator=(std::move(other));
  return *this;
}

Diligent::ITexture* texture_data_ref::get() const noexcept {
  return is_valid() ? manager_->current_texture(entry_) : nullptr;
}

buffer_data_ref::buffer_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept
    : gpu_data_ref{ manager, entry, type } {}

buffer_data_ref::buffer_data_ref(const buffer_data_ref& other) noexcept : gpu_data_ref{ other } {}

buffer_data_ref::buffer_data_ref(buffer_data_ref&& other) noexcept : gpu_data_ref{ std::move(other) } {}

buffer_data_ref& buffer_data_ref::operator=(const buffer_data_ref& other) noexcept {
  gpu_data_ref::operator=(other);
  return *this;
}

buffer_data_ref& buffer_data_ref::operator=(buffer_data_ref&& other) noexcept {
  gpu_data_ref::operator=(std::move(other));
  return *this;
}

Diligent::IBuffer* buffer_data_ref::get() const noexcept {
  return is_valid() ? manager_->current_buffer(entry_) : nullptr;
}

} // namespace render_core
} // namespace cyberverse
