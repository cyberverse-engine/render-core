/**
 * @file resource_ref.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Resource reference
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <variant>

#include "cyberverse/render_core/resource_uri.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Bind an entire texture or buffer
 *
 */
struct resource_bind_whole {};

/**
 * @brief Bind one element of a shader resource array
 *
 */
struct resource_bind_array_element {
  std::uint32_t index = 0; ///< Array element index
};

/**
 * @brief Bind a whole texture-array view to a shader resource
 *
 * The URI names the array texture; the manager binds its default view. Shader
 * resource arrays that require distinct objects per element should use
 * resource_bind_array_element.
 *
 */
struct resource_bind_whole_array {};

/**
 * @brief Bind a range of a constant buffer
 *
 */
struct bind_buffer_range {
  std::uint32_t offset = 0; ///< Byte offset from the start of the buffer
  std::uint32_t size = 0;   ///< Byte size; 0 means "from offset to the end"
};

/**
 * @brief How a resource reference is bound
 *
 */
using resource_bind_mode =
    std::variant<resource_bind_whole, resource_bind_array_element, resource_bind_whole_array, bind_buffer_range>;

/**
 * @brief One resource bound to a shader variable
 *
 */
struct resource_ref {
  resource_uri uri;                                ///< Texture or buffer URI
  resource_bind_mode mode = resource_bind_whole{}; ///< Binding mode
};

} // namespace render_core
} // namespace cyberverse
