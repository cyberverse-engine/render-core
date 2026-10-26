/**
 * @file binding_rule.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Binding plan
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <string>
#include <vector>

#include "cyberverse/render_core/resource_ref.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief A shader variable bound to a resource URI
 *
 */
struct variable_binding {
  std::string variable_name; ///< Shader variable name, e.g. "g_CameraCB"
  resource_ref resource;     ///< Resource and binding mode
};

/**
 * @brief Plan for creating shader resource bindings
 *
 * mutable_bindings are resolved once when the SRB is created and require their
 * GPU data to be ready. dynamic_bindings are resolved to variable pointers but
 * left unbound so a render packet can bind them per draw.
 *
 */
struct binding_rule {
  std::vector<variable_binding> mutable_bindings; ///< Bound once when the SRB is created
  std::vector<variable_binding> dynamic_bindings; ///< Bound per render packet; left empty in the SRB
};

} // namespace render_core
} // namespace cyberverse
