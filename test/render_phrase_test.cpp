/**
 * @file render_phrase_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render phrase tests
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "cyberverse/render_core/mesh_phrase.hpp"

using namespace cyberverse::render_core;

namespace {

Diligent::IPipelineState* fake_pso(std::uintptr_t value) noexcept {
  return reinterpret_cast<Diligent::IPipelineState*>(value);
}

Diligent::IShaderResourceBinding* fake_srb(std::uintptr_t value) noexcept {
  return reinterpret_cast<Diligent::IShaderResourceBinding*>(value);
}

resource_uri make_uri(std::string_view text) {
  auto result = make_resource_uri(text);
  REQUIRE(result.has_value());
  return *result;
}

} // namespace

TEST_CASE("phrase_sort names are stable", "[render-core][render-phrase]") {
  REQUIRE(to_string(phrase_sort::none) == "none");
  REQUIRE(to_string(phrase_sort::front_to_back) == "front_to_back");
  REQUIRE(to_string(phrase_sort::back_to_front) == "back_to_front");
}

TEST_CASE("opaque phrase writes color and depth", "[render-core][render-phrase]") {
  opaque_phrase phrase{ fake_pso(1), fake_srb(2) };
  REQUIRE(phrase.name() == "opaque");
  REQUIRE(phrase.sort_mode() == phrase_sort::front_to_back);
  REQUIRE(phrase.targets() == phrase_targets::color_and_depth);
  REQUIRE(phrase.enabled());
  REQUIRE(phrase.pipeline_state() == fake_pso(1));
  REQUIRE(phrase.shader_resource_binding() == fake_srb(2));
}

TEST_CASE("transparent phrase reads depth back to front", "[render-core][render-phrase]") {
  transparent_phrase phrase{ fake_pso(1) };
  REQUIRE(phrase.name() == "transparent");
  REQUIRE(phrase.sort_mode() == phrase_sort::back_to_front);
  REQUIRE(phrase.targets() == phrase_targets::color_depth_read_only);
  REQUIRE(phrase.pipeline_state() == fake_pso(1));
  REQUIRE(phrase.shader_resource_binding() == nullptr);
}

TEST_CASE("shadow phrase uses a depth-only target", "[render-core][render-phrase]") {
  shadow_phrase phrase;
  REQUIRE(phrase.name() == "shadow");
  REQUIRE(phrase.sort_mode() == phrase_sort::front_to_back);
  REQUIRE(phrase.targets() == phrase_targets::depth_only);
  REQUIRE(phrase.pipeline_state() == nullptr);
}

TEST_CASE("mesh phrases can be disabled", "[render-core][render-phrase]") {
  opaque_phrase phrase;
  REQUIRE(phrase.enabled());
  phrase.set_enabled(false);
  REQUIRE_FALSE(phrase.enabled());
  phrase.set_enabled(true);
  REQUIRE(phrase.enabled());
}

TEST_CASE("mesh phrase setters replace runtime state", "[render-core][render-phrase]") {
  mesh_phrase phrase{ "custom", phrase_sort::none, mesh_phrase_desc{} };
  REQUIRE(phrase.name() == "custom");
  REQUIRE(phrase.sort_mode() == phrase_sort::none);
  REQUIRE(phrase.targets() == phrase_targets::color_and_depth);

  phrase.set_pipeline_state(fake_pso(3));
  phrase.set_shader_resource_binding(fake_srb(4));
  REQUIRE(phrase.pipeline_state() == fake_pso(3));
  REQUIRE(phrase.shader_resource_binding() == fake_srb(4));
}

TEST_CASE("mesh phrase calls are safe without a device context", "[render-core][render-phrase]") {
  opaque_phrase phrase{ fake_pso(1) };
  const frame_context ctx;
  const render_packet item{};

  phrase.begin(ctx);
  phrase.execute(ctx, std::span<const render_packet>{ &item, 1 });
  phrase.execute(ctx, {});
  phrase.end(ctx);
  SUCCEED();
}

TEST_CASE("mesh phrase execute skips items without a pipeline", "[render-core][render-phrase]") {
  opaque_phrase phrase;
  frame_context ctx;
  render_packet item{};
  item.index_buffer = reinterpret_cast<Diligent::IBuffer*>(0x1000);
  item.index_count = 3;

  phrase.execute(ctx, std::span<const render_packet>{ &item, 1 });
  SUCCEED();
}

TEST_CASE("managed phrase stores URIs and an anonymous signature by default", "[render-core][render-phrase]") {
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/opaque");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/opaque");
  opaque_phrase phrase{ pso_uri, rule_uri };

  REQUIRE(phrase.pipeline_mode() == mesh_pipeline_mode::managed);
  REQUIRE(phrase.is_managed());
  REQUIRE(phrase.pso_uri() == pso_uri);
  REQUIRE(phrase.binding_rule_uri() == rule_uri);
  REQUIRE(phrase.signature_index() == anonymous_signature_index);
  REQUIRE(phrase.pipeline_state() == nullptr);
  REQUIRE(phrase.shader_resource_binding() == nullptr);
}

TEST_CASE("managed phrase stores an explicit signature index", "[render-core][render-phrase]") {
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/opaque");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/opaque");
  transparent_phrase phrase{ pso_uri, rule_uri, 1 };

  REQUIRE(phrase.pipeline_mode() == mesh_pipeline_mode::managed);
  REQUIRE(phrase.signature_index() == 1);
  REQUIRE(phrase.sort_mode() == phrase_sort::back_to_front);
}

TEST_CASE("managed phrase mode is immutable after construction", "[render-core][render-phrase]") {
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/opaque");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/opaque");
  opaque_phrase phrase{ pso_uri, rule_uri, 1 };

  phrase.set_pipeline_state(fake_pso(3));
  phrase.set_shader_resource_binding(fake_srb(4));
  phrase.set_direct_pipeline(fake_pso(5), fake_srb(6));

  REQUIRE(phrase.pipeline_mode() == mesh_pipeline_mode::managed);
  REQUIRE(phrase.is_managed());
  REQUIRE(phrase.pipeline_state() == nullptr);
  REQUIRE(phrase.shader_resource_binding() == nullptr);
  REQUIRE(phrase.pso_uri() == pso_uri);
  REQUIRE(phrase.binding_rule_uri() == rule_uri);
  REQUIRE(phrase.signature_index() == 1);
}

TEST_CASE("unmanaged phrase keeps direct setters", "[render-core][render-phrase]") {
  opaque_phrase phrase{ fake_pso(1) };
  REQUIRE(phrase.pipeline_mode() == mesh_pipeline_mode::unmanaged);
  REQUIRE_FALSE(phrase.is_managed());

  phrase.set_direct_pipeline(fake_pso(2), fake_srb(3));
  REQUIRE(phrase.pipeline_state() == fake_pso(2));
  REQUIRE(phrase.shader_resource_binding() == fake_srb(3));

  phrase.set_pipeline_state(fake_pso(4));
  phrase.set_shader_resource_binding(fake_srb(5));
  REQUIRE(phrase.pipeline_state() == fake_pso(4));
  REQUIRE(phrase.shader_resource_binding() == fake_srb(5));
}

TEST_CASE("managed phrase validates its URIs and signature index", "[render-core][render-phrase]") {
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/opaque");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/opaque");
  const resource_uri texture_uri = make_uri("@texture/cyberverse-engine/opaque");

  REQUIRE_THROWS_AS((opaque_phrase{ texture_uri, rule_uri }), std::invalid_argument);
  REQUIRE_THROWS_AS((opaque_phrase{ pso_uri, pso_uri }), std::invalid_argument);
  REQUIRE_THROWS_AS((opaque_phrase{ pso_uri, rule_uri, -2 }), std::invalid_argument);

  mesh_phrase_desc mixed;
  mixed.pipeline_state = fake_pso(1);
  mixed.pso_uri = pso_uri;
  mixed.binding_rule_uri = rule_uri;
  REQUIRE_THROWS_AS((mesh_phrase{ "mixed", phrase_sort::none, mixed }), std::invalid_argument);
}

TEST_CASE("managed phrase is a no-op without a device", "[render-core][render-phrase]") {
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/opaque");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/opaque");
  opaque_phrase phrase{ pso_uri, rule_uri };
  frame_context ctx;
  render_packet item{};
  item.index_buffer = reinterpret_cast<Diligent::IBuffer*>(0x1000);
  item.index_count = 3;

  phrase.begin(ctx);
  phrase.execute(ctx, std::span<const render_packet>{ &item, 1 });
  phrase.end(ctx);
  SUCCEED();
}
