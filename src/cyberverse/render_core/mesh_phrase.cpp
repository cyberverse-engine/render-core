/**
 * @file mesh_phrase.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Built-in mesh render phrases implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/mesh_phrase.hpp"

#include <stdexcept>
#include <utility>

#include "cyberverse/render_core/render_device.hpp"

namespace cyberverse {
namespace render_core {

namespace {

bool clears_color(phrase_clears clears) noexcept {
  return clears == phrase_clears::color || clears == phrase_clears::color_and_depth;
}

bool clears_depth(phrase_clears clears) noexcept {
  return clears == phrase_clears::depth || clears == phrase_clears::color_and_depth;
}

Diligent::ITextureView* select_color_target(const frame_context& ctx, phrase_targets targets) noexcept {
  return targets == phrase_targets::depth_only ? nullptr : ctx.color_target();
}

Diligent::ITextureView* select_depth_target(const frame_context& ctx, phrase_targets targets) noexcept {
  if (targets == phrase_targets::color_depth_read_only) {
    return ctx.depth_read_only();
  }
  return ctx.depth_target();
}

mesh_phrase_desc make_desc(Diligent::IPipelineState* pipeline_state,
                           Diligent::IShaderResourceBinding* srb,
                           phrase_targets targets,
                           phrase_clears clears) noexcept {
  mesh_phrase_desc desc;
  desc.pipeline_state = pipeline_state;
  desc.srb = srb;
  desc.targets = targets;
  desc.clears = clears;
  return desc;
}

mesh_phrase_desc make_managed_desc(resource_uri pso_uri,
                                   resource_uri binding_rule_uri,
                                   signature_index_type signature_index,
                                   phrase_targets targets,
                                   phrase_clears clears) {
  mesh_phrase_desc desc;
  desc.pso_uri = std::move(pso_uri);
  desc.binding_rule_uri = std::move(binding_rule_uri);
  desc.signature_index = signature_index;
  desc.targets = targets;
  desc.clears = clears;
  return desc;
}

void validate_managed_desc(const mesh_phrase_desc& desc) {
  if (!desc.pso_uri.is_valid() || desc.pso_uri.type() != resource_type::pso) {
    throw std::invalid_argument("mesh_phrase managed PSO URI must be a valid @pso URI");
  }
  if (!desc.binding_rule_uri.is_valid() || desc.binding_rule_uri.type() != resource_type::binding) {
    throw std::invalid_argument("mesh_phrase managed binding rule URI must be a valid @binding URI");
  }
  if (desc.signature_index < anonymous_signature_index) {
    throw std::invalid_argument("mesh_phrase signature index must be -1 or non-negative");
  }
}

} // namespace

mesh_phrase::mesh_phrase(std::string_view name, phrase_sort sort, const mesh_phrase_desc& desc)
    : name_{ name }
    , sort_{ sort }
    , targets_{ desc.targets }
    , clear_color_enabled_{ clears_color(desc.clears) }
    , clear_depth_enabled_{ clears_depth(desc.clears) }
    , clear_color_value_{ desc.clear_color }
    , clear_depth_value_{ desc.clear_depth }
    , clear_stencil_value_{ desc.clear_stencil } {
  const bool has_managed_config = desc.pso_uri.is_valid() || desc.binding_rule_uri.is_valid();
  if (!has_managed_config) {
    if (desc.signature_index < anonymous_signature_index) {
      throw std::invalid_argument("mesh_phrase signature index must be -1 or non-negative");
    }
    pipeline_mode_ = mesh_pipeline_mode::unmanaged;
    pipeline_state_ = desc.pipeline_state;
    srb_ = desc.srb;
    return;
  }

  if (desc.pipeline_state != nullptr || desc.srb != nullptr) {
    throw std::invalid_argument("mesh_phrase cannot mix direct pipelines with managed URIs");
  }
  validate_managed_desc(desc);
  pipeline_mode_ = mesh_pipeline_mode::managed;
  pso_uri_ = desc.pso_uri;
  binding_rule_uri_ = desc.binding_rule_uri;
  signature_index_ = desc.signature_index;
}

void mesh_phrase::begin(const frame_context& ctx) noexcept {
  active_ = false;
  Diligent::IDeviceContext* context = ctx.device_context();
  if (context == nullptr) {
    return;
  }

  Diligent::ITextureView* color = select_color_target(ctx, targets_);
  Diligent::ITextureView* depth = select_depth_target(ctx, targets_);
  if (color == nullptr && depth == nullptr) {
    return;
  }

  Diligent::ITextureView* targets[1] = { color };
  context->SetRenderTargets(
      color != nullptr ? 1u : 0u, targets, depth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  const viewport& vp = ctx.current_viewport();
  if (vp.is_valid()) {
    const Diligent::Viewport diligent_vp = vp.to_diligent();
    context->SetViewports(1, &diligent_vp, 0, 0);
  }

  if (clear_color_enabled_ && color != nullptr) {
    context->ClearRenderTarget(color, clear_color_value_.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
  if (clear_depth_enabled_ && depth != nullptr) {
    context->ClearDepthStencil(depth,
                               Diligent::CLEAR_DEPTH_FLAG,
                               clear_depth_value_,
                               clear_stencil_value_,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }

  active_ = true;
}

void mesh_phrase::execute(const frame_context& ctx, std::span<const render_packet> items) noexcept {
  Diligent::IDeviceContext* context = ctx.device_context();
  if (!active_ || context == nullptr || items.empty()) {
    return;
  }

  Diligent::IPipelineState* pipeline_state = nullptr;
  Diligent::IShaderResourceBinding* srb = nullptr;
  if (pipeline_mode_ == mesh_pipeline_mode::managed) {
    if (!ctx.has_device()) {
      return;
    }
    pipeline_manager& pipelines = ctx.device().pipelines();
    pipeline_state = pipelines.get_pso(pso_uri_);
    if (pipeline_state == nullptr) {
      return;
    }
    srb = pipelines.get_srb(pipeline_srb_key{ pso_uri_, binding_rule_uri_, signature_index_ });
    if (srb == nullptr) {
      return;
    }
  } else {
    pipeline_state = pipeline_state_;
    srb = srb_;
  }

  if (pipeline_state == nullptr) {
    return;
  }

  Diligent::IPipelineState* active_pso = nullptr;
  Diligent::IShaderResourceBinding* active_srb = nullptr;
  const Diligent::IShaderResourceBinding* const* active_srb_set = nullptr;
  std::size_t active_srb_count = 0;
  if (pipeline_state != nullptr) {
    context->SetPipelineState(pipeline_state);
    active_pso = pipeline_state;
  }
  if (ctx.camera_srb() != nullptr) {
    context->CommitShaderResources(ctx.camera_srb(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    active_srb = ctx.camera_srb();
  }
  if (srb != nullptr && srb != ctx.camera_srb()) {
    context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    active_srb = srb;
  }

  const std::uint64_t offset = 0;
  for (const render_packet& item : items) {
    if (!item.is_valid()) {
      continue;
    }

    Diligent::IPipelineState* item_pso = item.pipeline_state != nullptr ? item.pipeline_state : pipeline_state;
    if (item_pso != nullptr && item_pso != active_pso) {
      context->SetPipelineState(item_pso);
      active_pso = item_pso;
    }
    if (!item.srbs.empty()) {
      if (active_srb_set != item.srbs.data() || active_srb_count != item.srbs.size()) {
        for (Diligent::IShaderResourceBinding* binding : item.srbs) {
          if (binding != nullptr) {
            context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          }
        }
        active_srb_set = item.srbs.data();
        active_srb_count = item.srbs.size();
        active_srb = nullptr;
      }
    } else {
      Diligent::IShaderResourceBinding* item_srb = item.srb != nullptr ? item.srb : srb;
      if (item_srb != nullptr && item_srb != active_srb) {
        context->CommitShaderResources(item_srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        active_srb = item_srb;
        active_srb_set = nullptr;
        active_srb_count = 0;
      }
    }
    for (const render_packet_dynamic_binding& binding : item.dynamic_bindings) {
      if (binding.variable != nullptr && binding.object != nullptr) {
        binding.variable->Set(binding.object);
      }
    }

    if (item.vertex_buffer != nullptr) {
      Diligent::IBuffer* buffers[1] = { item.vertex_buffer };
      context->SetVertexBuffers(0,
                                1,
                                buffers,
                                &offset,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    }

    if (item.instance_buffer != nullptr) {
      Diligent::IBuffer* buffers[1] = { item.instance_buffer };
      context->SetVertexBuffers(1,
                                1,
                                buffers,
                                &offset,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                Diligent::SET_VERTEX_BUFFERS_FLAG_NONE);
    }

    if (item.index_buffer != nullptr) {
      context->SetIndexBuffer(item.index_buffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      Diligent::DrawIndexedAttribs attribs;
      attribs.NumIndices = item.index_count;
      attribs.IndexType = item.index_type;
      attribs.NumInstances = item.effective_instance_count();
      attribs.FirstIndexLocation = item.first_index;
      attribs.BaseVertex = item.base_vertex;
      attribs.FirstInstanceLocation = item.first_instance;
      context->DrawIndexed(attribs);
    } else if (item.vertex_buffer != nullptr) {
      Diligent::DrawAttribs attribs;
      attribs.NumVertices = item.vertex_count;
      attribs.NumInstances = item.effective_instance_count();
      attribs.StartVertexLocation = 0;
      attribs.FirstInstanceLocation = item.first_instance;
      context->Draw(attribs);
    }
  }
}

void mesh_phrase::end(const frame_context& /*ctx*/) noexcept { active_ = false; }

