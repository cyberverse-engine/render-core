/**
 * @file resource_uri.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Resource URI implementation
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/resource_uri.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cyberverse {
namespace render_core {

namespace {

char ascii_lower(char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }
  return value;
}

bool is_allowed_character(char value) noexcept {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
         value == '-' || value == '_';
}

bool is_allowed_component(std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  for (char character : value) {
    if (!is_allowed_character(character)) {
      return false;
    }
  }
  return true;
}

resource_type parse_type(std::string_view value) noexcept {
  if (value == "pso") {
    return resource_type::pso;
  }
  if (value == "binding") {
    return resource_type::binding;
  }
  if (value == "texture") {
    return resource_type::texture;
  }
  if (value == "buffer") {
    return resource_type::buffer;
  }
  return resource_type::undefined;
}

void hash_bytes(std::size_t& hash, std::string_view value) noexcept {
  for (unsigned char character : value) {
    hash ^= static_cast<std::size_t>(character);
    hash *= static_cast<std::size_t>(1099511628211ull);
  }
}

std::size_t compute_hash(resource_type type, std::string_view org, std::string_view path) noexcept {
  std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
  hash ^= static_cast<std::size_t>(type);
  hash *= static_cast<std::size_t>(1099511628211ull);
  hash_bytes(hash, org);
  hash ^= static_cast<std::size_t>('/');
  hash *= static_cast<std::size_t>(1099511628211ull);
  hash_bytes(hash, path);
  return hash;
}

std::error_code uri_error(render_core_errc error) noexcept { return make_error_code(error); }

struct parsed_uri {
  resource_type type = resource_type::undefined;
  std::string org;
  std::string path;
  std::size_t hash = 0;
};

std::expected<parsed_uri, std::error_code> parse_resource_uri(std::string_view uri) {
  if (uri.empty()) {
    return std::unexpected(uri_error(render_core_errc::empty));
  }
  if (uri.front() != '@') {
    return std::unexpected(uri_error(render_core_errc::missing_prefix));
  }

  const std::string_view body = uri.substr(1);
  const std::size_t type_end = body.find('/');
  if (type_end == std::string_view::npos || type_end == 0) {
    return std::unexpected(uri_error(render_core_errc::missing_type));
  }

  const std::string_view type_text = body.substr(0, type_end);
  if (!is_allowed_component(type_text)) {
    return std::unexpected(uri_error(render_core_errc::invalid_character));
  }

  std::string normalized_type;
  normalized_type.reserve(type_text.size());
  for (char value : type_text) {
    normalized_type.push_back(ascii_lower(value));
  }

  const resource_type type = parse_type(normalized_type);
  if (type == resource_type::undefined) {
    return std::unexpected(uri_error(render_core_errc::unknown_type));
  }

  const std::string_view remainder = body.substr(type_end + 1);
  if (remainder.empty()) {
    return std::unexpected(uri_error(render_core_errc::missing_org));
  }

  const std::size_t org_end = remainder.find('/');
  const std::string_view org_text = remainder.substr(0, org_end);
  if (org_text.empty()) {
    return std::unexpected(uri_error(render_core_errc::missing_org));
  }
  if (!is_allowed_component(org_text)) {
    return std::unexpected(uri_error(render_core_errc::invalid_character));
  }
  if (org_end == std::string_view::npos) {
    return std::unexpected(uri_error(render_core_errc::missing_path));
  }

  const std::string_view path_text = remainder.substr(org_end + 1);
  if (path_text.empty()) {
    return std::unexpected(uri_error(render_core_errc::missing_path));
  }

  std::string org;
  org.reserve(org_text.size());
  for (char value : org_text) {
    org.push_back(ascii_lower(value));
  }

  std::string path;
  path.reserve(path_text.size());
  std::size_t segment_start = 0;
  while (segment_start <= path_text.size()) {
    const std::size_t segment_end = path_text.find('/', segment_start);
    const std::string_view segment = path_text.substr(segment_start, segment_end - segment_start);
    if (segment.empty()) {
      return std::unexpected(uri_error(render_core_errc::empty_segment));
    }
    if (!is_allowed_component(segment)) {
      return std::unexpected(uri_error(render_core_errc::invalid_character));
    }
    if (!path.empty()) {
      path.push_back('/');
    }
    for (char value : segment) {
      path.push_back(ascii_lower(value));
    }
    if (segment_end == std::string_view::npos) {
      break;
    }
    segment_start = segment_end + 1;
  }

  const std::size_t hash = compute_hash(type, org, path);
  return parsed_uri{ type, std::move(org), std::move(path), hash };
}

} // namespace

std::string resource_uri::to_string() const {
  if (!is_valid()) {
    return {};
  }

  std::string result;
  result.reserve(1 + render_core::to_string(type_).size() + 1 + org_.size() + 1 + path_.size());
  result.push_back('@');
  result.append(render_core::to_string(type_));
  result.push_back('/');
  result.append(org_);
  result.push_back('/');
  result.append(path_);
  return result;
}

std::expected<resource_uri, std::error_code> make_resource_uri(std::string_view uri) noexcept {
  try {
    std::expected<parsed_uri, std::error_code> parsed = parse_resource_uri(uri);
    if (!parsed.has_value()) {
      return std::unexpected(parsed.error());
    }
    return resource_uri{
      parsed->type,
      std::move(parsed->org),
      std::move(parsed->path),
      parsed->hash,
    };
  } catch (...) {
    return std::unexpected(make_error_code(render_core_errc::out_of_memory));
  }
}

} // namespace render_core
} // namespace cyberverse
