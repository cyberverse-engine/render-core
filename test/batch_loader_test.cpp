#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <tuple>
#include <utility>

#include "cyberverse/render_core/batch_loader.hpp"
#include "cyberverse/render_core/detail/shader_compiler.hpp"
#include "cyberverse/render_core/shader_manager.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/this_thread.hpp"
#include "fake_gpu_data_backend.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::test::fake_gpu_data_backend;
using cyberverse::render_core::test::make_texture_desc;

namespace {

class fake_compiler final : public detail::shader_compiler {
public:
  Diligent::IShader* compile(const shader_key&, std::string_view, const std::filesystem::path&, std::string&) override {
    return reinterpret_cast<Diligent::IShader*>(0x1000);
  }

  void destroy(Diligent::IShader*) noexcept override {}
};

resource_uri make_uri(std::string_view text) {
  auto result = make_resource_uri(text);
  REQUIRE(result.has_value());
  return *result;
}

struct fixture {
  std::unique_ptr<fake_gpu_data_backend> backend_ptr{ std::make_unique<fake_gpu_data_backend>() };
  fake_gpu_data_backend& backend = *backend_ptr;
  gpu_data_manager gpu{ std::move(backend_ptr) };
  fake_compiler compiler;
  shader_manager shaders{ compiler };
  pipeline_manager pipelines{ reinterpret_cast<Diligent::IRenderDevice*>(static_cast<std::uintptr_t>(1)),
                              gpu,
                              shaders };

  fixture() { cyberverse::runtime::set_this_as_render_thread(); }

  void tick() {
    const auto budget = cyberverse::runtime::frame_budget::unlimited();
    gpu.tick(budget);
    pipelines.tick(budget);
  }
};

} // namespace

TEST_CASE("batch_loader accepts an empty manifest", "[render-core][batch-loader]") {
  fixture value;
  batch_loader loader{ {}, value.pipelines, value.gpu };
  REQUIRE(loader.state() == batch_load_state::ready);
  REQUIRE(loader);
  REQUIRE(loader.progress() == 1.0f);
  REQUIRE(loader.errors().empty());
}

TEST_CASE("batch_loader rejects invalid manifest URI types", "[render-core][batch-loader]") {
  fixture value;
  batch_load_manifest manifest;
  manifest.gpu_data.push_back(make_uri("@pso/cyberverse-engine/not-gpu"));

  batch_loader loader{ std::move(manifest), value.pipelines, value.gpu };
  REQUIRE(loader.state() == batch_load_state::failed);
  REQUIRE_FALSE(loader);
  REQUIRE_FALSE(loader.errors().empty());
  REQUIRE(loader.errors().front().kind == batch_load_error_kind::manifest);
}

TEST_CASE("batch_loader waits for GPU data and holds its reference", "[render-core][batch-loader]") {
  fixture value;
  const resource_uri texture_uri = make_uri("@texture/cyberverse-engine/batch-texture");
  auto submitted = value.gpu.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(submitted.has_value());

  batch_load_manifest manifest;
  manifest.gpu_data.push_back(texture_uri);
  batch_loader loader{ std::move(manifest), value.pipelines, value.gpu };

  const auto budget = cyberverse::runtime::frame_budget::unlimited();
  value.gpu.tick(budget);
  value.gpu.tick(budget);
  REQUIRE(value.gpu.get_state(texture_uri) == gpu_data_state::ready);

  loader.tick(budget);
  REQUIRE(loader);
  REQUIRE(loader.state() == batch_load_state::ready);
  REQUIRE(loader.progress() == 1.0f);
  REQUIRE(loader.manifest().gpu_data.size() == 1);
}

