/**
 * @file render_packet.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render packet
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

#include <RenderDevice.h>
#include <ShaderResourceVariable.h>

namespace cyberverse {
namespace render_core {

/**
 * @brief One draw submission
 *
 * A render packet is a value-owned description of one draw call. Buffers are
 * owned by the caller (usually gpu_data_manager) and must stay alive while the
 * packet is queued. Packets are sorted by the owning render phrase before
 * execution.
 *
 * sort_key is a phase-independent distance key. Opaque phrases sort it
 * front-to-back; transparent phrases sort it back-to-front. Ties fall back to
 * material and geometry fields so the order is deterministic.
 *
 */
/**
 * @brief Per-packet dynamic shader resource binding
 *
 * The caller resolves the variable from the packet's SRB and keeps the object
 * alive. The phrase applies the binding after committing the SRB.
 *
 */
struct render_packet_dynamic_binding {
  Diligent::IShaderResourceVariable* variable = nullptr; ///< Dynamic SRB variable
  Diligent::IDeviceObject* object = nullptr;             ///< Texture/buffer view to bind
};

struct render_packet {
  float sort_key = 0.0f; ///< Camera distance (squared or linear, caller's choice)

  Diligent::IPipelineState* pipeline_state = nullptr;      ///< Optional per-packet PSO override
  Diligent::IShaderResourceBinding* srb = nullptr;         ///< Optional per-packet SRB override
  std::span<Diligent::IShaderResourceBinding* const> srbs; ///< Optional ordered SRB set (camera/material/instance)
  std::span<const render_packet_dynamic_binding> dynamic_bindings; ///< Optional per-packet dynamic bindings

  Diligent::IBuffer* instance_buffer = nullptr; ///< Optional instance data
  std::uint32_t instance_count = 0;             ///< Instance count, 0 means one
  std::uint32_t first_instance = 0;             ///< First instance location

  std::uint32_t texture_index = 0;  ///< Texture array slice used by the material
  std::uint32_t material_index = 0; ///< Material/batch index

  Diligent::IBuffer* vertex_buffer = nullptr; ///< Vertex buffer, required for non-indexed draws
  std::uint32_t vertex_count = 0;             ///< Vertex count for non-indexed draws

  Diligent::IBuffer* index_buffer = nullptr;             ///< Optional index buffer
  std::uint32_t index_count = 0;                         ///< Index count
  std::uint32_t first_index = 0;                         ///< First index location
  std::uint32_t base_vertex = 0;                         ///< Base vertex added to each index
  Diligent::VALUE_TYPE index_type = Diligent::VT_UINT32; ///< Index value type

  bool visible = true; ///< Invisible packets are skipped by the phrase

  /**
   * @brief Whether the packet describes a drawable range
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_valid() const noexcept {
    if (!visible) {
      return false;
    }
    if (index_buffer != nullptr) {
      return index_count > 0;
    }
    return vertex_buffer != nullptr && vertex_count > 0;
  }

  /**
   * @brief Effective instance count
   *
   * @return std::uint32_t
   */
  [[nodiscard]] std::uint32_t effective_instance_count() const noexcept {
    return instance_count == 0 ? 1u : instance_count;
  }
};

namespace detail {

/**
 * @brief Deterministic tie-break shared by both sort directions
 *
 * @return true if a sorts before b when keys are equal
 */
inline bool render_packet_tie_less(const render_packet& a, const render_packet& b) noexcept {
  if (a.material_index != b.material_index) {
    return a.material_index < b.material_index;
  }
  if (a.texture_index != b.texture_index) {
    return a.texture_index < b.texture_index;
  }
  if (a.first_index != b.first_index) {
    return a.first_index < b.first_index;
  }
  if (a.index_count != b.index_count) {
    return a.index_count < b.index_count;
  }
  return a.instance_count < b.instance_count;
}

} // namespace detail

/**
 * @brief Front-to-back comparator for opaque geometry
 *
 */
struct render_packet_front_to_back {
  bool operator()(const render_packet& a, const render_packet& b) const noexcept {
    if (a.sort_key < b.sort_key) {
      return true;
    }
    if (b.sort_key < a.sort_key) {
      return false;
    }
    return detail::render_packet_tie_less(a, b);
  }
};

/**
 * @brief Back-to-front comparator for transparent geometry
 *
 */
struct render_packet_back_to_front {
  bool operator()(const render_packet& a, const render_packet& b) const noexcept {
    if (a.sort_key > b.sort_key) {
      return true;
    }
    if (b.sort_key > a.sort_key) {
      return false;
    }
    return detail::render_packet_tie_less(a, b);
  }
};

/**
 * @brief Squared distance helper
 *
 * @param x Camera-space x
 * @param y Camera-space y
 * @param z Camera-space z
 * @return float
 */
[[nodiscard]] inline float distance_squared_key(float x, float y, float z) noexcept { return x * x + y * y + z * z; }

} // namespace render_core
} // namespace cyberverse
