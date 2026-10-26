/**
 * @file render_packet_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render packet tests
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <vector>

#include "cyberverse/render_core/render_packet.hpp"

using namespace cyberverse::render_core;

namespace {

render_packet make_indexed(float key, std::uint32_t material = 0) {
  render_packet item;
  item.sort_key = key;
  item.index_buffer = reinterpret_cast<Diligent::IBuffer*>(0x1000);
  item.index_count = 36;
  item.material_index = material;
  return item;
}

} // namespace

TEST_CASE("render_packet defaults to invalid", "[render-core][render-packet]") {
  const render_packet item;
  REQUIRE_FALSE(item.is_valid());
  REQUIRE(item.effective_instance_count() == 1);
}

TEST_CASE("render_packet validates indexed and non-indexed ranges", "[render-core][render-packet]") {
  render_packet item = make_indexed(1.0f);
  REQUIRE(item.is_valid());

  item.index_count = 0;
  REQUIRE_FALSE(item.is_valid());

  item.index_buffer = nullptr;
  item.vertex_buffer = reinterpret_cast<Diligent::IBuffer*>(0x2000);
  item.vertex_count = 6;
  REQUIRE(item.is_valid());

  item.vertex_count = 0;
  REQUIRE_FALSE(item.is_valid());

  item.visible = false;
  item.vertex_count = 6;
  REQUIRE_FALSE(item.is_valid());
}

TEST_CASE("render_packet instance count defaults to one", "[render-core][render-packet]") {
  render_packet item = make_indexed(1.0f);
  REQUIRE(item.effective_instance_count() == 1);
  item.instance_count = 8;
  REQUIRE(item.effective_instance_count() == 8);
}

TEST_CASE("render_packet comparators order opaque items front to back", "[render-core][render-packet]") {
  std::vector<render_packet> items{ make_indexed(9.0f), make_indexed(1.0f), make_indexed(4.0f) };
  std::sort(items.begin(), items.end(), render_packet_front_to_back{});

  REQUIRE(items.at(0).sort_key == 1.0f);
  REQUIRE(items.at(1).sort_key == 4.0f);
  REQUIRE(items.at(2).sort_key == 9.0f);
}

TEST_CASE("render_packet comparators order transparent items back to front", "[render-core][render-packet]") {
  std::vector<render_packet> items{ make_indexed(9.0f), make_indexed(1.0f), make_indexed(4.0f) };
  std::sort(items.begin(), items.end(), render_packet_back_to_front{});

  REQUIRE(items.at(0).sort_key == 9.0f);
  REQUIRE(items.at(1).sort_key == 4.0f);
  REQUIRE(items.at(2).sort_key == 1.0f);
}

TEST_CASE("render_packet comparators break ties deterministically", "[render-core][render-packet]") {
  render_packet a = make_indexed(5.0f, 2);
  render_packet b = make_indexed(5.0f, 1);
  REQUIRE(render_packet_front_to_back{}(b, a));
  REQUIRE_FALSE(render_packet_front_to_back{}(a, b));
  REQUIRE(render_packet_back_to_front{}(b, a));
  REQUIRE_FALSE(render_packet_back_to_front{}(a, b));

  render_packet c = make_indexed(5.0f, 1);
  c.texture_index = 3;
  b.texture_index = 2;
  REQUIRE(render_packet_front_to_back{}(b, c));
}

TEST_CASE("render_packet NaN sort keys are treated as equal", "[render-core][render-packet]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  render_packet a = make_indexed(nan, 1);
  render_packet b = make_indexed(nan, 1);
  REQUIRE_FALSE(render_packet_front_to_back{}(a, b));
  REQUIRE_FALSE(render_packet_front_to_back{}(b, a));
  REQUIRE_FALSE(render_packet_back_to_front{}(a, b));
  REQUIRE_FALSE(render_packet_back_to_front{}(b, a));
}

TEST_CASE("distance_squared_key is monotonic", "[render-core][render-packet]") {
  REQUIRE(distance_squared_key(0.0f, 0.0f, 0.0f) == 0.0f);
  REQUIRE(distance_squared_key(3.0f, 4.0f, 0.0f) == 25.0f);
  REQUIRE(distance_squared_key(1.0f, 0.0f, 0.0f) < distance_squared_key(2.0f, 0.0f, 0.0f));
}

TEST_CASE("render_packet carries optional per-packet pipeline and dynamic bindings", "[render-core][render-packet]") {
  render_packet item = make_indexed(1.0f);
  item.pipeline_state = reinterpret_cast<Diligent::IPipelineState*>(0x3000);
  item.srb = reinterpret_cast<Diligent::IShaderResourceBinding*>(0x4000);
  const render_packet_dynamic_binding binding{
    reinterpret_cast<Diligent::IShaderResourceVariable*>(0x5000),
    reinterpret_cast<Diligent::IDeviceObject*>(0x6000),
  };
  item.dynamic_bindings = std::span<const render_packet_dynamic_binding>{ &binding, 1 };

  REQUIRE(item.pipeline_state != nullptr);
  REQUIRE(item.srb != nullptr);
  REQUIRE(item.dynamic_bindings.size() == 1);
  REQUIRE(item.dynamic_bindings.front().variable == binding.variable);

  Diligent::IShaderResourceBinding* srbs[] = {
    reinterpret_cast<Diligent::IShaderResourceBinding*>(0x7000),
    reinterpret_cast<Diligent::IShaderResourceBinding*>(0x8000),
  };
  item.srbs = std::span<Diligent::IShaderResourceBinding* const>{ srbs, 2 };
  REQUIRE(item.srbs.size() == 2);
  REQUIRE(item.srbs.front() == srbs[0]);
}
