/**
 * @file error.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief render-core error codes and category
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <string_view>
#include <system_error>
#include <type_traits>

namespace cyberverse {
namespace render_core {

/**
 * @brief Unified render-core error codes
 *
 */
enum class render_core_errc {
  // resource_uri
  empty = 1,
  missing_prefix,
  missing_type,
  missing_org,
  missing_path,
  unknown_type,
  invalid_character,
  empty_segment,

  // pipeline_manager
  invalid_uri,
  invalid_argument,
  duplicate,
  not_found,
  unsupported,
  drsn_unavailable,
  out_of_memory,

  // gpu_data_manager
  already_exists,
};

/**
 * @brief Short alias for render_core_errc
 *
 */
using errc = render_core_errc;

/**
 * @brief Error category for every render-core error code
 *
 * @return const std::error_category&
 */
[[nodiscard]] const std::error_category& render_core_error_category() noexcept;

/**
 * @brief Build a render-core error code
 *
 * @param error Error code
 * @return std::error_code
 */
[[nodiscard]] std::error_code make_error_code(render_core_errc error) noexcept;

/**
 * @brief Human-readable render-core error name
 *
 * @param error Error code
 * @return std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(render_core_errc error) noexcept {
  switch (error) {
  case render_core_errc::empty:
    return "empty";
  case render_core_errc::missing_prefix:
    return "missing_prefix";
  case render_core_errc::missing_type:
    return "missing_type";
  case render_core_errc::missing_org:
    return "missing_org";
  case render_core_errc::missing_path:
    return "missing_path";
  case render_core_errc::unknown_type:
    return "unknown_type";
  case render_core_errc::invalid_character:
    return "invalid_character";
  case render_core_errc::empty_segment:
    return "empty_segment";
  case render_core_errc::invalid_uri:
    return "invalid_uri";
  case render_core_errc::invalid_argument:
    return "invalid_argument";
  case render_core_errc::duplicate:
    return "duplicate";
  case render_core_errc::not_found:
    return "not_found";
  case render_core_errc::unsupported:
    return "unsupported";
  case render_core_errc::drsn_unavailable:
    return "drsn_unavailable";
  case render_core_errc::out_of_memory:
    return "out_of_memory";
  case render_core_errc::already_exists:
    return "already_exists";
  }
  return "unknown";
}

} // namespace render_core
} // namespace cyberverse

namespace std {

template <>
struct is_error_code_enum<cyberverse::render_core::render_core_errc> : true_type {};

} // namespace std
