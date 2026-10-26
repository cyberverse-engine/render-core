/**
 * @file pipeline_variant.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline state variant cache
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>
#include <unordered_map>

#include "cyberverse/render_core/pipeline_desc.hpp"
#include "cyberverse/render_core/pipeline_manager.hpp"
#include "cyberverse/render_core/resource_uri.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Render-state variant bits
 *
 */
enum class pipeline_variant_flag : std::uint32_t {
  none = 0,
  transparent = 1u << 0,   ///< Alpha blending, depth test, no depth write
  shadow = 1u << 1,        ///< Depth-only shadow pass
  depth_prepass = 1u << 2, ///< Depth-only pre-pass
};

using pipeline_variant_flags = std::uint32_t;

[[nodiscard]] constexpr pipeline_variant_flags operator|(pipeline_variant_flag lhs,
                                                         pipeline_variant_flag rhs) noexcept {
  return static_cast<pipeline_variant_flags>(lhs) | static_cast<pipeline_variant_flags>(rhs);
}

[[nodiscard]] constexpr pipeline_variant_flags operator|(pipeline_variant_flags lhs,
                                                         pipeline_variant_flag rhs) noexcept {
  return lhs | static_cast<pipeline_variant_flags>(rhs);
}

/**
 * @brief Identity of a cached PSO variant
 *
 * The key combines the base PSO, render-state flags, shader permutation hash,
 * and input-layout hash. Equal keys reuse the same derived PSO URI.
 *
 */
struct pipeline_variant_key {
  resource_uri base_pso;
  pipeline_variant_flags flags = 0;
  std::size_t shader_hash = 0;
  std::size_t input_layout_hash = 0;

  bool operator==(const pipeline_variant_key&) const noexcept = default;
};

/**
 * @brief Hash for pipeline_variant_key
 *
 */
struct pipeline_variant_key_hash {
  std::size_t operator()(const pipeline_variant_key& key) const noexcept;
};

/**
 * @brief Base description and format overrides for a variant
 *
 */
struct pipeline_variant_desc {
  pipeline_desc base;
  shader_defines defines;
  Diligent::TEXTURE_FORMAT rtv_format = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::TEXTURE_FORMAT dsv_format = Diligent::TEX_FORMAT_UNKNOWN;
};

/**
 * @brief Creates and caches PSO variants derived from a base description
 *
 * The cache applies transparent/shadow/depth-prepass graphics state, appends
 * variant macros to every shader, derives a stable PSO URI from the variant key,
 * and forwards the result to pipeline_manager::request_create_pso. It does not
 * perform reflection; callers provide the shader/input-layout hashes that form
 * the cache key.
 *
 */
class pipeline_variant_cache {
public:
  explicit pipeline_variant_cache(pipeline_manager& pipelines) noexcept;

  pipeline_variant_cache(const pipeline_variant_cache&) = delete;
  pipeline_variant_cache(pipeline_variant_cache&&) = delete;
  pipeline_variant_cache& operator=(const pipeline_variant_cache&) = delete;
  pipeline_variant_cache& operator=(pipeline_variant_cache&&) = delete;

  /**
   * @brief Request a variant PSO
   *
   * @param key Variant identity
   * @param desc Base description and format overrides
   * @return std::expected<resource_uri, std::error_code> Derived PSO URI
   */
  [[nodiscard]] std::expected<resource_uri, std::error_code> request(const pipeline_variant_key& key,
                                                                     pipeline_variant_desc desc);

  /**
   * @brief Look up an already requested variant
   *
   * @param key Variant identity
   * @return const resource_uri* Null when absent
   */
  [[nodiscard]] const resource_uri* find(const pipeline_variant_key& key) const noexcept;

  /**
   * @brief Number of cached variants
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t size() const noexcept { return variants_.size(); }

  /**
   * @brief Clear the cache; PSOs remain owned by pipeline_manager
   *
   */
  void clear() noexcept { variants_.clear(); }

private:
  pipeline_manager* pipelines_ = nullptr;
  std::unordered_map<pipeline_variant_key, resource_uri, pipeline_variant_key_hash> variants_;
};

} // namespace render_core
} // namespace cyberverse
