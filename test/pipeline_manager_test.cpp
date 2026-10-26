/**
 * @file pipeline_manager_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline manager tests
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "cyberverse/platform.hpp"
#include "cyberverse/render_core/binding_rule.hpp"
#include "cyberverse/render_core/detail/shader_compiler.hpp"
#include "cyberverse/render_core/mesh_phrase.hpp"
#include "cyberverse/render_core/pipeline_desc.hpp"
#include "cyberverse/render_core/pipeline_manager.hpp"
#include "cyberverse/render_core/pipeline_variant.hpp"
#include "cyberverse/render_core/render_device.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/frame_clock.hpp"
#include "cyberverse/runtime/this_thread.hpp"
#include "fake_gpu_data_backend.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::detail::shader_compiler;
using cyberverse::render_core::test::fake_gpu_data_backend;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_clock;

namespace {

class manual_clock final : public frame_clock {
public:
  double now() noexcept override { return now_; }
  void sleep_until(double) override {}

  double now_ = 0.0;
};

class fake_shader_compiler final : public shader_compiler {
public:
  Diligent::IShader*
  compile(const shader_key&, std::string_view, const std::filesystem::path&, std::string& compile_log) override {
    compile_log = "fake";
    return reinterpret_cast<Diligent::IShader*>(static_cast<std::uintptr_t>(++next_id * 64u));
  }

  Diligent::IShader*
  compile_bytecode(const shader_key&, std::span<const std::byte>, std::string& compile_log) override {
    compile_log = "fake bytecode";
    return reinterpret_cast<Diligent::IShader*>(static_cast<std::uintptr_t>(++next_id * 64u));
  }

  void destroy(Diligent::IShader*) noexcept override {}

  std::uintptr_t next_id = 1;
};

struct fixture {
  std::unique_ptr<fake_gpu_data_backend> backend_ptr{ std::make_unique<fake_gpu_data_backend>() };
  fake_gpu_data_backend& backend = *backend_ptr;
  gpu_data_manager gpu{ std::move(backend_ptr) };
  fake_shader_compiler compiler;
  shader_manager shaders{ compiler };
  manual_clock clock;
  pipeline_manager manager{ reinterpret_cast<Diligent::IRenderDevice*>(static_cast<std::uintptr_t>(1)), gpu, shaders };

  fixture() { cyberverse::runtime::set_this_as_render_thread(); }
};

resource_uri uri(std::string_view text) {
  auto result = make_resource_uri(text);
  REQUIRE(result.has_value());
  return *result;
}

frame_budget unlimited(fixture& value) { return frame_budget{ -1.0, value.clock }; }

void value_tick(pipeline_manager& manager) {
  manual_clock clock;
  manager.tick(frame_budget{ -1.0, clock });
}

} // namespace

TEST_CASE("pipeline_manager validates request arguments synchronously", "[render-core][pipeline-manager]") {
  fixture value;

  REQUIRE_FALSE(value.manager.request_load_drsn(uri("@binding/org/x")).has_value());
  REQUIRE(value.manager.request_load_drsn(uri("@pso/org/drsn")).error() ==
          make_error_code(render_core_errc::drsn_unavailable));
  REQUIRE_FALSE(
      value.manager.request_create_srb(pipeline_srb_key{ uri("@texture/org/x"), uri("@binding/org/y") }).has_value());
  REQUIRE_FALSE(value.manager.request_reload(uri("@texture/org/x")).has_value());

  REQUIRE_FALSE(value.manager.request_create_pso(uri("@pso/org/graphics"), pipeline_desc{}).has_value());
  REQUIRE(value.manager.pso_count() == 0);

  pipeline_desc compute;
  compute.kind = pipeline_kind::compute;
  REQUIRE_FALSE(value.manager.request_create_pso(uri("@pso/org/compute"), compute).has_value());
  REQUIRE(value.manager.pso_count() == 0);

  binding_rule invalid_rule;
  invalid_rule.mutable_bindings.push_back(
      variable_binding{ "", resource_ref{ uri("@texture/org/albedo"), resource_bind_whole{} } });
  REQUIRE_FALSE(value.manager.request_add(uri("@binding/org/rule"), invalid_rule).has_value());

  binding_rule invalid_mode;
  invalid_mode.mutable_bindings.push_back(
      variable_binding{ "g_Texture", resource_ref{ uri("@texture/org/albedo"), bind_buffer_range{ 0, 0 } } });
  REQUIRE_FALSE(value.manager.request_add(uri("@binding/org/rule"), invalid_mode).has_value());
  REQUIRE(value.manager.binding_rule_count() == 0);

  REQUIRE(
      value.manager.request_add(uri("@pso/org/manual"), Diligent::RefCntAutoPtr<Diligent::IPipelineState>{}).error() ==
      make_error_code(render_core_errc::invalid_argument));
}

TEST_CASE("pipeline_manager adds and invalidates binding rules", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri rule_uri = uri("@binding/org/rule");

  binding_rule rule;
  rule.mutable_bindings.push_back(
      variable_binding{ "g_Texture", resource_ref{ uri("@texture/org/albedo"), resource_bind_whole{} } });

  REQUIRE(value.manager.request_add(rule_uri, rule).has_value());
  REQUIRE_FALSE(value.manager.contains(rule_uri));

  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.contains(rule_uri));
  REQUIRE(value.manager.binding_rule_count() == 1);
  REQUIRE(value.manager.get_state(rule_uri).type == pipeline_resource_state::ready);
  REQUIRE(value.manager.get_binding_rule(rule_uri) != nullptr);
  REQUIRE(value.manager.get_binding_rule(rule_uri)->mutable_bindings.size() == 1);

  value.manager.request_invalidate(rule_uri);
  REQUIRE(value.manager.contains(rule_uri));
  value.manager.tick(unlimited(value));
  REQUIRE_FALSE(value.manager.contains(rule_uri));
  REQUIRE(value.manager.binding_rule_count() == 0);
}

TEST_CASE("pipeline_manager records failed SRB requests until invalidated", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri pso_uri = uri("@pso/org/missing");
  const resource_uri rule_uri = uri("@binding/org/missing");

  REQUIRE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri }).has_value());
  value.manager.tick(unlimited(value));

  REQUIRE(value.manager.srb_count() == 1);
  REQUIRE(value.manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri }).type == pipeline_resource_state::failed);
  REQUIRE_FALSE(value.manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri }).error.empty());
  REQUIRE(value.manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri }) == nullptr);

  value.manager.request_invalidate_failed();
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.srb_count() == 0);
}

TEST_CASE("pipeline_manager keys anonymous and explicit signature SRBs separately", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri pso_uri = uri("@pso/org/signatures");
  const resource_uri rule_uri = uri("@binding/org/signatures");

  REQUIRE_FALSE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri, -2 }).has_value());
  REQUIRE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri }).has_value());
  REQUIRE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri, 0 }).has_value());
  REQUIRE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri, 1 }).has_value());
  value.manager.tick(unlimited(value));

  REQUIRE(value.manager.srb_count() == 3);
  REQUIRE(value.manager.get_srb_count(pso_uri, rule_uri) == 3);
  REQUIRE(value.manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri }).type == pipeline_resource_state::failed);
  REQUIRE(value.manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 0 }).type ==
          pipeline_resource_state::failed);
  REQUIRE(value.manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 1 }).type ==
          pipeline_resource_state::failed);
  REQUIRE(value.manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri }) == nullptr);
  REQUIRE(value.manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri, 0 }) == nullptr);
  REQUIRE(value.manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri, 1 }) == nullptr);

  value.manager.request_invalidate(pso_uri);
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.srb_count() == 0);
  REQUIRE(value.manager.get_srb_count(pso_uri, rule_uri) == 0);
}

TEST_CASE("pipeline_manager defers tasks when the budget is exhausted", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri rule_uri = uri("@binding/org/deferred");

  REQUIRE(value.manager.request_add(rule_uri, binding_rule{}).has_value());
  const frame_budget exhausted{ 0.0, value.clock };
  value.manager.tick(exhausted);
  REQUIRE_FALSE(value.manager.contains(rule_uri));

  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.contains(rule_uri));
}

TEST_CASE("pipeline_manager invalidation removes dependent SRBs", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri pso_uri = uri("@pso/org/missing");
  const resource_uri rule_uri = uri("@binding/org/dependent");

  REQUIRE(value.manager.request_add(rule_uri, binding_rule{}).has_value());
  REQUIRE(value.manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri }).has_value());
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.srb_count() == 1);

  value.manager.request_invalidate(rule_uri);
  value.manager.tick(unlimited(value));
  REQUIRE_FALSE(value.manager.contains(rule_uri));
  REQUIRE(value.manager.srb_count() == 0);
}

TEST_CASE("pipeline_manager records reload failures", "[render-core][pipeline-manager]") {
  fixture value;
  const resource_uri pso_uri = uri("@pso/org/missing");

  REQUIRE(value.manager.request_reload(pso_uri).has_value());
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.contains(pso_uri));
  REQUIRE(value.manager.get_state(pso_uri).type == pipeline_resource_state::failed);
  REQUIRE_FALSE(value.manager.get_state(pso_uri).error.empty());

  value.manager.request_invalidate_failed();
  value.manager.tick(unlimited(value));
  REQUIRE_FALSE(value.manager.contains(pso_uri));
}

TEST_CASE("pipeline_manager clear removes every resource", "[render-core][pipeline-manager]") {
  fixture value;
  REQUIRE(value.manager.request_add(uri("@binding/org/a"), binding_rule{}).has_value());
  REQUIRE(value.manager.request_add(uri("@binding/org/b"), binding_rule{}).has_value());
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.binding_rule_count() == 2);

  value.manager.request_clear();
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.binding_rule_count() == 0);
}

TEST_CASE("pipeline_manager release_all drops queued and live resources", "[render-core][pipeline-manager]") {
  fixture value;
  REQUIRE(value.manager.request_add(uri("@binding/org/queued"), binding_rule{}).has_value());
  value.manager.release_all();
  REQUIRE(value.manager.binding_rule_count() == 0);

  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.binding_rule_count() == 0);

  REQUIRE(value.manager.request_add(uri("@binding/org/live"), binding_rule{}).has_value());
  value.manager.tick(unlimited(value));
  REQUIRE(value.manager.binding_rule_count() == 1);

  value.manager.release_all();
  REQUIRE(value.manager.binding_rule_count() == 0);
  REQUIRE(value.manager.pso_count() == 0);
  REQUIRE(value.manager.srb_count() == 0);
}

TEST_CASE("pipeline_manager exposes stable errors", "[render-core][pipeline-manager]") {
  REQUIRE(to_string(render_core_errc::invalid_uri) == "invalid_uri");
  const std::error_code code = make_error_code(render_core_errc::not_found);
  REQUIRE(code.category() == render_core_error_category());
  REQUIRE(code.message() == "pipeline resource was not found");
}

TEST_CASE("pipeline_desc adds and replaces shaders", "[render-core][pipeline-desc]") {
  pipeline_desc desc;
  REQUIRE(desc.find_shader(shader_stage::vertex) == nullptr);

  desc.add_shader("opaque.vs", shader_stage::vertex, "/tmp/opaque.vs");
  desc.add_shader("opaque.ps", shader_stage::pixel, "/tmp/opaque.ps");
  REQUIRE(desc.shaders.size() == 2);
  REQUIRE(desc.find_shader(shader_stage::vertex) != nullptr);
  REQUIRE(desc.find_shader(shader_stage::vertex)->key.name == "opaque.vs");
  REQUIRE(desc.find_shader(shader_stage::pixel)->source == "/tmp/opaque.ps");

  desc.add_shader("replacement.vs", shader_stage::vertex, "/tmp/replacement.vs");
  REQUIRE(desc.shaders.size() == 2);
  REQUIRE(desc.find_shader(shader_stage::vertex)->key.name == "replacement.vs");
  REQUIRE(desc.find_shader(shader_stage::compute) == nullptr);

  desc.add_shader_source("inline.ps", shader_stage::pixel, "float4 main() : SV_Target { return 0; }");
  REQUIRE(desc.shaders.size() == 2);
  REQUIRE(desc.find_shader(shader_stage::pixel)->source.empty());
  REQUIRE(desc.find_shader(shader_stage::pixel)->source_text.find("SV_Target") != std::string::npos);
}

TEST_CASE("pipeline_manager creates PSOs, multiple SRBs, and reloads on a real device",
          "[render-core][pipeline-manager][gpu]") {
  if (std::getenv("CYBERVERSE_RENDER_CORE_TEST_GPU") == nullptr) {
    SKIP("set CYBERVERSE_RENDER_CORE_TEST_GPU=1 to run the pipeline GPU integration test");
  }

  namespace platform = cyberverse::platform;
  bool ran = false;
  for (render_backend backend : { render_backend::vulkan, render_backend::d3d12 }) {
    if (!is_backend_available(backend)) {
      continue;
    }
    INFO("backend: " << to_string(backend));
    ran = true;

    auto platform_handle = platform::make_platform(platform::backend_type::glfw);
    platform::window_config window_config;
    window_config.width = 320;
    window_config.height = 240;
    window_config.title = "pipeline-manager test";
    window_config.vsync = false;
    auto window = platform_handle->create_window(window_config);

    render_device_desc device_desc;
    device_desc.backend = backend;
    device_desc.window = window->native_handle();
    device_desc.width = static_cast<std::uint32_t>(window->width());
    device_desc.height = static_cast<std::uint32_t>(window->height());
    device_desc.vsync = false;
    auto device = render_device::create(device_desc);
    REQUIRE(device != nullptr);

    pipeline_manager& manager = device->pipelines();

    static const std::uint8_t pixels[4 * 4 * 4] = {
      255, 0,   0,   255, 0,   255, 0,   255, 0,   0,   255, 255, 255, 255, 255, 255, 255, 0,   0,   255, 0,   255,
      0,   255, 0,   0,   255, 255, 255, 255, 255, 255, 255, 0,   0,   255, 0,   255, 0,   255, 0,   0,   255, 255,
      255, 255, 255, 255, 255, 0,   0,   255, 0,   255, 0,   255, 0,   0,   255, 255, 255, 255, 255, 255,
    };
    texture_submit_desc texture_desc;
    texture_desc.data = pixels;
    texture_desc.width = 4;
    texture_desc.height = 4;
    texture_desc.format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    texture_desc.debug_name = "pipeline test texture";
    const resource_uri texture_uri = uri("@texture/cyberverse-engine/pipeline-test");
    cyberverse::runtime::set_this_as_render_thread();
    auto texture_submit = device->gpu_data().submit_texture(texture_uri, texture_desc);
    REQUIRE(texture_submit.has_value());
    auto texture = std::move(std::get<0>(*texture_submit));
    REQUIRE(texture.is_valid());
    const auto unlimited = [] {
      return cyberverse::runtime::frame_budget::unlimited();
    };
    for (int tick = 0; tick < 100 && device->gpu_data().get_state(texture_uri) != gpu_data_state::ready; ++tick) {
      device->gpu_data().tick(unlimited());
    }
    REQUIRE(device->gpu_data().get_state(texture_uri) == gpu_data_state::ready);
    REQUIRE(texture.get() != nullptr);

    Diligent::PipelineResourceDesc texture_resource{
      Diligent::SHADER_TYPE_PIXEL,
      "g_Texture",
      1,
      Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
      Diligent::PIPELINE_RESOURCE_FLAG_COMBINED_SAMPLER,
    };
    Diligent::PipelineResourceSignatureDesc texture_signature_desc;
    texture_signature_desc.Name = "PipelineTestTextureSignature";
    texture_signature_desc.Resources = &texture_resource;
    texture_signature_desc.NumResources = 1;
    texture_signature_desc.BindingIndex = 0;
    texture_signature_desc.UseCombinedTextureSamplers = true;
    texture_signature_desc.CombinedSamplerSuffix = "_sampler";

    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> texture_signature;
    device->device()->CreatePipelineResourceSignature(texture_signature_desc, &texture_signature);
    REQUIRE(texture_signature != nullptr);

    Diligent::PipelineResourceSignatureDesc empty_signature_desc;
    empty_signature_desc.Name = "PipelineTestEmptySignature";
    empty_signature_desc.NumResources = 0;
    empty_signature_desc.BindingIndex = 1;

    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> empty_signature;
    device->device()->CreatePipelineResourceSignature(empty_signature_desc, &empty_signature);
    REQUIRE(empty_signature != nullptr);

    const bool vulkan = backend == render_backend::vulkan;
    const std::string vertex_name = vulkan ? "pipeline.vert" : "pipeline.vs.hlsl";
    const std::string pixel_name = vulkan ? "pipeline.frag" : "pipeline.ps.hlsl";
    const std::string vertex_source = vulkan ? "#version 450\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"
                                             : "float4 main() : SV_POSITION { return float4(0.0, 0.0, 0.0, 1.0); }\n";
    const std::string pixel_source =
        vulkan ? "#version 450\nlayout(set = 0, binding = 0) uniform sampler2D g_Texture;\n"
                 "layout(location = 0) out vec4 color;\n"
                 "void main() { color = texture(g_Texture, vec2(0.0)); }\n"
               : "Texture2D g_Texture : register(t0);\n"
                 "SamplerState g_Texture_sampler : register(s0);\n"
                 "float4 main() : SV_Target { return g_Texture.Sample(g_Texture_sampler, float2(0.0, 0.0)); }\n";

    pipeline_desc desc;
    desc.kind = pipeline_kind::graphics;
    desc.pso_desc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    desc.graphics_desc.NumRenderTargets = 1;
    desc.graphics_desc.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.add_shader_source(vertex_name, shader_stage::vertex, vertex_source);
    desc.add_shader_source(pixel_name, shader_stage::pixel, pixel_source);
    desc.add_resource_signature(texture_signature);
    desc.add_resource_signature(empty_signature);

    const resource_uri pso_uri = uri("@pso/cyberverse-engine/pipeline-test");
    const resource_uri rule_uri = uri("@binding/cyberverse-engine/pipeline-test");

    REQUIRE(manager.request_create_pso(pso_uri, std::move(desc)).has_value());
    value_tick(manager);
    INFO("PSO state error: " << manager.get_state(pso_uri).error);
    REQUIRE(manager.get_state(pso_uri).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_pso(pso_uri) != nullptr);

    binding_rule rule;
    rule.mutable_bindings.push_back(
        variable_binding{ "g_Texture", resource_ref{ texture_uri, resource_bind_whole{} } });
    REQUIRE(manager.request_add(rule_uri, std::move(rule)).has_value());
    value_tick(manager);

    REQUIRE(manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri, 0 }).has_value());
    REQUIRE(manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri, 1 }).has_value());
    value_tick(manager);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 0 }).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 1 }).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_srb_count(pso_uri, rule_uri) == 2);
    REQUIRE(manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri }) == nullptr);
    REQUIRE(manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri, 0 }) != nullptr);
    REQUIRE(manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri, 1 }) != nullptr);
    texture.reset();
    REQUIRE(device->gpu_data().get_texture(texture_uri).is_valid());

    REQUIRE(manager.request_create_srb(pipeline_srb_key{ pso_uri, rule_uri }).has_value());
    value_tick(manager);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri }).type == pipeline_resource_state::failed);
    REQUIRE(manager.get_srb_count(pso_uri, rule_uri) == 3);
    REQUIRE(manager.get_srb(pipeline_srb_key{ pso_uri, rule_uri }) == nullptr);
    manager.request_invalidate_failed();
    value_tick(manager);
    REQUIRE(manager.get_srb_count(pso_uri, rule_uri) == 2);

    static const float vertices[] = {
      0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    buffer_submit_desc vertex_buffer_desc;
    vertex_buffer_desc.data = vertices;
    vertex_buffer_desc.size = sizeof(vertices);
    vertex_buffer_desc.bind_flags = Diligent::BIND_VERTEX_BUFFER;
    const resource_uri vertex_uri = uri("@buffer/cyberverse-engine/vertex-test");
    auto vertex_submit = device->gpu_data().submit_buffer(vertex_uri, vertex_buffer_desc);
    REQUIRE(vertex_submit.has_value());
    auto vertex_buffer = std::move(std::get<0>(*vertex_submit));
    REQUIRE(vertex_buffer.is_valid());
    device->gpu_data().tick(unlimited());
    device->gpu_data().tick(unlimited());
    REQUIRE(vertex_buffer.get() != nullptr);

    frame_context frame = device->begin_frame(0.016);
    opaque_phrase phrase{ pso_uri, rule_uri, 0 };
    REQUIRE(phrase.pipeline_mode() == mesh_pipeline_mode::managed);
    REQUIRE(phrase.signature_index() == 0);
    phrase.begin(frame);
    render_packet packet;
    packet.vertex_buffer = vertex_buffer.get();
    packet.vertex_count = 3;
    phrase.execute(frame, std::span<const render_packet>{ &packet, 1 });
    phrase.end(frame);
    device->present();

    pipeline_desc implicit_desc;
    implicit_desc.kind = pipeline_kind::graphics;
    implicit_desc.pso_desc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    implicit_desc.graphics_desc.NumRenderTargets = 1;
    implicit_desc.graphics_desc.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA8_UNORM;
    implicit_desc.add_shader_source(vulkan ? "implicit.vert" : "implicit.vs.hlsl",
                                    shader_stage::vertex,
                                    vulkan ? "#version 450\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"
                                           : "float4 main() : SV_POSITION { return float4(0.0, 0.0, 0.0, 1.0); }\n");
    implicit_desc.add_shader_source(vulkan ? "implicit.frag" : "implicit.ps.hlsl",
                                    shader_stage::pixel,
                                    vulkan ? "#version 450\nlayout(location = 0) out vec4 color;\n"
                                             "void main() { color = vec4(1.0); }\n"
                                           : "float4 main() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }\n");

    const resource_uri implicit_pso_uri = uri("@pso/cyberverse-engine/implicit-test");
    const resource_uri implicit_rule_uri = uri("@binding/cyberverse-engine/implicit-test");
    REQUIRE(manager.request_create_pso(implicit_pso_uri, std::move(implicit_desc)).has_value());
    value_tick(manager);
    REQUIRE(manager.get_state(implicit_pso_uri).type == pipeline_resource_state::ready);
    REQUIRE(manager.request_add(implicit_rule_uri, binding_rule{}).has_value());
    value_tick(manager);
    REQUIRE(manager.request_create_srb(pipeline_srb_key{ implicit_pso_uri, implicit_rule_uri }).has_value());
    value_tick(manager);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ implicit_pso_uri, implicit_rule_uri }).type ==
            pipeline_resource_state::ready);
    REQUIRE(manager.get_srb(pipeline_srb_key{ implicit_pso_uri, implicit_rule_uri }) != nullptr);
    REQUIRE(manager.get_srb_count(implicit_pso_uri, implicit_rule_uri) == 1);

    frame_context implicit_frame = device->begin_frame(0.016);
    opaque_phrase implicit_phrase{ implicit_pso_uri, implicit_rule_uri };
    REQUIRE(implicit_phrase.signature_index() == anonymous_signature_index);
    implicit_phrase.begin(implicit_frame);
    implicit_phrase.execute(implicit_frame, std::span<const render_packet>{ &packet, 1 });
    implicit_phrase.end(implicit_frame);
    device->present();

    REQUIRE(manager.request_reload(pso_uri).has_value());
    value_tick(manager);
    REQUIRE(manager.get_state(pso_uri).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 0 }).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 1 }).type == pipeline_resource_state::ready);
    REQUIRE(manager.get_srb_count(pso_uri, rule_uri) == 2);

    manager.request_invalidate(pso_uri);
    value_tick(manager);
    REQUIRE(manager.get_pso(pso_uri) == nullptr);
    REQUIRE(manager.get_srb_state(pipeline_srb_key{ pso_uri, rule_uri, 0 }).type == pipeline_resource_state::failed);
  }
  REQUIRE(ran);
}

TEST_CASE("pipeline_variant_cache reuses variants by state shader and input layout",
          "[render-core][pipeline-manager]") {
  fixture value;
  pipeline_desc base;
  base.add_shader_source("variant.vs", shader_stage::vertex, "fake vs");
  base.add_shader_source("variant.ps", shader_stage::pixel, "fake ps");
  pipeline_variant_desc desc;
  desc.base = std::move(base);

  pipeline_variant_cache cache{ value.manager };
  const resource_uri base_uri = uri("@pso/cyberverse-engine/base");
  pipeline_variant_key key{ base_uri, static_cast<pipeline_variant_flags>(pipeline_variant_flag::transparent), 1, 2 };
  auto first = cache.request(key, desc);
  REQUIRE(first.has_value());
  auto second = cache.request(key, desc);
  REQUIRE(second.has_value());
  REQUIRE(*first == *second);
  REQUIRE(cache.size() == 1);
  REQUIRE(cache.find(key) != nullptr);

  pipeline_variant_key shadow = key;
  shadow.flags = static_cast<pipeline_variant_flags>(pipeline_variant_flag::shadow);
  auto third = cache.request(shadow, desc);
  REQUIRE(third.has_value());
  REQUIRE(*third != *first);
  REQUIRE(cache.size() == 2);
}

TEST_CASE("pipeline_desc can add bytecode shader stages", "[render-core][pipeline-manager]") {
  pipeline_desc desc;
  const std::byte bytecode[] = { std::byte{ 1 }, std::byte{ 2 } };
  desc.add_shader_bytecode("bytecode.vs", shader_stage::vertex, bytecode);
  const pipeline_shader* shader = desc.find_shader(shader_stage::vertex);
  REQUIRE(shader != nullptr);
  REQUIRE(shader->bytecode.size() == 2);
  REQUIRE(shader->key.stage == shader_stage::vertex);
}

TEST_CASE("pipeline_manager reports aggregate state statistics", "[render-core][pipeline-manager]") {
  fixture value;
  pipeline_manager_stats empty = value.manager.get_stats();
  REQUIRE(empty.pso_count == 0);
  REQUIRE(empty.binding_rule_count == 0);
  REQUIRE(empty.srb_count == 0);

  binding_rule rule;
  rule.mutable_bindings.push_back(variable_binding{
      "g_Texture",
      resource_ref{ uri("@texture/cyberverse-engine/stats"), resource_bind_whole{} },
  });
  REQUIRE(value.manager.request_add(uri("@binding/cyberverse-engine/stats"), std::move(rule)).has_value());
  value.manager.tick(unlimited(value));

  const pipeline_manager_stats stats = value.manager.get_stats();
  REQUIRE(stats.binding_rule_count == 1);
  REQUIRE(stats.binding_rule_ready == 1);
  REQUIRE(stats.binding_rule_pending == 0);
  REQUIRE(stats.binding_rule_failed == 0);
}
