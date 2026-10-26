/**
 * @file mesh_phrase.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Built-in mesh render phrases
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "cyberverse/render_core/render_phrase.hpp"
#include "cyberverse/render_core/resource_uri.hpp"
#include "cyberverse/render_core/signature_index.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Render targets bound by a mesh phrase
 *
 */
enum class phrase_targets {
  color_and_depth,       ///< Opaque geometry
  color_depth_read_only, ///< Transparent geometry
  depth_only,            ///< Shadow map
};

/**
 * @brief Targets cleared by a mesh phrase
 *
 */
enum class phrase_clears {
  none,
  color,
  depth,
  color_and_depth,
};

/**
 * @brief How a mesh phrase obtains its pipeline state and SRB
 *
 */
enum class mesh_pipeline_mode {
  unmanaged, ///< Directly owned PSO/SRB pointers
  managed,   ///< Resolved through pipeline_manager from URIs
};

/**
 * @brief Mesh phrase configuration
 *
 * Configure either the direct pipeline pointers (unmanaged mode) or the managed
 * URI fields (managed mode). Mixing both is invalid.
 *
 */
struct mesh_phrase_desc {
  // Unmanaged mode: direct pointers, may be null for tests
  Diligent::IPipelineState* pipeline_state = nullptr; ///< Pipeline state
  Diligent::IShaderResourceBinding* srb = nullptr;    ///< Static SRB, optional

  // Managed mode: resolved from the render device's pipeline manager
  resource_uri pso_uri;                                             ///< Managed PSO URI
  resource_uri binding_rule_uri;                                    ///< Managed binding rule URI
  signature_index_type signature_index = anonymous_signature_index; ///< Selected SRB signature

  phrase_targets targets = phrase_targets::color_and_depth;   ///< Render target set
  phrase_clears clears = phrase_clears::none;                 ///< Targets to clear on begin
  std::array<float, 4> clear_color{ 0.0f, 0.0f, 0.0f, 1.0f }; ///< Color clear value
  float clear_depth = 1.0f;                                   ///< Depth clear value
  std::uint8_t clear_stencil = 0;                             ///< Stencil clear value
};

/**
 * @brief Draws mesh items with one pipeline state and SRB
 *
 * The phrase binds the pipeline state, the frame camera SRB (when supplied), an
 * SRB, per-item vertex/index/instance buffers, and issues indexed or
 * non-indexed instanced draws. Per-packet PSO/SRB overrides, ordered SRB sets,
 * and dynamic variable/object bindings are applied before each draw. It is
 * either unmanaged (direct PSO/SRB pointers) or managed (PSO URI, binding rule
 * URI, and signature index); the mode is fixed at construction.
 *
 * Every Diligent call is null-checked so an empty or partially initialized
 * context is a no-op rather than a crash. This is what lets unit tests exercise
 * the scheduling layer without a GPU.
 *
 */
class mesh_phrase : public render_phrase {
public:
  /**
   * @brief Construct a mesh phrase
   *
   * @param name Stable name
   * @param sort Item sort order
   * @param desc Pipeline and target configuration
   * @throws std::invalid_argument If managed URIs and direct pointers are mixed,
   *         a managed URI has the wrong type, or signature_index is less than -1
   */
  mesh_phrase(std::string_view name, phrase_sort sort, const mesh_phrase_desc& desc);

  ~mesh_phrase() override = default;

  [[nodiscard]] std::string_view name() const noexcept override { return name_; }

  [[nodiscard]] phrase_sort sort_mode() const noexcept override { return sort_; }

  [[nodiscard]] bool enabled() const noexcept override { return enabled_; }

  void begin(const frame_context& ctx) noexcept override;

  void execute(const frame_context& ctx, std::span<const render_packet> items) noexcept override;

  void end(const frame_context& ctx) noexcept override;

  /**
   * @brief Enable or disable the phrase
   *
   * @param value Enabled state
   */
  void set_enabled(bool value) noexcept { enabled_ = value; }

  /**
   * @brief Replace the pipeline state
   *
   * @param pipeline_state Pipeline state
   */
  void set_pipeline_state(Diligent::IPipelineState* pipeline_state) noexcept {
    if (pipeline_mode_ == mesh_pipeline_mode::unmanaged) {
      pipeline_state_ = pipeline_state;
    }
  }

  /**
   * @brief Replace both direct pipeline pointers
   *
   * Only affects unmanaged phrases. Managed phrases keep their URI configuration.
   *
   * @param pipeline_state Pipeline state
   * @param srb Shader resource binding
   */
  void set_direct_pipeline(Diligent::IPipelineState* pipeline_state,
                           Diligent::IShaderResourceBinding* srb = nullptr) noexcept {
    if (pipeline_mode_ == mesh_pipeline_mode::unmanaged) {
      pipeline_state_ = pipeline_state;
      srb_ = srb;
    }
  }

