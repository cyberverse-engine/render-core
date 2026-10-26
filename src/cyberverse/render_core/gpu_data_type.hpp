/**
 * @file gpu_data_type.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief GPU data type tags
 * @version 0.2
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
 * @brief Kind of GPU data
 *
 */
enum class gpu_data_type : std::uint8_t {
  undefined = 0,
  texture = 1,
  buffer = 2,
};

} // namespace render_core
} // namespace cyberverse
