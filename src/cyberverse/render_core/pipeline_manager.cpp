/**
 * @file pipeline_manager.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline state object registry implementation
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/pipeline_manager.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if CYBERVERSE_RENDER_CORE_WITH_DRSN
#include <RenderStateNotationLoader.h>
#endif

namespace cyberverse {
namespace render_core {

namespace {

std::error_code pipeline_error(render_core_errc error) noexcept { return make_error_code(error); }

bool is_pso_uri(const resource_uri& uri) noexcept { return uri.is_valid() && uri.type() == resource_type::pso; }

bool is_binding_uri(const resource_uri& uri) noexcept { return uri.is_valid() && uri.type() == resource_type::binding; }

bool is_texture_or_buffer_uri(const resource_uri& uri) noexcept {
  return uri.is_valid() && (uri.type() == resource_type::texture || uri.type() == resource_type::buffer);
}

bool is_buffer_range(const resource_bind_mode& mode) noexcept {
  return std::holds_alternative<bind_buffer_range>(mode);
}

std::string join_messages(const std::vector<std::string>& messages) {
  std::string result;
  for (const std::string& message : messages) {
    if (!result.empty()) {
      result += "; ";
    }
    result += message;
  }
  return result;
}

const Diligent::PipelineResourceDesc* find_resource_desc(const Diligent::PipelineResourceSignatureDesc& signature,
                                                         std::string_view name) noexcept {
  for (Diligent::Uint32 index = 0; index < signature.NumResources; ++index) {
    const Diligent::PipelineResourceDesc& resource = signature.Resources[index];
    if (resource.Name != nullptr && name == resource.Name) {
      return &resource;
    }
  }
  return nullptr;
}

constexpr Diligent::SHADER_TYPE shader_stages[] = {
  Diligent::SHADER_TYPE_VERTEX,        Diligent::SHADER_TYPE_PIXEL,  Diligent::SHADER_TYPE_GEOMETRY,
  Diligent::SHADER_TYPE_HULL,          Diligent::SHADER_TYPE_DOMAIN, Diligent::SHADER_TYPE_COMPUTE,
  Diligent::SHADER_TYPE_AMPLIFICATION, Diligent::SHADER_TYPE_MESH,
};

bool validate_binding_rule(const binding_rule& rule, std::string& error) {
  std::unordered_set<std::string> names;
  auto validate = [&](const variable_binding& binding) -> bool {
    if (binding.variable_name.empty()) {
      error = "binding rule contains an empty variable name";
      return false;
    }
    if (!is_texture_or_buffer_uri(binding.resource.uri)) {
      error = "binding rule variable '" + binding.variable_name + "' must reference a texture or buffer URI";
      return false;
    }
    if (!names.insert(binding.variable_name).second) {
      error = "binding rule variable '" + binding.variable_name + "' is bound more than once";
      return false;
    }
    if (is_buffer_range(binding.resource.mode) && binding.resource.uri.type() != resource_type::buffer) {
      error = "binding rule variable '" + binding.variable_name + "' uses a buffer range for a non-buffer resource";
      return false;
    }
    return true;
  };

  for (const variable_binding& binding : rule.mutable_bindings) {
    if (!validate(binding)) {
      return false;
    }
  }
  for (const variable_binding& binding : rule.dynamic_bindings) {
    if (!validate(binding)) {
      return false;
    }
  }
  return true;
}

bool validate_pipeline_desc(const pipeline_desc& desc, std::string& error) {
  if (desc.kind == pipeline_kind::graphics) {
    if (desc.find_shader(shader_stage::vertex) == nullptr) {
      error = "graphics pipeline requires a vertex shader";
      return false;
    }
    if (desc.find_shader(shader_stage::pixel) == nullptr) {
      error = "graphics pipeline requires a pixel shader";
      return false;
    }
    if (desc.find_shader(shader_stage::compute) != nullptr) {
      error = "graphics pipeline cannot contain a compute shader";
      return false;
    }
  } else {
    if (desc.find_shader(shader_stage::compute) == nullptr) {
      error = "compute pipeline requires a compute shader";
      return false;
    }
    if (desc.find_shader(shader_stage::vertex) != nullptr || desc.find_shader(shader_stage::pixel) != nullptr) {
      error = "compute pipeline cannot contain graphics shaders";
      return false;
    }
  }

  std::unordered_set<int> stages;
  for (const pipeline_shader& shader : desc.shaders) {
    if (shader.key.name.empty() || shader.key.stage == shader_stage::undefined) {
      error = "pipeline contains an invalid shader key";
      return false;
    }
    if (!stages.insert(static_cast<int>(shader.key.stage)).second) {
      error = "pipeline contains duplicate shader stages";
      return false;
    }
  }
  for (const auto& signature : desc.resource_signatures) {
    if (signature == nullptr) {
      error = "pipeline contains a null resource signature";
      return false;
    }
  }
  return true;
}

} // namespace

pipeline_manager::pipeline_manager(Diligent::IRenderDevice* device,
                                   gpu_data_manager& gpu_data,
                                   shader_manager& shader_mgr,
                                   Diligent::IRenderStateNotationLoader* drsn_loader)
    : device_{ device }, gpu_data_mgr_{ gpu_data }, shader_mgr_{ shader_mgr }, drsn_loader_{ drsn_loader } {
  if (device_ == nullptr) {
    throw std::invalid_argument("pipeline_manager requires a non-null render device");
  }
}

pipeline_manager::~pipeline_manager() = default;

bool pipeline_manager::enqueue(task value) noexcept {
  try {
    tq_.push(std::move(value));
    return true;
  } catch (...) {
    return false;
  }
}

std::expected<void, std::error_code> pipeline_manager::request_load_drsn(const resource_uri& uri,
                                                                         std::string_view drsn_name) noexcept {
  try {
    if (!is_pso_uri(uri)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
#if !CYBERVERSE_RENDER_CORE_WITH_DRSN
    return std::unexpected(pipeline_error(render_core_errc::drsn_unavailable));
#else
    if (drsn_loader_ == nullptr) {
      return std::unexpected(pipeline_error(render_core_errc::drsn_unavailable));
    }
    task value;
    value.kind = task_kind::load_drsn;
    value.uri = uri;
    value.drsn_name.assign(drsn_name);
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
#endif
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code> pipeline_manager::request_create_pso(const resource_uri& uri,
                                                                          pipeline_desc desc) noexcept {
  try {
    if (!is_pso_uri(uri)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    std::string error;
    if (!validate_pipeline_desc(desc, error)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }

    task value;
    value.kind = task_kind::create_pso;
    value.uri = uri;
    value.desc = std::move(desc);
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code> pipeline_manager::request_create_srb(const pipeline_srb_key& key) noexcept {
  try {
    if (!is_pso_uri(key.pso_uri) || !is_binding_uri(key.binding_rule)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    if (key.signature_index < anonymous_signature_index) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }
    task value;
    value.kind = task_kind::create_srb;
    value.srb_key = key;
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code>
pipeline_manager::request_add(const resource_uri& uri, Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso) noexcept {
  try {
    if (!is_pso_uri(uri)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    if (pso == nullptr) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }
    task value;
    value.kind = task_kind::add_pso;
    value.uri = uri;
    value.pso = std::move(pso);
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code> pipeline_manager::request_add(const resource_uri& uri,
                                                                   binding_rule rule) noexcept {
  try {
    if (!is_binding_uri(uri)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    std::string error;
    if (!validate_binding_rule(rule, error)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }
    task value;
    value.kind = task_kind::add_binding_rule;
    value.uri = uri;
    value.rule = std::move(rule);
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code>
pipeline_manager::request_add(const pipeline_srb_key& key,
                              Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb) noexcept {
  try {
    if (!is_pso_uri(key.pso_uri) || !is_binding_uri(key.binding_rule)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    if (srb == nullptr || key.signature_index < anonymous_signature_index) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }

    task value;
    value.kind = task_kind::add_srb;
    value.srb_key = key;
    value.srb = std::move(srb);
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

std::expected<void, std::error_code> pipeline_manager::request_reload(const resource_uri& uri) noexcept {
  try {
    if (!is_pso_uri(uri) && !is_binding_uri(uri)) {
      return std::unexpected(pipeline_error(render_core_errc::invalid_uri));
    }
    task value;
    value.kind = task_kind::reload;
    value.uri = uri;
    if (!enqueue(std::move(value))) {
      return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
    }
    return {};
  } catch (...) {
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

void pipeline_manager::request_invalidate(const resource_uri& uri) noexcept {
  try {
    if (!is_pso_uri(uri) && !is_binding_uri(uri)) {
      return;
    }
    task value;
    value.kind = task_kind::invalidate;
    value.uri = uri;
    (void)enqueue(std::move(value));
  } catch (...) {
  }
}

void pipeline_manager::request_invalidate_failed() noexcept {
  try {
    task value;
    value.kind = task_kind::invalidate_failed;
    (void)enqueue(std::move(value));
  } catch (...) {
  }
}

void pipeline_manager::request_clear() noexcept {
  try {
    task value;
    value.kind = task_kind::clear;
    (void)enqueue(std::move(value));
  } catch (...) {
  }
}

void pipeline_manager::tick(const runtime::frame_budget& budget) {
  while (budget.has_remaining()) {
    task value;
    if (!tq_.try_pop(value)) {
      break;
    }
    execute_task(value);
  }

  process_pending(budget);
}

void pipeline_manager::execute_task(task& value) {
  switch (value.kind) {
  case task_kind::load_drsn: {
    const auto existing = pso_map_.find(value.uri);
    if (existing != pso_map_.end() && (existing->second.state == pipeline_resource_state::ready ||
                                       existing->second.state == pipeline_resource_state::pending)) {
      return;
    }

    pso_entry entry;
    entry.from = pso_from::drsn;
    entry.drsn_name = value.drsn_name;
    if (!value.drsn_name.empty()) {
      entry.drsn_name = value.drsn_name;
    }
    std::string error;
    entry.p_pso = load_pso_from_drsn(value.uri, value.drsn_name, error);
    if (entry.p_pso == nullptr) {
      entry.state = pipeline_resource_state::failed;
      entry.error = std::move(error);
    } else if (has_static_resources(entry.p_pso, error)) {
      entry.p_pso.Release();
      entry.state = pipeline_resource_state::failed;
      entry.error = std::move(error);
    } else {
      const Diligent::PIPELINE_STATE_STATUS status = entry.p_pso->GetStatus(false);
      entry.state = status == Diligent::PIPELINE_STATE_STATUS_READY
                        ? pipeline_resource_state::ready
                        : (status == Diligent::PIPELINE_STATE_STATUS_FAILED ? pipeline_resource_state::failed
                                                                            : pipeline_resource_state::pending);
      if (entry.state == pipeline_resource_state::failed) {
        entry.error = "DRSN pipeline creation failed";
        entry.p_pso.Release();
      }
    }
    pso_map_[value.uri] = std::move(entry);
    return;
  }
  case task_kind::create_pso: {
    const auto existing = pso_map_.find(value.uri);
    if (existing != pso_map_.end() && (existing->second.state == pipeline_resource_state::ready ||
                                       existing->second.state == pipeline_resource_state::pending)) {
      return;
    }

    pso_entry entry;
    entry.from = pso_from::desc;
    entry.desc = std::move(value.desc);
    std::string error;
    auto built = build_pso_from_desc(value.uri, entry.desc, error);
    if (!built) {
      entry.state = pipeline_resource_state::failed;
      entry.error = std::move(error);
    } else {
      entry.p_pso = std::move(*built);
    }
    if (entry.p_pso == nullptr) {
      entry.state = pipeline_resource_state::failed;
      if (entry.error.empty()) {
        entry.error = std::move(error);
      }
    } else if (has_static_resources(entry.p_pso, error)) {
      entry.p_pso.Release();
      entry.state = pipeline_resource_state::failed;
      entry.error = std::move(error);
    } else {
      const Diligent::PIPELINE_STATE_STATUS status = entry.p_pso->GetStatus(false);
      entry.state = status == Diligent::PIPELINE_STATE_STATUS_READY
                        ? pipeline_resource_state::ready
                        : (status == Diligent::PIPELINE_STATE_STATUS_FAILED ? pipeline_resource_state::failed
                                                                            : pipeline_resource_state::pending);
      if (entry.state == pipeline_resource_state::failed) {
        entry.error = "pipeline creation failed";
        entry.p_pso.Release();
      }
    }
    pso_map_[value.uri] = std::move(entry);
    return;
  }
  case task_kind::add_pso: {
    pso_entry entry;
    entry.from = pso_from::manual;
    entry.p_pso = std::move(value.pso);
    if (entry.p_pso == nullptr) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "null pipeline state";
    } else {
      std::string error;
      if (has_static_resources(entry.p_pso, error)) {
        entry.p_pso.Release();
        entry.state = pipeline_resource_state::failed;
        entry.error = std::move(error);
      } else {
        const Diligent::PIPELINE_STATE_STATUS status = entry.p_pso->GetStatus(false);
        entry.state = status == Diligent::PIPELINE_STATE_STATUS_READY
                          ? pipeline_resource_state::ready
                          : (status == Diligent::PIPELINE_STATE_STATUS_FAILED ? pipeline_resource_state::failed
                                                                              : pipeline_resource_state::pending);
        if (entry.state == pipeline_resource_state::failed) {
          entry.error = "pipeline state is not ready";
          entry.p_pso.Release();
        }
      }
    }
    remove_srbs_for_pso(value.uri);
    pso_map_[value.uri] = std::move(entry);
    return;
  }
  case task_kind::add_binding_rule: {
    binding_rule_entry entry;
    entry.rule = std::move(value.rule);
    std::string error;
    if (!validate_binding_rule(entry.rule, error)) {
      entry.state = pipeline_resource_state::failed;
      entry.error = std::move(error);
    } else if (!collect_dependencies(entry.rule, entry.dep_resources)) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "binding rule contains an invalid dependency";
    } else {
      entry.state = pipeline_resource_state::ready;
    }
    remove_srbs_for_binding(value.uri);
    binding_rule_map_[value.uri] = std::move(entry);
    return;
  }
  case task_kind::create_srb:
    create_or_update_srb(value.srb_key);
    return;
  case task_kind::add_srb: {
    const auto pso = pso_map_.find(value.srb_key.pso_uri);
    const auto rule = binding_rule_map_.find(value.srb_key.binding_rule);
    srb_entry entry;
    if (pso == pso_map_.end() || rule == binding_rule_map_.end()) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "PSO or binding rule is not registered";
    } else if (pso->second.p_pso == nullptr || pso->second.state != pipeline_resource_state::ready) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "PSO is not ready";
    } else if (value.srb_key.signature_index >= 0) {
      const Diligent::Uint32 signature_count = pso->second.p_pso->GetResourceSignatureCount();
      if (static_cast<Diligent::Uint32>(value.srb_key.signature_index) >= signature_count ||
          pso->second.p_pso->GetResourceSignature(static_cast<Diligent::Uint32>(value.srb_key.signature_index)) ==
              nullptr) {
        entry.state = pipeline_resource_state::failed;
        entry.error = "resource signature index is out of range";
      }
    }

    if (entry.state == pipeline_resource_state::failed && !entry.error.empty()) {
      srb_map_[value.srb_key] = std::move(entry);
      return;
    }

    entry.p_srb = std::move(value.srb);
    entry.dep_resources = rule->second.dep_resources;
    entry.state = pipeline_resource_state::ready;
    srb_map_[value.srb_key] = std::move(entry);
    return;
  }
  case task_kind::reload: {
    if (value.uri.type() == resource_type::pso) {
      auto found = pso_map_.find(value.uri);
      if (found == pso_map_.end()) {
        pso_entry entry;
        entry.state = pipeline_resource_state::failed;
        entry.error = "PSO is not registered";
        pso_map_[value.uri] = std::move(entry);
        return;
      }
      if (found->second.from == pso_from::manual) {
        found->second.error = "manually added PSO cannot be reloaded";
        return;
      }

      std::string error;
      Diligent::RefCntAutoPtr<Diligent::IPipelineState> replacement;
      if (found->second.from == pso_from::drsn) {
        replacement = load_pso_from_drsn(value.uri, found->second.drsn_name, error);
      } else {
        auto built = build_pso_from_desc(value.uri, found->second.desc, error);
        if (built) {
          replacement = std::move(*built);
        }
      }
      if (replacement == nullptr) {
        found->second.error = std::move(error);
        return;
      }
      if (has_static_resources(replacement, error)) {
        found->second.error = std::move(error);
        return;
      }

      found->second.p_pso = std::move(replacement);
      found->second.error.clear();
      const Diligent::PIPELINE_STATE_STATUS status = found->second.p_pso->GetStatus(false);
      found->second.state = status == Diligent::PIPELINE_STATE_STATUS_READY
                                ? pipeline_resource_state::ready
                                : (status == Diligent::PIPELINE_STATE_STATUS_FAILED ? pipeline_resource_state::failed
                                                                                    : pipeline_resource_state::pending);
      rebuild_srbs_for_pso(value.uri);
      return;
    }

    auto found = binding_rule_map_.find(value.uri);
    if (found == binding_rule_map_.end()) {
      binding_rule_entry entry;
      entry.state = pipeline_resource_state::failed;
      entry.error = "binding rule is not registered";
      binding_rule_map_[value.uri] = std::move(entry);
      return;
    }
    found->second.state = pipeline_resource_state::ready;
    found->second.error.clear();
    rebuild_srbs_for_binding(value.uri);
    return;
  }
  case task_kind::invalidate:
    invalidate_uri(value.uri);
    return;
  case task_kind::invalidate_failed:
    invalidate_failed();
    return;
  case task_kind::clear:
    clear_all();
    return;
  }
}

void pipeline_manager::process_pending(const runtime::frame_budget& budget) {
  for (auto& [uri, entry] : pso_map_) {
    (void)uri;
    if (entry.state != pipeline_resource_state::pending || entry.p_pso == nullptr) {
      continue;
    }
    if (!budget.has_remaining()) {
      return;
    }
    const Diligent::PIPELINE_STATE_STATUS status = entry.p_pso->GetStatus(false);
    if (status == Diligent::PIPELINE_STATE_STATUS_READY) {
      entry.state = pipeline_resource_state::ready;
      entry.error.clear();
    } else if (status == Diligent::PIPELINE_STATE_STATUS_FAILED) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "pipeline creation failed";
      entry.p_pso.Release();
    }
  }

  std::vector<pipeline_srb_key> pending;
  pending.reserve(srb_map_.size());
  for (const auto& [key, entry] : srb_map_) {
    if (entry.state == pipeline_resource_state::pending) {
      pending.push_back(key);
    }
  }

  for (const pipeline_srb_key& key : pending) {
    if (!budget.has_remaining()) {
      return;
    }
    const auto found = srb_map_.find(key);
    if (found != srb_map_.end() && found->second.state == pipeline_resource_state::pending) {
      create_or_update_srb(key);
    }
  }
}

std::expected<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, std::error_code>
pipeline_manager::build_pso_from_desc(const resource_uri& uri, const pipeline_desc& desc, std::string& error) noexcept {
  try {
    std::string validation_error;
    if (!validate_pipeline_desc(desc, validation_error)) {
      error = std::move(validation_error);
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }

    auto load_shader = [&](shader_stage stage) -> Diligent::IShader* {
      const pipeline_shader* shader = desc.find_shader(stage);
      if (shader == nullptr) {
        return nullptr;
      }
      Diligent::IShader* result = nullptr;
      if (!shader->bytecode.empty()) {
        result = shader_mgr_.get_or_create_from_bytecode(shader->key, shader->bytecode, shader->key.name);
      } else if (!shader->source_text.empty()) {
        result = shader_mgr_.get_or_create_from_source(shader->key, shader->source_text, shader->key.name);
      } else {
        const std::filesystem::path path =
            shader->source.empty() ? std::filesystem::path{ shader->key.name } : shader->source;
        result = shader_mgr_.get_or_create_from_file(shader->key, path);
      }
      if (result == nullptr) {
        error = "failed to compile shader '" + shader->key.name + "': " + shader_mgr_.get_compile_log(shader->key);
      }
      return result;
    };

    std::vector<Diligent::IPipelineResourceSignature*> signatures;
    signatures.reserve(desc.resource_signatures.size());
    for (const auto& signature : desc.resource_signatures) {
      if (signature == nullptr) {
        error = "pipeline contains a null resource signature";
        return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
      }
      signatures.push_back(signature.RawPtr());
    }

    const std::string name = uri.to_string();
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;

    if (desc.kind == pipeline_kind::compute) {
      Diligent::ComputePipelineStateCreateInfo create_info;
      create_info.PSODesc = desc.pso_desc;
      create_info.PSODesc.Name = name.c_str();
      create_info.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
      create_info.Flags = desc.flags;
      create_info.ResourceSignaturesCount = static_cast<Diligent::Uint32>(signatures.size());
      create_info.ppResourceSignatures = signatures.empty() ? nullptr : signatures.data();
      create_info.pCS = load_shader(shader_stage::compute);
      if (create_info.pCS == nullptr) {
        if (error.empty()) {
          error = "compute pipeline requires a compute shader";
        }
        return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
      }

      Diligent::IPipelineState* raw = nullptr;
      device_->CreateComputePipelineState(create_info, &raw);
      pipeline.Attach(raw);
    } else {
      Diligent::GraphicsPipelineStateCreateInfo create_info;
      create_info.PSODesc = desc.pso_desc;
      create_info.PSODesc.Name = name.c_str();
      create_info.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
      create_info.Flags = desc.flags;
      create_info.GraphicsPipeline = desc.graphics_desc;
      create_info.ResourceSignaturesCount = static_cast<Diligent::Uint32>(signatures.size());
      create_info.ppResourceSignatures = signatures.empty() ? nullptr : signatures.data();
      create_info.pVS = load_shader(shader_stage::vertex);
      create_info.pPS = load_shader(shader_stage::pixel);
      if (!error.empty()) {
        return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
      }
      if (create_info.pVS == nullptr || create_info.pPS == nullptr) {
        error = "graphics pipeline requires vertex and pixel shaders";
        return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
      }

      Diligent::IPipelineState* raw = nullptr;
      device_->CreateGraphicsPipelineState(create_info, &raw);
      pipeline.Attach(raw);
    }

    if (pipeline == nullptr) {
      if (error.empty()) {
        error = "DiligentEngine could not create the pipeline state";
      }
      return std::unexpected(pipeline_error(render_core_errc::invalid_argument));
    }

    return pipeline;
  } catch (...) {
    error = "pipeline creation threw an exception";
    return std::unexpected(pipeline_error(render_core_errc::out_of_memory));
  }
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState>
pipeline_manager::load_pso_from_drsn(const resource_uri& uri, std::string_view drsn_name, std::string& error) noexcept {
#if CYBERVERSE_RENDER_CORE_WITH_DRSN
  try {
    if (drsn_loader_ == nullptr) {
      error = "DRSN loader is not available";
      return {};
    }

    const std::string name = drsn_name.empty() ? uri.to_string() : std::string{ drsn_name };
    Diligent::LoadPipelineStateInfo load_info;
    load_info.Name = name.c_str();
    load_info.PipelineType = Diligent::PIPELINE_TYPE_INVALID;
    load_info.AddToCache = false;
    load_info.LookupInCache = false;

    Diligent::IPipelineState* raw = nullptr;
    drsn_loader_->LoadPipelineState(load_info, &raw);
    if (raw == nullptr) {
      error = "DRSN loader could not load pipeline '" + name + "'";
      return {};
    }
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
    pipeline.Attach(raw);
    return pipeline;
  } catch (...) {
    error = "DRSN pipeline loading threw an exception";
    return {};
  }
#else
  (void)uri;
  (void)drsn_name;
  error = "DRSN support is not compiled into this build";
  return {};
#endif
}

bool pipeline_manager::has_static_resources(Diligent::IPipelineState* pso, std::string& error) const noexcept {
  if (pso == nullptr) {
    error = "null pipeline state";
    return true;
  }

  const Diligent::Uint32 signature_count = pso->GetResourceSignatureCount();
  for (Diligent::Uint32 signature_index = 0; signature_index < signature_count; ++signature_index) {
    Diligent::IPipelineResourceSignature* signature = pso->GetResourceSignature(signature_index);
    if (signature == nullptr) {
      error = "pipeline contains a null resource signature";
      return true;
    }
    const Diligent::PipelineResourceSignatureDesc& desc = signature->GetDesc();
    for (Diligent::Uint32 resource_index = 0; resource_index < desc.NumResources; ++resource_index) {
      if (desc.Resources[resource_index].VarType == Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC) {
        error = "pipeline contains a static resource; static resources must be moved into the binding rule";
        return true;
      }
    }
  }
  return false;
}

bool pipeline_manager::collect_dependencies(const binding_rule& rule, std::vector<resource_uri>& dependencies) const {
  try {
    for (const variable_binding& binding : rule.mutable_bindings) {
      if (!is_texture_or_buffer_uri(binding.resource.uri)) {
        return false;
      }
      const bool exists = std::any_of(dependencies.begin(), dependencies.end(), [&](const resource_uri& uri) {
        return uri == binding.resource.uri;
      });
      if (!exists) {
        dependencies.push_back(binding.resource.uri);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

void pipeline_manager::create_or_update_srb(const pipeline_srb_key& key) {
  const resource_uri& pso_uri = key.pso_uri;
  const resource_uri& br_uri = key.binding_rule;
  const signature_index_type signature_index = key.signature_index;
  srb_entry& entry = srb_map_[key];

  const auto pso_found = pso_map_.find(pso_uri);
  if (pso_found == pso_map_.end() || pso_found->second.p_pso == nullptr) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "PSO is not registered";
    return;
  }
  if (pso_found->second.state == pipeline_resource_state::failed) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "PSO is failed";
    return;
  }
  if (pso_found->second.state == pipeline_resource_state::pending) {
    entry.state = pipeline_resource_state::pending;
    entry.error.clear();
    return;
  }

  const auto rule_found = binding_rule_map_.find(br_uri);
  if (rule_found == binding_rule_map_.end()) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "binding rule is not registered";
    return;
  }
  if (rule_found->second.state == pipeline_resource_state::failed) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "binding rule is failed";
    return;
  }

  Diligent::IPipelineState* pso = pso_found->second.p_pso.RawPtr();
  const Diligent::Uint32 signature_count = pso->GetResourceSignatureCount();
  if (signature_count == 0) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "PSO has no resource signatures";
    return;
  }

  if (signature_index == anonymous_signature_index && pso_found->second.from == pso_from::desc &&
      !pso_found->second.desc.resource_signatures.empty()) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "anonymous signature is not available for an explicit-signature PSO";
    return;
  }

  Diligent::IPipelineResourceSignature* signature = nullptr;
  if (signature_index == anonymous_signature_index) {
    signature = pso->GetResourceSignature(0);
  } else {
    if (static_cast<Diligent::Uint32>(signature_index) >= signature_count) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "resource signature index is out of range";
      return;
    }
    signature = pso->GetResourceSignature(static_cast<Diligent::Uint32>(signature_index));
  }
  if (signature == nullptr) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "resource signature is not available";
    return;
  }
  const Diligent::PipelineResourceSignatureDesc& signature_desc = signature->GetDesc();

  const binding_rule& rule = rule_found->second.rule;
  auto variable_exists = [&](const std::string& name, Diligent::SHADER_RESOURCE_VARIABLE_TYPE required_type) {
    for (Diligent::Uint32 index = 0; index < signature_count; ++index) {
      Diligent::IPipelineResourceSignature* candidate = pso->GetResourceSignature(index);
      if (candidate == nullptr) {
        continue;
      }
      const Diligent::PipelineResourceDesc* resource = find_resource_desc(candidate->GetDesc(), name);
      if (resource != nullptr && resource->VarType == required_type) {
        return true;
      }
    }
    return false;
  };
  for (const variable_binding& binding : rule.mutable_bindings) {
    if (!variable_exists(binding.variable_name, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "mutable variable '" + binding.variable_name + "' was not found in the PSO resource signatures";
      return;
    }
  }
  for (const variable_binding& binding : rule.dynamic_bindings) {
    if (!variable_exists(binding.variable_name, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)) {
      entry.state = pipeline_resource_state::failed;
      entry.error = "dynamic variable '" + binding.variable_name + "' was not found in the PSO resource signatures";
      return;
    }
  }

  try {
    std::vector<resource_uri> dependencies;
    for (const variable_binding& binding : rule.mutable_bindings) {
      const Diligent::PipelineResourceDesc* resource = find_resource_desc(signature_desc, binding.variable_name);
      if (resource == nullptr || resource->VarType != Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE) {
        continue;
      }
      const bool exists = std::any_of(dependencies.begin(), dependencies.end(), [&](const resource_uri& uri) {
        return uri == binding.resource.uri;
      });
      if (!exists) {
        dependencies.push_back(binding.resource.uri);
      }
    }
    entry.dep_resources = std::move(dependencies);
  } catch (...) {
    entry.state = pipeline_resource_state::failed;
    entry.error = "could not collect SRB dependencies";
    return;
  }

  bool dependency_pending = false;
  for (const resource_uri& dependency : entry.dep_resources) {
    const gpu_data_state state = gpu_data_mgr_.get_state(dependency);
    if (state == gpu_data_state::ready) {
      continue;
    }
    if (state == gpu_data_state::pending || state == gpu_data_state::uploading) {
      dependency_pending = true;
      continue;
    }
    entry.state = pipeline_resource_state::failed;
    entry.error = "GPU data '" + dependency.to_string() + "' is not available";
    return;
  }
  if (dependency_pending) {
    entry.state = pipeline_resource_state::pending;
    entry.error.clear();
    return;
  }

  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  try {
    Diligent::IShaderResourceBinding* raw_srb = nullptr;
    if (signature_index == anonymous_signature_index) {
      pso->CreateShaderResourceBinding(&raw_srb, false);
      if (raw_srb == nullptr) {
        entry.state = pipeline_resource_state::failed;
        entry.error = "could not create an anonymous SRB; the PSO may use explicit signatures";
        return;
      }
    } else {
      signature->CreateShaderResourceBinding(&raw_srb, false);
      if (raw_srb == nullptr) {
        entry.state = pipeline_resource_state::failed;
        entry.error = "could not create an SRB for the resource signature";
        return;
      }
    }
    srb.Attach(raw_srb);

    bind_result result = bind_mutable(rule, srb, signature_desc, entry);
    warnings = std::move(result.warnings);
    errors = std::move(result.errors);
    entry.dynamic_variables = resolve_dynamic(rule, srb, signature_desc);
  } catch (...) {
    errors.push_back("SRB creation threw an exception");
  }

  if (!errors.empty()) {
    entry.p_srb.Release();
    entry.dynamic_variables.clear();
    entry.texture_refs.clear();
    entry.buffer_refs.clear();
    entry.state = pipeline_resource_state::failed;
    entry.error = join_messages(errors);
    return;
  }

  entry.p_srb = std::move(srb);
  entry.state = pipeline_resource_state::ready;
  entry.error = join_messages(warnings);
}

pipeline_manager::bind_result pipeline_manager::bind_mutable(const binding_rule& rule,
                                                             Diligent::IShaderResourceBinding* p_srb,
                                                             const Diligent::PipelineResourceSignatureDesc& signature,
                                                             srb_entry& entry) {
  bind_result result;
  result.total = static_cast<std::uint32_t>(rule.mutable_bindings.size());
  if (p_srb == nullptr) {
    result.errors.push_back("SRB is null");
    return result;
  }

  for (const variable_binding& binding : rule.mutable_bindings) {
    const Diligent::PipelineResourceDesc* resource = find_resource_desc(signature, binding.variable_name);
    if (resource == nullptr) {
      continue;
    }
    if (resource->VarType != Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE) {
      result.errors.push_back("variable '" + binding.variable_name + "' is not mutable");
      continue;
    }

    Diligent::IDeviceObject* object = nullptr;
    Diligent::IBuffer* buffer = nullptr;
    const Diligent::SHADER_RESOURCE_TYPE resource_type = resource->ResourceType;
    const resource_uri& uri = binding.resource.uri;

    if (resource_type == Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
      buffer_data_ref ref = gpu_data_mgr_.get_buffer(uri);
      buffer = ref.get();
      object = buffer;
      if (buffer == nullptr) {
        result.errors.push_back("buffer '" + uri.to_string() + "' is not ready");
        continue;
      }
      entry.buffer_refs.push_back(std::move(ref));
    } else if (resource_type == Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV ||
               resource_type == Diligent::SHADER_RESOURCE_TYPE_TEXTURE_UAV) {
      texture_data_ref ref = gpu_data_mgr_.get_texture(uri);
      Diligent::ITexture* texture = ref.get();
      if (texture == nullptr) {
        result.errors.push_back("texture '" + uri.to_string() + "' is not ready");
        continue;
      }
      entry.texture_refs.push_back(std::move(ref));
      object = texture->GetDefaultView(resource_type == Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV
                                           ? Diligent::TEXTURE_VIEW_SHADER_RESOURCE
                                           : Diligent::TEXTURE_VIEW_UNORDERED_ACCESS);
      if (object == nullptr) {
        result.errors.push_back("texture '" + uri.to_string() + "' has no default view");
        continue;
      }
    } else if (resource_type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV ||
               resource_type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV) {
      buffer_data_ref ref = gpu_data_mgr_.get_buffer(uri);
      buffer = ref.get();
      if (buffer == nullptr) {
        result.errors.push_back("buffer '" + uri.to_string() + "' is not ready");
        continue;
      }
      entry.buffer_refs.push_back(std::move(ref));
      object = buffer->GetDefaultView(resource_type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV
                                          ? Diligent::BUFFER_VIEW_SHADER_RESOURCE
                                          : Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
      if (object == nullptr) {
        result.errors.push_back("buffer '" + uri.to_string() + "' has no default view");
        continue;
      }
    } else {
      result.errors.push_back("variable '" + binding.variable_name + "' has an unsupported resource type");
      continue;
    }

    bool bound_any = false;
    for (Diligent::SHADER_TYPE stage : shader_stages) {
      if ((resource->ShaderStages & stage) == 0) {
        continue;
      }
      Diligent::IShaderResourceVariable* variable = p_srb->GetVariableByName(stage, binding.variable_name.c_str());
      if (variable == nullptr) {
        continue;
      }

      if (const auto* range = std::get_if<bind_buffer_range>(&binding.resource.mode)) {
        if (resource_type != Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER || buffer == nullptr) {
          result.errors.push_back("buffer ranges are only supported for constant buffers");
          bound_any = true;
          break;
        }
        const Diligent::Uint64 buffer_size = buffer->GetDesc().Size;
        if (range->offset > buffer_size) {
          result.errors.push_back("buffer range offset is out of bounds");
          bound_any = true;
          break;
        }
        const Diligent::Uint64 size = range->size == 0 ? buffer_size - range->offset : range->size;
        if (size > buffer_size - range->offset) {
          result.errors.push_back("buffer range size is out of bounds");
          bound_any = true;
          break;
        }
        variable->SetBufferRange(buffer, range->offset, size);
      } else if (const auto* element = std::get_if<resource_bind_array_element>(&binding.resource.mode)) {
        Diligent::IDeviceObject* objects[] = { object };
        variable->SetArray(objects, element->index, 1);
      } else {
        variable->Set(object);
      }
      bound_any = true;
    }

    if (!bound_any) {
      result.errors.push_back("variable '" + binding.variable_name + "' was not found in the shader stages");
    } else {
      ++result.bound;
    }
  }

  return result;
}

std::vector<Diligent::IShaderResourceVariable*>
pipeline_manager::resolve_dynamic(const binding_rule& rule,
                                  Diligent::IShaderResourceBinding* p_srb,
                                  const Diligent::PipelineResourceSignatureDesc& signature) {
  std::vector<Diligent::IShaderResourceVariable*> variables;
  if (p_srb == nullptr) {
    return variables;
  }

  for (const variable_binding& binding : rule.dynamic_bindings) {
    const Diligent::PipelineResourceDesc* resource = find_resource_desc(signature, binding.variable_name);
    if (resource == nullptr || resource->VarType != Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC) {
      continue;
    }
    for (Diligent::SHADER_TYPE stage : shader_stages) {
      if ((resource->ShaderStages & stage) == 0) {
        continue;
      }
      if (Diligent::IShaderResourceVariable* variable =
              p_srb->GetVariableByName(stage, binding.variable_name.c_str())) {
        variables.push_back(variable);
      }
    }
  }
  return variables;
}

void pipeline_manager::rebuild_srbs_for_pso(const resource_uri& pso_uri) {
  for (auto& [key, entry] : srb_map_) {
    (void)entry;
    if (key.pso_uri == pso_uri) {
      create_or_update_srb(key);
    }
  }
}

void pipeline_manager::rebuild_srbs_for_binding(const resource_uri& br_uri) {
  for (auto& [key, entry] : srb_map_) {
    (void)entry;
    if (key.binding_rule == br_uri) {
      create_or_update_srb(key);
    }
  }
}

void pipeline_manager::remove_srbs_for_pso(const resource_uri& pso_uri) {
  for (auto it = srb_map_.begin(); it != srb_map_.end();) {
    if (it->first.pso_uri == pso_uri) {
      it = srb_map_.erase(it);
    } else {
      ++it;
    }
  }
}

void pipeline_manager::remove_srbs_for_binding(const resource_uri& br_uri) {
  for (auto it = srb_map_.begin(); it != srb_map_.end();) {
    if (it->first.binding_rule == br_uri) {
      it = srb_map_.erase(it);
    } else {
      ++it;
    }
  }
}

void pipeline_manager::invalidate_uri(const resource_uri& uri) {
  if (uri.type() == resource_type::pso) {
    remove_srbs_for_pso(uri);
    pso_map_.erase(uri);
  } else if (uri.type() == resource_type::binding) {
    remove_srbs_for_binding(uri);
    binding_rule_map_.erase(uri);
  }
}

void pipeline_manager::invalidate_failed() {
  std::vector<resource_uri> failed_pso;
  std::vector<resource_uri> failed_bindings;
  for (const auto& [uri, entry] : pso_map_) {
    if (entry.state == pipeline_resource_state::failed) {
      failed_pso.push_back(uri);
    }
  }
  for (const auto& [uri, entry] : binding_rule_map_) {
    if (entry.state == pipeline_resource_state::failed) {
      failed_bindings.push_back(uri);
    }
  }
  for (const resource_uri& uri : failed_pso) {
    remove_srbs_for_pso(uri);
    pso_map_.erase(uri);
  }
  for (const resource_uri& uri : failed_bindings) {
    remove_srbs_for_binding(uri);
    binding_rule_map_.erase(uri);
  }
  for (auto it = srb_map_.begin(); it != srb_map_.end();) {
    it = it->second.state == pipeline_resource_state::failed ? srb_map_.erase(it) : std::next(it);
  }
}

void pipeline_manager::release_all() noexcept {
  task value;
  while (tq_.try_pop(value)) {
  }
  clear_all();
}

void pipeline_manager::clear_all() {
  srb_map_.clear();
  pso_map_.clear();
  binding_rule_map_.clear();
}

bool pipeline_manager::contains(const resource_uri& uri) const noexcept {
  return pso_map_.contains(uri) || binding_rule_map_.contains(uri);
}

pipeline_resource_status pipeline_manager::get_state(const resource_uri& uri) const {
  if (const auto pso = pso_map_.find(uri); pso != pso_map_.end()) {
    return { pso->second.state, pso->second.error };
  }
  if (const auto rule = binding_rule_map_.find(uri); rule != binding_rule_map_.end()) {
    return { rule->second.state, rule->second.error };
  }
  return { pipeline_resource_state::failed, "resource is not registered" };
}

pipeline_resource_status pipeline_manager::get_srb_state(const pipeline_srb_key& key) const {
  const auto found = srb_map_.find(key);
  if (found == srb_map_.end()) {
    return { pipeline_resource_state::failed, "SRB is not registered" };
  }
  return { found->second.state, found->second.error };
}

Diligent::IPipelineState* pipeline_manager::get_pso(const resource_uri& uri) const noexcept {
  const auto found = pso_map_.find(uri);
  if (found == pso_map_.end() || found->second.state != pipeline_resource_state::ready) {
    return nullptr;
  }
  return found->second.p_pso.RawPtr();
}

const binding_rule* pipeline_manager::get_binding_rule(const resource_uri& uri) const noexcept {
  const auto found = binding_rule_map_.find(uri);
  return found != binding_rule_map_.end() ? &found->second.rule : nullptr;
}

Diligent::IShaderResourceBinding* pipeline_manager::get_srb(const pipeline_srb_key& key) const noexcept {
  const auto found = srb_map_.find(key);
  if (found == srb_map_.end() || found->second.state != pipeline_resource_state::ready) {
    return nullptr;
  }
  return found->second.p_srb.RawPtr();
}

std::size_t pipeline_manager::get_srb_count(const resource_uri& pso_uri, const resource_uri& br_uri) const noexcept {
  std::size_t count = 0;
  for (const auto& [key, entry] : srb_map_) {
    if (key.pso_uri == pso_uri && key.binding_rule == br_uri) {
      ++count;
    }
  }
  return count;
}

pipeline_manager_stats pipeline_manager::get_stats() const noexcept {
  pipeline_manager_stats stats;
  stats.pso_count = pso_map_.size();
  for (const auto& [uri, entry] : pso_map_) {
    (void)uri;
    switch (entry.state) {
    case pipeline_resource_state::ready:
      ++stats.pso_ready;
      break;
    case pipeline_resource_state::pending:
      ++stats.pso_pending;
      break;
    case pipeline_resource_state::failed:
      ++stats.pso_failed;
      break;
    }
  }
  stats.binding_rule_count = binding_rule_map_.size();
  for (const auto& [uri, entry] : binding_rule_map_) {
    (void)uri;
    switch (entry.state) {
    case pipeline_resource_state::ready:
      ++stats.binding_rule_ready;
      break;
    case pipeline_resource_state::pending:
      ++stats.binding_rule_pending;
      break;
    case pipeline_resource_state::failed:
      ++stats.binding_rule_failed;
      break;
    }
  }
  stats.srb_count = srb_map_.size();
  for (const auto& [key, entry] : srb_map_) {
    (void)key;
    switch (entry.state) {
    case pipeline_resource_state::ready:
      ++stats.srb_ready;
      break;
    case pipeline_resource_state::pending:
      ++stats.srb_pending;
      break;
    case pipeline_resource_state::failed:
      ++stats.srb_failed;
      break;
    }
  }
  return stats;
}

} // namespace render_core
} // namespace cyberverse
