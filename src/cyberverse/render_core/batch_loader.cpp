/**
 * @file batch_loader.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Batch scene resource loader implementation
 * @version 0.1
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/batch_loader.hpp"

#include "cyberverse/render_core/render_device.hpp"

#include <algorithm>
#include <utility>

namespace cyberverse {
namespace render_core {

namespace {

bool is_gpu_uri(const resource_uri& uri) noexcept {
  return uri.is_valid() && (uri.type() == resource_type::texture || uri.type() == resource_type::buffer);
}

} // namespace

batch_loader::batch_loader(batch_load_manifest manifest, render_device& device)
    : batch_loader{ std::move(manifest), device.pipelines(), device.gpu_data() } {}

batch_loader::batch_loader(batch_load_manifest manifest, pipeline_manager& pipelines, gpu_data_manager& gpu_data)
    : pipelines_{ &pipelines }, gpu_data_{ &gpu_data } {
  manifest_.srbs = std::move(manifest.srbs);
  manifest_.pso_uris = std::move(manifest.pso_uris);
  manifest_.gpu_data = std::move(manifest.gpu_data);

  if (!expand_manifest()) {
    state_ = batch_load_state::failed;
    return;
  }

  state_ = total_count() == 0 ? batch_load_state::ready : batch_load_state::pending;
}

batch_loader::operator bool() const noexcept { return state_ == batch_load_state::ready; }

const batch_load_manifest& batch_loader::manifest() const noexcept { return manifest_; }

void batch_loader::tick(const runtime::frame_budget& budget) {
  if (state_ == batch_load_state::ready || state_ == batch_load_state::failed ||
      state_ == batch_load_state::cancelled) {
    return;
  }

  state_ = batch_load_state::loading;
  if (total_count() == 0) {
    state_ = batch_load_state::ready;
    return;
  }

  process_rules(budget);
  if (state_ == batch_load_state::failed || !budget.has_remaining()) {
    return;
  }
  process_pso(budget);
  if (state_ == batch_load_state::failed || !budget.has_remaining()) {
    return;
  }
  process_gpu(budget);
  if (state_ == batch_load_state::failed || !budget.has_remaining()) {
    return;
  }
  process_srbs(budget);
  if (state_ == batch_load_state::failed) {
    return;
  }

  if (ready_count() == total_count()) {
    state_ = batch_load_state::ready;
  }
}

void batch_loader::cancel() noexcept {
  if (state_ == batch_load_state::cancelled || state_ == batch_load_state::failed) {
    return;
  }
  state_ = batch_load_state::cancelled;
  release_gpu_refs();
}

batch_load_state batch_loader::state() const noexcept { return state_; }

float batch_loader::progress() const noexcept {
  const std::size_t total = total_count();
  if (total == 0) {
    return state_ == batch_load_state::failed ? 0.0f : 1.0f;
  }
  return static_cast<float>(ready_count()) / static_cast<float>(total);
}

std::span<const batch_load_error> batch_loader::errors() const noexcept { return errors_; }

bool batch_loader::expand_manifest() {
  const std::vector<resource_uri> explicit_gpu = std::move(manifest_.gpu_data);
  const std::vector<resource_uri> explicit_pso = std::move(manifest_.pso_uris);
  manifest_.gpu_data.clear();
  manifest_.pso_uris.clear();

  for (const resource_uri& uri : explicit_gpu) {
    if (!is_gpu_uri(uri)) {
      fail({ batch_load_error_kind::manifest, uri, {}, "gpu_data URI must name a texture or buffer" });
      return false;
    }
  }
  for (const resource_uri& uri : explicit_pso) {
    if (!uri.is_valid() || uri.type() != resource_type::pso) {
      fail({ batch_load_error_kind::manifest, uri, {}, "pso_uris URI must name a PSO" });
      return false;
    }
  }
  for (const pipeline_srb_key& key : manifest_.srbs) {
    if (!key.pso_uri.is_valid() || key.pso_uri.type() != resource_type::pso || !key.binding_rule.is_valid() ||
        key.binding_rule.type() != resource_type::binding || key.signature_index < anonymous_signature_index) {
      fail({ batch_load_error_kind::manifest, key.pso_uri, key, "SRB key is invalid" });
      return false;
    }
  }

  for (const resource_uri& uri : explicit_gpu) {
    add_gpu(uri);
  }
  for (const resource_uri& uri : explicit_pso) {
    add_pso(uri);
  }

  for (const pipeline_srb_key& key : manifest_.srbs) {
    if (std::any_of(srb_items_.begin(), srb_items_.end(), [&](const srb_item& item) {
          return item.key == key;
        })) {
      continue;
    }
    srb_items_.push_back(srb_item{ key, false, false });
    add_pso(key.pso_uri);
    add_rule(key.binding_rule);
  }

  for (rule_item& item : rule_items_) {
    if (!pipelines_->contains(item.uri)) {
      continue;
    }
    const pipeline_resource_status status = pipelines_->get_state(item.uri);
    if (status.type == pipeline_resource_state::failed) {
      fail({ batch_load_error_kind::binding_rule, item.uri, {}, status.error });
      return false;
    }
    if (status.type == pipeline_resource_state::ready && !infer_binding_rule(item.uri)) {
      fail({ batch_load_error_kind::binding_rule, item.uri, {}, "could not read binding rule resources" });
      return false;
    }
    item.ready = status.type == pipeline_resource_state::ready;
  }

  return errors_.empty();
}

void batch_loader::add_pso(const resource_uri& uri) {
  if (std::any_of(pso_items_.begin(), pso_items_.end(), [&](const pso_item& item) {
        return item.uri == uri;
      })) {
    return;
  }
  pso_items_.push_back(pso_item{ uri, false });
  manifest_.pso_uris.push_back(uri);
}

void batch_loader::add_rule(const resource_uri& uri) {
  if (std::any_of(rule_items_.begin(), rule_items_.end(), [&](const rule_item& item) {
        return item.uri == uri;
      })) {
    return;
  }
  rule_items_.push_back(rule_item{ uri, false, false });
}

void batch_loader::add_gpu(const resource_uri& uri) {
  if (!is_gpu_uri(uri)) {
    fail({ batch_load_error_kind::manifest, uri, {}, "inferred GPU dependency is not a texture or buffer" });
    return;
  }
  if (std::any_of(gpu_items_.begin(), gpu_items_.end(), [&](const gpu_item& item) {
        return item.uri == uri;
      })) {
    return;
  }
  gpu_items_.push_back(gpu_item{ uri, false });
  manifest_.gpu_data.push_back(uri);
}

bool batch_loader::infer_binding_rule(const resource_uri& uri) {
  const auto rule = std::find_if(rule_items_.begin(), rule_items_.end(), [&](const rule_item& item) {
    return item.uri == uri;
  });
  if (rule == rule_items_.end() || rule->inferred) {
    return true;
  }

  const binding_rule* value = pipelines_->get_binding_rule(uri);
  if (value == nullptr) {
    return false;
  }
  for (const variable_binding& binding : value->mutable_bindings) {
    add_gpu(binding.resource.uri);
  }
  for (const variable_binding& binding : value->dynamic_bindings) {
    add_gpu(binding.resource.uri);
  }
  rule->inferred = true;
  return errors_.empty();
}

void batch_loader::process_rules(const runtime::frame_budget& budget) {
  for (rule_item& item : rule_items_) {
    if (item.ready) {
      continue;
    }
    if (!budget.has_remaining()) {
      return;
    }
    if (!pipelines_->contains(item.uri)) {
      fail({ batch_load_error_kind::binding_rule, item.uri, {}, "binding rule is not registered" });
      return;
    }
    const pipeline_resource_status status = pipelines_->get_state(item.uri);
    if (status.type == pipeline_resource_state::failed) {
      fail({ batch_load_error_kind::binding_rule, item.uri, {}, status.error });
      return;
    }
    if (status.type != pipeline_resource_state::ready) {
      continue;
    }
    if (!infer_binding_rule(item.uri)) {
      fail({ batch_load_error_kind::binding_rule, item.uri, {}, "could not read binding rule resources" });
      return;
    }
    item.ready = true;
  }
}

void batch_loader::process_pso(const runtime::frame_budget& budget) {
  for (pso_item& item : pso_items_) {
    if (item.ready) {
      continue;
    }
    if (!budget.has_remaining()) {
      return;
    }
    if (!pipelines_->contains(item.uri)) {
      fail({ batch_load_error_kind::pso, item.uri, {}, "PSO is not registered" });
      return;
    }
    const pipeline_resource_status status = pipelines_->get_state(item.uri);
    if (status.type == pipeline_resource_state::failed) {
      fail({ batch_load_error_kind::pso, item.uri, {}, status.error });
      return;
    }
    item.ready = status.type == pipeline_resource_state::ready;
  }
}

void batch_loader::process_gpu(const runtime::frame_budget& budget) {
  for (gpu_item& item : gpu_items_) {
    if (item.acquired) {
      continue;
    }
    if (!budget.has_remaining()) {
      return;
    }
    const gpu_data_state status = gpu_data_->get_state(item.uri);
    if (status == gpu_data_state::pending || status == gpu_data_state::uploading) {
      continue;
    }
    if (status != gpu_data_state::ready) {
      fail({ batch_load_error_kind::gpu_data, item.uri, {}, "GPU data is not ready" });
      return;
    }

    if (item.uri.type() == resource_type::texture) {
      texture_data_ref ref = gpu_data_->get_texture(item.uri);
      if (!ref.is_valid()) {
        fail({ batch_load_error_kind::gpu_data, item.uri, {}, "texture reference is not available" });
        return;
      }
      texture_refs_.push_back(std::move(ref));
    } else {
      buffer_data_ref ref = gpu_data_->get_buffer(item.uri);
      if (!ref.is_valid()) {
        fail({ batch_load_error_kind::gpu_data, item.uri, {}, "buffer reference is not available" });
        return;
      }
      buffer_refs_.push_back(std::move(ref));
    }
    item.acquired = true;
  }
}

void batch_loader::process_srbs(const runtime::frame_budget& budget) {
  for (srb_item& item : srb_items_) {
    if (item.ready) {
      continue;
    }
    if (!budget.has_remaining()) {
      return;
    }

    const auto pso = std::find_if(pso_items_.begin(), pso_items_.end(), [&](const pso_item& value) {
      return value.uri == item.key.pso_uri;
    });
    const auto rule = std::find_if(rule_items_.begin(), rule_items_.end(), [&](const rule_item& value) {
      return value.uri == item.key.binding_rule;
    });
    if (pso == pso_items_.end() || rule == rule_items_.end() || !pso->ready || !rule->ready) {
      continue;
    }

    const pipeline_resource_status status = pipelines_->get_srb_state(item.key);
    if (status.type == pipeline_resource_state::ready) {
      item.ready = true;
      continue;
    }
    if (status.type == pipeline_resource_state::pending) {
      continue;
    }
    if (item.requested) {
      fail({ batch_load_error_kind::srb, item.key.pso_uri, item.key, status.error });
      return;
    }

    const std::expected<void, std::error_code> result = pipelines_->request_create_srb(item.key);
    if (!result.has_value()) {
      fail({ batch_load_error_kind::srb, item.key.pso_uri, item.key, result.error().message() });
      return;
    }
    item.requested = true;
  }
}

void batch_loader::fail(batch_load_error error) {
  if (state_ == batch_load_state::failed || state_ == batch_load_state::cancelled) {
    return;
  }
  if (error.message.empty()) {
    error.message = "batch load failed";
  }
  errors_.push_back(std::move(error));
  state_ = batch_load_state::failed;
  release_gpu_refs();
}

void batch_loader::release_gpu_refs() noexcept {
  texture_refs_.clear();
  buffer_refs_.clear();
}

std::size_t batch_loader::ready_count() const noexcept {
  std::size_t count = 0;
  for (const pso_item& item : pso_items_) {
    count += item.ready ? 1 : 0;
  }
  for (const rule_item& item : rule_items_) {
    count += item.ready ? 1 : 0;
  }
  for (const gpu_item& item : gpu_items_) {
    count += item.acquired ? 1 : 0;
  }
  for (const srb_item& item : srb_items_) {
    count += item.ready ? 1 : 0;
  }
  return count;
}

std::size_t batch_loader::total_count() const noexcept {
  return pso_items_.size() + rule_items_.size() + gpu_items_.size() + srb_items_.size();
}

} // namespace render_core
} // namespace cyberverse