TEST_CASE("batch_loader keeps required GPU data alive until it is destroyed", "[render-core][batch-loader]") {
  fixture value;
  const resource_uri texture_uri = make_uri("@texture/cyberverse-engine/batch-held");
  auto submitted = value.gpu.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(submitted.has_value());
  auto user_ref = std::move(std::get<0>(*submitted));
  const auto budget = cyberverse::runtime::frame_budget::unlimited();
  value.gpu.tick(budget);
  value.gpu.tick(budget);
  REQUIRE(user_ref.is_ready());

  batch_load_manifest manifest;
  manifest.gpu_data.push_back(texture_uri);
  auto loader = std::make_unique<batch_loader>(std::move(manifest), value.pipelines, value.gpu);
  loader->tick(budget);
  REQUIRE(*loader);

  user_ref.reset();
  value.gpu.tick(budget);
  REQUIRE(value.gpu.get_texture(texture_uri).is_valid());

  loader.reset();
  value.gpu.tick(budget);
  REQUIRE_FALSE(value.gpu.get_texture(texture_uri).is_valid());
}

TEST_CASE("batch_loader is copyable and movable", "[render-core][batch-loader]") {
  fixture value;
  const resource_uri texture_uri = make_uri("@texture/cyberverse-engine/batch-copy");
  auto submitted = value.gpu.submit_texture(texture_uri, make_texture_desc());
  REQUIRE(submitted.has_value());
  auto user_ref = std::move(std::get<0>(*submitted));
  const auto budget = cyberverse::runtime::frame_budget::unlimited();
  value.gpu.tick(budget);
  value.gpu.tick(budget);

  batch_load_manifest manifest;
  manifest.gpu_data.push_back(texture_uri);
  batch_loader first{ std::move(manifest), value.pipelines, value.gpu };
  first.tick(budget);
  REQUIRE(first);

  batch_loader copy{ first };
  REQUIRE(copy);
  batch_loader moved{ std::move(copy) };
  REQUIRE(moved);
  batch_loader assigned{ {}, value.pipelines, value.gpu };
  assigned = moved;
  REQUIRE(assigned);

  user_ref.reset();
  value.gpu.tick(budget);
  REQUIRE(value.gpu.get_texture(texture_uri).is_valid());
}

TEST_CASE("batch_loader fails when required GPU data is absent", "[render-core][batch-loader]") {
  fixture value;
  batch_load_manifest manifest;
  const resource_uri missing = make_uri("@texture/cyberverse-engine/batch-missing");
  manifest.gpu_data.push_back(missing);
  batch_loader loader{ std::move(manifest), value.pipelines, value.gpu };

  loader.tick(cyberverse::runtime::frame_budget::unlimited());
  REQUIRE(loader.state() == batch_load_state::failed);
  REQUIRE(loader.errors().front().kind == batch_load_error_kind::gpu_data);
}

TEST_CASE("batch_loader infers SRB binding-rule resources", "[render-core][batch-loader]") {
  fixture value;
  const resource_uri pso_uri = make_uri("@pso/cyberverse-engine/batch-pso");
  const resource_uri rule_uri = make_uri("@binding/cyberverse-engine/batch-rule");
  const resource_uri texture_uri = make_uri("@texture/cyberverse-engine/batch-inferred");

  binding_rule rule;
  rule.mutable_bindings.push_back(variable_binding{ "g_Texture", resource_ref{ texture_uri } });
  REQUIRE(value.pipelines.request_add(rule_uri, std::move(rule)).has_value());
  value.tick();

  batch_load_manifest manifest;
  manifest.srbs.push_back(pipeline_srb_key{ pso_uri, rule_uri });
  batch_loader loader{ std::move(manifest), value.pipelines, value.gpu };

  loader.tick(cyberverse::runtime::frame_budget::unlimited());
  REQUIRE(loader.state() == batch_load_state::failed);
  REQUIRE(loader.errors().front().kind == batch_load_error_kind::pso);
  REQUIRE(loader.manifest().pso_uris.size() == 1);
  REQUIRE(loader.manifest().gpu_data.size() == 1);
  REQUIRE(loader.manifest().gpu_data.front() == texture_uri);
}

TEST_CASE("batch_loader can be cancelled before completion", "[render-core][batch-loader]") {
  fixture value;
  batch_load_manifest manifest;
  manifest.gpu_data.push_back(make_uri("@texture/cyberverse-engine/batch-cancel"));
  batch_loader loader{ std::move(manifest), value.pipelines, value.gpu };

  loader.cancel();
  REQUIRE(loader.state() == batch_load_state::cancelled);
  REQUIRE_FALSE(loader);
}
