/**
 * @file error.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief render-core error category
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/error.hpp"

namespace cyberverse {
namespace render_core {

namespace {

class render_core_error_category_impl final : public std::error_category {
public:
  [[nodiscard]] const char* name() const noexcept override { return "cyberverse render-core"; }

  [[nodiscard]] std::string message(int condition) const override {
    switch (static_cast<render_core_errc>(condition)) {
    case render_core_errc::empty:
      return "resource URI is empty";
    case render_core_errc::missing_prefix:
      return "resource URI must start with '@'";
    case render_core_errc::missing_type:
      return "resource URI is missing its type";
    case render_core_errc::missing_org:
      return "resource URI is missing its organization";
    case render_core_errc::missing_path:
      return "resource URI is missing its path";
    case render_core_errc::unknown_type:
      return "resource URI has an unknown type";
    case render_core_errc::invalid_character:
      return "resource URI contains a disallowed character";
    case render_core_errc::empty_segment:
      return "resource URI path contains an empty segment";
    case render_core_errc::invalid_uri:
      return "invalid pipeline resource URI";
    case render_core_errc::invalid_argument:
      return "invalid render-core argument";
    case render_core_errc::duplicate:
      return "pipeline resource already exists";
    case render_core_errc::not_found:
      return "pipeline resource was not found";
    case render_core_errc::unsupported:
      return "operation is not supported";
    case render_core_errc::drsn_unavailable:
      return "DRSN loader is not available";
    case render_core_errc::out_of_memory:
      return "render-core could not allocate memory";
    case render_core_errc::already_exists:
      return "resource URI is already live";
    }
    return "unknown render-core error";
  }
};

} // namespace

const std::error_category& render_core_error_category() noexcept {
  static const render_core_error_category_impl category;
  return category;
}

std::error_code make_error_code(render_core_errc error) noexcept {
  return std::error_code{ static_cast<int>(error), render_core_error_category() };
}

} // namespace render_core
} // namespace cyberverse