opaque_phrase::opaque_phrase(Diligent::IPipelineState* pipeline_state, Diligent::IShaderResourceBinding* srb)
    : mesh_phrase{ "opaque",
                   phrase_sort::front_to_back,
                   make_desc(pipeline_state, srb, phrase_targets::color_and_depth, phrase_clears::color_and_depth) } {}

opaque_phrase::opaque_phrase(resource_uri pso_uri, resource_uri binding_rule_uri, signature_index_type signature_index)
    : mesh_phrase{ "opaque",
                   phrase_sort::front_to_back,
                   make_managed_desc(std::move(pso_uri),
                                     std::move(binding_rule_uri),
                                     signature_index,
                                     phrase_targets::color_and_depth,
                                     phrase_clears::color_and_depth) } {}

transparent_phrase::transparent_phrase(Diligent::IPipelineState* pipeline_state, Diligent::IShaderResourceBinding* srb)
    : mesh_phrase{ "transparent",
                   phrase_sort::back_to_front,
                   make_desc(pipeline_state, srb, phrase_targets::color_depth_read_only, phrase_clears::none) } {}

transparent_phrase::transparent_phrase(resource_uri pso_uri,
                                       resource_uri binding_rule_uri,
                                       signature_index_type signature_index)
    : mesh_phrase{ "transparent",
                   phrase_sort::back_to_front,
                   make_managed_desc(std::move(pso_uri),
                                     std::move(binding_rule_uri),
                                     signature_index,
                                     phrase_targets::color_depth_read_only,
                                     phrase_clears::none) } {}

shadow_phrase::shadow_phrase(Diligent::IPipelineState* pipeline_state, Diligent::IShaderResourceBinding* srb)
    : mesh_phrase{ "shadow",
                   phrase_sort::front_to_back,
                   make_desc(pipeline_state, srb, phrase_targets::depth_only, phrase_clears::depth) } {}

shadow_phrase::shadow_phrase(resource_uri pso_uri, resource_uri binding_rule_uri, signature_index_type signature_index)
    : mesh_phrase{ "shadow",
                   phrase_sort::front_to_back,
                   make_managed_desc(std::move(pso_uri),
                                     std::move(binding_rule_uri),
                                     signature_index,
                                     phrase_targets::depth_only,
                                     phrase_clears::depth) } {}

} // namespace render_core
} // namespace cyberverse
