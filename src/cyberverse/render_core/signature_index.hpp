/**
 * @file signature_index.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline resource signature index
 * @version 0.1
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>

namespace cyberverse {
namespace render_core {

/**
 * @brief Shader resource binding signature index
 *
 * Non-negative values address an explicit pipeline resource signature by its
 * binding index. anonymous_signature_index addresses the implicit/default
 * signature of a pipeline state.
 *
 */
using signature_index_type = std::int32_t;

/**
 * @brief Anonymous (implicit/default) resource signature
 *
 */
inline constexpr signature_index_type anonymous_signature_index = -1;

} // namespace render_core
} // namespace cyberverse
