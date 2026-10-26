/**
 * @file resource_uri_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Resource URI tests
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_set>
#include <utility>

#include "cyberverse/render_core/resource_uri.hpp"

using namespace cyberverse::render_core;

namespace {

bool has_error(const std::expected<resource_uri, std::error_code>& result, render_core_errc error) {
  return !result.has_value() && result.error() == make_error_code(error);
}

} // namespace

TEST_CASE("resource_uri parses a valid URI", "[render-core][resource-uri]") {
  const auto result = make_resource_uri("@texture/cyberverse-engine/builtin/debug");
  REQUIRE(result.has_value());
  REQUIRE(result->is_valid());
  REQUIRE(static_cast<bool>(*result));
  REQUIRE(result->type() == resource_type::texture);
  REQUIRE(result->org() == "cyberverse-engine");
  REQUIRE(result->path() == "builtin/debug");
  REQUIRE(result->to_string() == "@texture/cyberverse-engine/builtin/debug");
  REQUIRE(result->hash() != 0);
}

TEST_CASE("resource_uri is case-insensitive and stores lowercase", "[render-core][resource-uri]") {
  const auto lower = make_resource_uri("@texture/cyberverse-engine/builtin/debug");
  const auto upper = make_resource_uri("@TEXTURE/CyberVerse-Engine/BuiltIn/Debug");

  REQUIRE(lower.has_value());
  REQUIRE(upper.has_value());
  REQUIRE(*lower == *upper);
  REQUIRE(lower->hash() == upper->hash());
  REQUIRE(lower->to_string() == upper->to_string());
  REQUIRE(upper->org() == "cyberverse-engine");
  REQUIRE(upper->path() == "builtin/debug");
  REQUIRE(std::hash<resource_uri>{}(*lower) == std::hash<resource_uri>{}(*upper));
  REQUIRE(resource_uri_hash{}(*lower) == resource_uri_hash{}(*upper));
}

TEST_CASE("resource_uri supports every resource type", "[render-core][resource-uri]") {
  const auto pso = make_resource_uri("@pso/org/main");
  const auto binding = make_resource_uri("@binding/org/camera");
  const auto texture = make_resource_uri("@texture/org/albedo");
  const auto buffer = make_resource_uri("@buffer/org/vertices");

  REQUIRE(pso.has_value());
  REQUIRE(binding.has_value());
  REQUIRE(texture.has_value());
  REQUIRE(buffer.has_value());
  REQUIRE(pso->type() == resource_type::pso);
  REQUIRE(binding->type() == resource_type::binding);
  REQUIRE(texture->type() == resource_type::texture);
  REQUIRE(buffer->type() == resource_type::buffer);
}

TEST_CASE("resource_uri accepts multiple path segments", "[render-core][resource-uri]") {
  const auto result = make_resource_uri("@texture/my-org/a/b/c/d_e-f");
  REQUIRE(result.has_value());
  REQUIRE(result->path() == "a/b/c/d_e-f");
  REQUIRE(result->to_string() == "@texture/my-org/a/b/c/d_e-f");
}

TEST_CASE("resource_uri rejects malformed input", "[render-core][resource-uri]") {
  REQUIRE(has_error(make_resource_uri(""), render_core_errc::empty));
  REQUIRE(has_error(make_resource_uri("texture/org/path"), render_core_errc::missing_prefix));
  REQUIRE(has_error(make_resource_uri(" @texture/org/path"), render_core_errc::missing_prefix));
  REQUIRE(has_error(make_resource_uri("@/org/path"), render_core_errc::missing_type));
  REQUIRE(has_error(make_resource_uri("@texture"), render_core_errc::missing_type));
  REQUIRE(has_error(make_resource_uri("@texture/"), render_core_errc::missing_org));
  REQUIRE(has_error(make_resource_uri("@texture//path"), render_core_errc::missing_org));
  REQUIRE(has_error(make_resource_uri("@texture/org"), render_core_errc::missing_path));
  REQUIRE(has_error(make_resource_uri("@texture/org/"), render_core_errc::missing_path));
  REQUIRE(has_error(make_resource_uri("@unknown/org/path"), render_core_errc::unknown_type));
  REQUIRE(has_error(make_resource_uri("@texture.org/org/path"), render_core_errc::invalid_character));
  REQUIRE(has_error(make_resource_uri("@texture/org!/path"), render_core_errc::invalid_character));
  REQUIRE(has_error(make_resource_uri("@texture/org/path.with.dot"), render_core_errc::invalid_character));
  REQUIRE(has_error(make_resource_uri("@texture/org/path with space"), render_core_errc::invalid_character));
  REQUIRE(has_error(make_resource_uri("@texture/org/path//child"), render_core_errc::empty_segment));
  REQUIRE(has_error(make_resource_uri("@texture/org/path/"), render_core_errc::empty_segment));
}

TEST_CASE("resource_uri errors carry category and message", "[render-core][resource-uri]") {
  const std::error_code code = make_error_code(render_core_errc::unknown_type);
  REQUIRE(code.category() == render_core_error_category());
  REQUIRE(code.value() == static_cast<int>(render_core_errc::unknown_type));
  REQUIRE_FALSE(code.message().empty());
  REQUIRE(to_string(render_core_errc::unknown_type) == "unknown_type");
  REQUIRE(to_string(resource_type::undefined) == "undefined");
}

TEST_CASE("resource_uri default and moved-from values are invalid", "[render-core][resource-uri]") {
  const resource_uri default_uri;
  REQUIRE_FALSE(default_uri.is_valid());
  REQUIRE_FALSE(default_uri);
  REQUIRE(default_uri.type() == resource_type::undefined);
  REQUIRE(default_uri.org().empty());
  REQUIRE(default_uri.path().empty());
  REQUIRE(default_uri.hash() == 0);
  REQUIRE(default_uri.to_string().empty());

  auto source = make_resource_uri("@texture/org/path");
  REQUIRE(source.has_value());

  resource_uri moved{ std::move(*source) };
  REQUIRE(moved.is_valid());
  REQUIRE(moved.to_string() == "@texture/org/path");
  REQUIRE_FALSE(source->is_valid());
  REQUIRE(source->hash() == 0);

  resource_uri assigned;
  assigned = std::move(moved);
  REQUIRE(assigned.is_valid());
  REQUIRE_FALSE(moved.is_valid());
  REQUIRE(moved.hash() == 0);
}

TEST_CASE("resource_uri copies preserve value and hash", "[render-core][resource-uri]") {
  const auto source = make_resource_uri("@buffer/org/main");
  REQUIRE(source.has_value());

  const resource_uri copy = *source;
  REQUIRE(copy == *source);
  REQUIRE(copy.hash() == source->hash());
  REQUIRE(copy.to_string() == source->to_string());
}

TEST_CASE("resource_uri works as an unordered_set key", "[render-core][resource-uri]") {
  std::unordered_set<resource_uri, resource_uri_hash> uris;
  const auto first = make_resource_uri("@texture/org/path");
  const auto second = make_resource_uri("@TEXTURE/ORG/PATH");
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());

  uris.insert(*first);
  uris.insert(*second);
  REQUIRE(uris.size() == 1);
  REQUIRE(uris.contains(*first));
}
