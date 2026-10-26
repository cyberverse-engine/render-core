/**
 * @file resource_uri.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Resource URI
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "cyberverse/render_core/error.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Resource kind named by a resource URI
 *
 */
enum class resource_type {
  undefined = 0,
  pso,
  binding,
  texture,
  buffer,
};

/**
 * @brief Human-readable resource kind
 *
 * @param type Resource kind
 * @return std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(resource_type type) noexcept {
  switch (type) {
  case resource_type::pso:
    return "pso";
  case resource_type::binding:
    return "binding";
  case resource_type::texture:
    return "texture";
  case resource_type::buffer:
    return "buffer";
  case resource_type::undefined:
  default:
    return "undefined";
  }
}

class resource_uri;

/**
 * @brief Hash for resource_uri
 *
 */
struct resource_uri_hash {
  std::size_t operator()(const resource_uri& uri) const noexcept;
};

/**
 * @brief Immutable, case-insensitive resource URI
 *
 * A resource URI is the unified name for textures, buffers, PSOs, and shader
 * resource bindings. Its canonical text form is:
 *
 *     @type/org/path
 *
 * where:
 * - `type` is one of `pso`, `binding`, `texture`, or `buffer`;
 * - `org` identifies the owning organization and avoids collisions;
 * - `path` is a `/`-separated semantic path.
 *
 * Every component is stored in lowercase and may contain only `a-z`, `A-Z`,
 * `0-9`, `-`, and `_`; `/` is the path separator only. Parsed URIs are
 * immutable. A default-constructed or moved-from URI is invalid.
 *
 * The hash is computed once at construction and cached.
 *
 */
class resource_uri {
public:
  resource_uri() noexcept = default;

  resource_uri(const resource_uri&) = default;
  resource_uri& operator=(const resource_uri&) = default;

  resource_uri(resource_uri&& other) noexcept
      : type_{ other.type_ }, org_{ std::move(other.org_) }, path_{ std::move(other.path_) }, hash_{ other.hash_ } {
    other.reset();
  }

  resource_uri& operator=(resource_uri&& other) noexcept {
    if (this != &other) {
      type_ = other.type_;
      org_ = std::move(other.org_);
      path_ = std::move(other.path_);
      hash_ = other.hash_;
      other.reset();
    }
    return *this;
  }

  ~resource_uri() = default;

  /**
   * @brief Resource kind
   *
   * @return resource_type
   */
  [[nodiscard]] resource_type type() const noexcept { return type_; }

  /**
   * @brief Organization component, lowercase
   *
   * @return std::string_view
   */
  [[nodiscard]] std::string_view org() const noexcept { return org_; }

  /**
   * @brief Path component, lowercase and `/`-separated
   *
   * @return std::string_view
   */
  [[nodiscard]] std::string_view path() const noexcept { return path_; }

  /**
   * @brief Cached hash
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t hash() const noexcept { return hash_; }

  /**
   * @brief Canonical text form, e.g. `@texture/org/path`
   *
   * Returns an empty string for an invalid URI.
   *
   * @return std::string
   */
  [[nodiscard]] std::string to_string() const;

  /**
   * @brief Whether the URI contains all components
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_valid() const noexcept {
    return type_ != resource_type::undefined && !org_.empty() && !path_.empty();
  }

  /**
   * @brief Explicit bool conversion
   *
   * @return true
   * @return false
   */
  [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

  friend bool operator==(const resource_uri& lhs, const resource_uri& rhs) noexcept {
    return lhs.type_ == rhs.type_ && lhs.org_ == rhs.org_ && lhs.path_ == rhs.path_;
  }

private:
  friend std::expected<resource_uri, std::error_code> make_resource_uri(std::string_view uri) noexcept;

  resource_uri(resource_type type, std::string org, std::string path, std::size_t hash) noexcept
      : type_{ type }, org_{ std::move(org) }, path_{ std::move(path) }, hash_{ hash } {}

  void reset() noexcept {
    type_ = resource_type::undefined;
    org_.clear();
    path_.clear();
    hash_ = 0;
  }

  resource_type type_ = resource_type::undefined;
  std::string org_;
  std::string path_;
  std::size_t hash_ = 0;
};

inline std::size_t resource_uri_hash::operator()(const resource_uri& uri) const noexcept { return uri.hash(); }

/**
 * @brief Parse a resource URI
 *
 * Accepted form is `@type/org/path`, for example:
 *
 *     @texture/cyberverse-engine/builtin/debug
 *
 * Parsing is case-insensitive and stores all components lowercase. The org must
 * be a single non-empty component; the path must contain at least one non-empty
 * `/`-separated component and no empty segments.
 *
 * @param uri URI text
 * @return std::expected<resource_uri, std::error_code>
 */
[[nodiscard]] std::expected<resource_uri, std::error_code> make_resource_uri(std::string_view uri) noexcept;

} // namespace render_core
} // namespace cyberverse

namespace std {

template <>
struct hash<cyberverse::render_core::resource_uri> {
  std::size_t operator()(const cyberverse::render_core::resource_uri& uri) const noexcept { return uri.hash(); }
};

} // namespace std
