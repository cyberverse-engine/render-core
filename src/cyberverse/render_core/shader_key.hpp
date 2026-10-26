/**
 * @file shader_key.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader key
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cyberverse {
namespace render_core {

/**
 * @brief Shader stage used by a shader key
 *
 */
enum class shader_stage { undefined = 0, vertex, pixel, compute };

/**
 * @brief One shader macro definition
 *
 */
using shader_define = std::pair<std::string, std::string>;

/**
 * @brief Shader macro definitions
 *
 */
using shader_defines = std::vector<shader_define>;

/**
 * @brief Identity of a shader permutation
 *
 * A key identifies a logical shader name, stage, entry point, and macro set.
 * Two keys with the same defines in a different order are the same permutation;
 * normalize_shader_key()/make_shader_key() sort the defines. The manager
 * normalizes keys it receives, so callers may construct keys directly.
 *
 */
struct shader_key {
  std::string name;                             ///< Logical name or relative path, e.g. "opaque.vs"
  shader_stage stage = shader_stage::undefined; ///< Shader stage
  std::string entryPoint = "main";              ///< Entry point
  shader_defines defines;                       ///< Macro variants

  bool operator==(const shader_key&) const noexcept = default;
};

/**
 * @brief Hash for shader_key
 *
 */
struct shader_key_hash {
  std::size_t operator()(const shader_key& key) const noexcept;
};

/**
 * @brief Human-readable shader stage name
 *
 * @param stage Shader stage
 * @return std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(shader_stage stage) noexcept {
  switch (stage) {
  case shader_stage::vertex:
    return "vertex";
  case shader_stage::pixel:
    return "pixel";
  case shader_stage::compute:
    return "compute";
  case shader_stage::undefined:
  default:
    return "undefined";
  }
}

/**
 * @brief Normalize a key in place
 *
 * Sorts defines by name then value and replaces an empty entry point with
 * "main". This is the canonical form used by shader_manager's cache.
 *
 * @param key Key to normalize
 * @return shader_key
 */
[[nodiscard]] shader_key normalize_shader_key(shader_key key) noexcept;

/**
 * @brief Make a canonical shader key
 *
 * The factory sorts defines so {A=1,B=2} and {B=2,A=1} produce equal keys.
 *
 * @param name Logical name or relative path
 * @param stage Shader stage
 * @param defines Macro definitions
 * @param entryPoint Entry point, defaults to "main"
 * @return shader_key
 */
[[nodiscard]] shader_key make_shader_key(std::string name,
                                         shader_stage stage,
                                         shader_defines defines = {},
                                         std::string entryPoint = "main") noexcept;

} // namespace render_core
} // namespace cyberverse
