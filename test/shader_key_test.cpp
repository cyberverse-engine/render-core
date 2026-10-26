/**
 * @file shader_key_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader key tests
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include "cyberverse/render_core/shader_key.hpp"

using namespace cyberverse::render_core;

TEST_CASE("shader_stage names are stable", "[render-core][shader-key]") {
  REQUIRE(to_string(shader_stage::undefined) == "undefined");
  REQUIRE(to_string(shader_stage::vertex) == "vertex");
  REQUIRE(to_string(shader_stage::pixel) == "pixel");
  REQUIRE(to_string(shader_stage::compute) == "compute");
}

TEST_CASE("shader_key defaults to undefined", "[render-core][shader-key]") {
  const shader_key key;
  REQUIRE(key.name.empty());
  REQUIRE(key.stage == shader_stage::undefined);
  REQUIRE(key.entryPoint == "main");
  REQUIRE(key.defines.empty());
}

TEST_CASE("make_shader_key canonicalizes define order", "[render-core][shader-key]") {
  const shader_key a = make_shader_key("opaque.hlsl", shader_stage::vertex, { { "B", "2" }, { "A", "1" } });

  const shader_key b = make_shader_key("opaque.hlsl", shader_stage::vertex, { { "A", "1" }, { "B", "2" } });

  REQUIRE(a == b);
  REQUIRE(a.defines.at(0).first == "A");
  REQUIRE(a.defines.at(1).first == "B");
  REQUIRE(shader_key_hash{}(a) == shader_key_hash{}(b));
}

TEST_CASE("defines with the same name are ordered by value", "[render-core][shader-key]") {
  const shader_key key = make_shader_key("shader", shader_stage::pixel, { { "MODE", "2" }, { "MODE", "1" } });

  REQUIRE(key.defines.at(0).second == "1");
  REQUIRE(key.defines.at(1).second == "2");
}

TEST_CASE("normalize_shader_key supplies a default entry point", "[render-core][shader-key]") {
  shader_key key;
  key.name = "test.hlsl";
  key.stage = shader_stage::compute;
  key.entryPoint.clear();

  const shader_key normalized = normalize_shader_key(key);
  REQUIRE(normalized.entryPoint == "main");
  REQUIRE(normalized.stage == shader_stage::compute);
}

TEST_CASE("different shader keys are not equal", "[render-core][shader-key]") {
  const shader_key base = make_shader_key("a.hlsl", shader_stage::vertex);
  REQUIRE(base != make_shader_key("b.hlsl", shader_stage::vertex));
  REQUIRE(base != make_shader_key("a.hlsl", shader_stage::pixel));
  REQUIRE(base != make_shader_key("a.hlsl", shader_stage::vertex, {}, "vs_main"));
  REQUIRE(base != make_shader_key("a.hlsl", shader_stage::vertex, { { "A", "1" } }));
}
