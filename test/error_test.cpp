#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <system_error>

#include "cyberverse/render_core/error.hpp"

using namespace cyberverse::render_core;

TEST_CASE("render_core_errc uses one category", "[render-core][error]") {
  const std::error_code code = render_core_errc::not_found;
  REQUIRE(code.category() == render_core_error_category());
  REQUIRE(std::string_view{ code.category().name() } == "cyberverse render-core");
  REQUIRE(code.value() == static_cast<int>(render_core_errc::not_found));
}

TEST_CASE("render_core_errc names and messages are stable", "[render-core][error]") {
  REQUIRE(to_string(render_core_errc::invalid_uri) == "invalid_uri");
  REQUIRE(to_string(render_core_errc::already_exists) == "already_exists");
  REQUIRE(make_error_code(render_core_errc::not_found).message() == "pipeline resource was not found");
  REQUIRE(make_error_code(render_core_errc::unknown_type).message() == "resource URI has an unknown type");
  REQUIRE_FALSE(make_error_code(static_cast<render_core_errc>(9999)).message().empty());
}