  /**
   * @brief Pipeline state
   *
   * Returns null for managed phrases.
   *
   * @return Diligent::IPipelineState*
   */
  [[nodiscard]] Diligent::IPipelineState* pipeline_state() const noexcept { return pipeline_state_; }

  /**
   * @brief Replace the static shader resource binding
   *
   * Only affects unmanaged phrases. Managed phrases keep their URI configuration.
   *
   * @param srb Shader resource binding
   */
  void set_shader_resource_binding(Diligent::IShaderResourceBinding* srb) noexcept {
    if (pipeline_mode_ == mesh_pipeline_mode::unmanaged) {
      srb_ = srb;
    }
  }

  /**
   * @brief Static shader resource binding
   *
   * Returns null for managed phrases.
   *
   * @return Diligent::IShaderResourceBinding*
   */
  [[nodiscard]] Diligent::IShaderResourceBinding* shader_resource_binding() const noexcept { return srb_; }

  /**
   * @brief Pipeline source mode, fixed at construction
   *
   * @return mesh_pipeline_mode
   */
  [[nodiscard]] mesh_pipeline_mode pipeline_mode() const noexcept { return pipeline_mode_; }

  /**
   * @brief Whether this phrase uses managed URIs
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_managed() const noexcept { return pipeline_mode_ == mesh_pipeline_mode::managed; }

  /**
   * @brief Managed PSO URI
   *
   * @return const resource_uri&
   */
  [[nodiscard]] const resource_uri& pso_uri() const noexcept { return pso_uri_; }

  /**
   * @brief Managed binding rule URI
   *
   * @return const resource_uri&
   */
  [[nodiscard]] const resource_uri& binding_rule_uri() const noexcept { return binding_rule_uri_; }

  /**
   * @brief Managed signature index; -1 selects the anonymous signature
   *
   * @return signature_index_type
   */
  [[nodiscard]] signature_index_type signature_index() const noexcept { return signature_index_; }

  /**
   * @brief Render targets bound by this phrase
   *
   * @return phrase_targets
   */
  [[nodiscard]] phrase_targets targets() const noexcept { return targets_; }

protected:
  std::string name_;
  phrase_sort sort_ = phrase_sort::none;
  bool enabled_ = true;

  mesh_pipeline_mode pipeline_mode_ = mesh_pipeline_mode::unmanaged;

  Diligent::IPipelineState* pipeline_state_ = nullptr;
  Diligent::IShaderResourceBinding* srb_ = nullptr;

  resource_uri pso_uri_;
  resource_uri binding_rule_uri_;
  signature_index_type signature_index_ = anonymous_signature_index;

  phrase_targets targets_ = phrase_targets::color_and_depth;
  bool clear_color_enabled_ = false;
  bool clear_depth_enabled_ = false;
  std::array<float, 4> clear_color_value_{ 0.0f, 0.0f, 0.0f, 1.0f };
  float clear_depth_value_ = 1.0f;
  std::uint8_t clear_stencil_value_ = 0;

  bool active_ = false;
};

/**
 * @brief Opaque geometry phrase
 *
 * Front-to-back, color and depth write, clears color and depth.
 *
 */
class opaque_phrase final : public mesh_phrase {
public:
  explicit opaque_phrase(Diligent::IPipelineState* pipeline_state = nullptr,
                         Diligent::IShaderResourceBinding* srb = nullptr);

  opaque_phrase(resource_uri pso_uri,
                resource_uri binding_rule_uri,
                signature_index_type signature_index = anonymous_signature_index);
};

/**
 * @brief Transparent geometry phrase
 *
 * Back-to-front, depth test with a read-only depth target, no clears.
 *
 */
class transparent_phrase final : public mesh_phrase {
public:
  explicit transparent_phrase(Diligent::IPipelineState* pipeline_state = nullptr,
                              Diligent::IShaderResourceBinding* srb = nullptr);

  transparent_phrase(resource_uri pso_uri,
                     resource_uri binding_rule_uri,
                     signature_index_type signature_index = anonymous_signature_index);
};

/**
 * @brief Shadow-map phrase
 *
 * Depth-only target, front-to-back, clears depth.
 *
 */
class shadow_phrase final : public mesh_phrase {
public:
  explicit shadow_phrase(Diligent::IPipelineState* pipeline_state = nullptr,
                         Diligent::IShaderResourceBinding* srb = nullptr);

  shadow_phrase(resource_uri pso_uri,
                resource_uri binding_rule_uri,
                signature_index_type signature_index = anonymous_signature_index);
};

} // namespace render_core
} // namespace cyberverse
