/**
 * @file pipeline_variant.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline state variant cache implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/pipeline_variant.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace cyberverse {
namespace render_core {

namespace {

std::string variant_uri_text(const pipeline_variant_key& key) {
  constexpr char digits[] = "0123456789abcdef";
  std::size_t value = key.flags;
  value ^= key.shader_hash + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
  value ^= key.input_layout_hash + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);

  std::array<char, 16> suffix{};
  for (std::size_t index = 0; index < suffix.size(); ++index) {
    suffix[suffix.size() - 1 - index] = digits[value & 0xfu];
    value >>= 4;
  }
  return "@pso/" + std::string{ key.base_pso.org() } + "/" + std::string{ key.base_pso.path() } + "/variant-" +
         std::string{ suffix.data(), suffix.size() };
}

void apply_variant_state(pipeline_variant_desc& desc, pipeline_variant_flags flags) {
  auto& graphics = desc.base.graphics_desc;
  const bool transparent = (flags & static_cast<pipeline_variant_flags>(pipeline_variant_flag::transparent)) != 0;
  const bool depth_only = (flags & static_cast<pipeline_variant_flags>(pipeline_variant_flag::shadow)) != 0 ||
                          (flags & static_cast<pipeline_variant_flags>(pipeline_variant_flag::depth_prepass)) != 0;

  if (transparent) {
    auto& target = graphics.BlendDesc.RenderTargets[0];
    target.BlendEnable = Diligent::True;
    target.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    target.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    target.BlendOp = Diligent::BLEND_OPERATION_ADD;
    target.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    target.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    target.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    graphics.DepthStencilDesc.DepthWriteEnable = Diligent::False;
  }

  if (depth_only) {
    graphics.NumRenderTargets = 0;
    for (Diligent::TEXTURE_FORMAT& format : graphics.RTVFormats) {
      format = Diligent::TEX_FORMAT_UNKNOWN;
    }
    if (desc.dsv_format != Diligent::TEX_FORMAT_UNKNOWN) {
      graphics.DSVFormat = desc.dsv_format;
    }
    graphics.DepthStencilDesc.DepthEnable = Diligent::True;
    graphics.DepthStencilDesc.DepthWriteEnable = Diligent::True;
  } else if (desc.rtv_format != Diligent::TEX_FORMAT_UNKNOWN) {
    graphics.RTVFormats[0] = desc.rtv_format;
  }

  for (pipeline_shader& shader : desc.base.shaders) {
    for (const shader_define& define : desc.defines) {
      shader.key.defines.push_back(define);
    }
    if (transparent) {
      shader.key.defines.emplace_back("CYBERVERSE_TRANSPARENT", "1");
    }
    if ((flags & static_cast<pipeline_variant_flags>(pipeline_variant_flag::shadow)) != 0) {
      shader.key.defines.emplace_back("CYBERVERSE_SHADOW", "1");
    }
    if ((flags & static_cast<pipeline_variant_flags>(pipeline_variant_flag::depth_prepass)) != 0) {
      shader.key.defines.emplace_back("CYBERVERSE_DEPTH_PREPASS", "1");
    }
    shader.key = normalize_shader_key(std::move(shader.key));
  }
}

} // namespace

std::size_t pipeline_variant_key_hash::operator()(const pipeline_variant_key& key) const noexcept {
  std::size_t seed = key.base_pso.hash();
  seed ^= static_cast<std::size_t>(key.flags) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  seed ^= key.shader_hash + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  seed ^= key.input_layout_hash + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
  return seed;
}

pipeline_variant_cache::pipeline_variant_cache(pipeline_manager& pipelines) noexcept : pipelines_{ &pipelines } {}

std::expected<resource_uri, std::error_code> pipeline_variant_cache::request(const pipeline_variant_key& key,
                                                                             pipeline_variant_desc desc) {
  if (pipelines_ == nullptr || !key.base_pso.is_valid() || key.base_pso.type() != resource_type::pso) {
    return std::unexpected(make_error_code(render_core_errc::invalid_argument));
  }

  const auto existing = variants_.find(key);
  if (existing != variants_.end()) {
    return existing->second;
  }

  auto uri = make_resource_uri(variant_uri_text(key));
  if (!uri.has_value()) {
    return std::unexpected(uri.error());
  }

  apply_variant_state(desc, key.flags);
  if (!pipelines_->contains(*uri)) {
    auto result = pipelines_->request_create_pso(*uri, std::move(desc.base));
    if (!result.has_value()) {
      return std::unexpected(result.error());
    }
  }

  variants_.emplace(key, *uri);
  return *uri;
}

const resource_uri* pipeline_variant_cache::find(const pipeline_variant_key& key) const noexcept {
  const auto found = variants_.find(key);
  return found != variants_.end() ? &found->second : nullptr;
}

} // namespace render_core
} // namespace cyberverse
