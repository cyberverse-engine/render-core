/**
 * @file render_core_benchmarks.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief render-core micro benchmarks
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cyberverse/render_core/detail/gpu_data_backend.hpp"
#include "cyberverse/render_core/detail/shader_compiler.hpp"
#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/gpu_data_type.hpp"
#include "cyberverse/render_core/pipeline_variant.hpp"
#include "cyberverse/render_core/render_phrase_list.hpp"
#include "cyberverse/render_core/shader_key.hpp"
#include "cyberverse/render_core/shader_manager.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/this_thread.hpp"

using namespace cyberverse::render_core;
using cyberverse::render_core::detail::diligent_shader_compiler;
using cyberverse::render_core::detail::gpu_data_backend;
using cyberverse::render_core::detail::gpu_data_upload_state;
using cyberverse::render_core::detail::gpu_data_upload_ticket;
using cyberverse::render_core::detail::shader_compiler;

namespace {

class benchmark_backend final : public gpu_data_backend {
public:
  Diligent::ITexture* create_texture(const texture_submit_desc&) override {
    return reinterpret_cast<Diligent::ITexture*>(next_++ * 64u);
  }

  Diligent::IBuffer* create_buffer(const buffer_submit_desc&) override {
    return reinterpret_cast<Diligent::IBuffer*>(next_++ * 64u);
  }

  void destroy_texture(Diligent::ITexture*) noexcept override {}
  void destroy_buffer(Diligent::IBuffer*) noexcept override {}

  gpu_data_upload_ticket request_texture_upload(const texture_submit_desc& desc, std::string&) override {
    const std::uint64_t id = next_ticket_++;
    uploads.emplace(id, upload{ create_texture(desc), nullptr, true });
    return gpu_data_upload_ticket{ id };
  }

  gpu_data_upload_ticket request_buffer_upload(const buffer_submit_desc& desc, std::string&) override {
    const std::uint64_t id = next_ticket_++;
    uploads.emplace(id, upload{ nullptr, create_buffer(desc), false });
    return gpu_data_upload_ticket{ id };
  }

  gpu_data_upload_state
  poll_texture_upload(gpu_data_upload_ticket ticket, Diligent::ITexture*& texture, std::string&) override {
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || !found->second.is_texture) {
      texture = nullptr;
      return gpu_data_upload_state::failed;
    }
    texture = found->second.texture;
    uploads.erase(found);
    return gpu_data_upload_state::ready;
  }

  gpu_data_upload_state
  poll_buffer_upload(gpu_data_upload_ticket ticket, Diligent::IBuffer*& buffer, std::string&) override {
    const auto found = uploads.find(ticket.id);
    if (found == uploads.end() || found->second.is_texture) {
      buffer = nullptr;
      return gpu_data_upload_state::failed;
    }
    buffer = found->second.buffer;
    uploads.erase(found);
    return gpu_data_upload_state::ready;
  }

  void cancel_texture_upload(gpu_data_upload_ticket ticket) noexcept override { uploads.erase(ticket.id); }
  void cancel_buffer_upload(gpu_data_upload_ticket ticket) noexcept override { uploads.erase(ticket.id); }

  struct upload {
    Diligent::ITexture* texture = nullptr;
    Diligent::IBuffer* buffer = nullptr;
    bool is_texture = false;
  };

  std::uintptr_t next_ = 1;
  std::uint64_t next_ticket_ = 1;
  std::unordered_map<std::uint64_t, upload> uploads;
};

texture_submit_desc texture_desc() {
  static const std::uint8_t pixels[4 * 4 * 4] = {};
  texture_submit_desc desc;
  desc.data = pixels;
  desc.width = 4;
  desc.height = 4;
  desc.format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  return desc;
}

buffer_submit_desc buffer_desc() {
  static const std::uint32_t data[16] = {};
  buffer_submit_desc desc;
  desc.data = data;
  desc.size = sizeof(data);
  return desc;
}

class counting_phrase final : public render_phrase {
public:
  explicit counting_phrase(phrase_sort sort) : sort_{ sort } {}
  std::string_view name() const noexcept override { return "counting"; }
  phrase_sort sort_mode() const noexcept override { return sort_; }
  void begin(const frame_context&) noexcept override { ++begins; }
  void execute(const frame_context&, std::span<const render_packet> items) noexcept override {
    last_count = items.size();
  }
  void end(const frame_context&) noexcept override { ++ends; }

  phrase_sort sort_ = phrase_sort::none;
  std::size_t begins = 0;
  std::size_t ends = 0;
  std::size_t last_count = 0;
};

std::vector<render_packet> make_items(std::size_t count) {
  std::vector<render_packet> items(count);
  for (std::size_t index = 0; index < count; ++index) {
    items[index].sort_key = static_cast<float>((count - index) % 1000);
    items[index].index_buffer = reinterpret_cast<Diligent::IBuffer*>(0x1000);
    items[index].index_count = 36;
    items[index].material_index = static_cast<std::uint32_t>(index % 16);
  }
  return items;
}

} // namespace

static void BM_ResourceManagerSubmitTexture(benchmark::State& state) {
  cyberverse::runtime::set_this_as_render_thread();
  auto backend = std::make_unique<benchmark_backend>();
  gpu_data_manager manager{ std::move(backend) };
  const texture_submit_desc desc = texture_desc();
  const auto texture_uri = make_resource_uri("@texture/cyberverse-engine/benchmark");
  if (!texture_uri.has_value()) {
    return;
  }
  const cyberverse::runtime::frame_budget budget = cyberverse::runtime::frame_budget::unlimited();
  for (auto _ : state) {
    auto result = manager.submit_texture(*texture_uri, desc);
    if (result.has_value()) {
      auto ref = std::move(std::get<0>(*result));
      benchmark::DoNotOptimize(ref);
      manager.tick(budget);
      manager.tick(budget);
      ref.reset();
      manager.tick(budget);
    }
  }
}
BENCHMARK(BM_ResourceManagerSubmitTexture);

static void BM_ResourceManagerSubmitBuffer(benchmark::State& state) {
  cyberverse::runtime::set_this_as_render_thread();
  auto backend = std::make_unique<benchmark_backend>();
  gpu_data_manager manager{ std::move(backend) };
  const buffer_submit_desc desc = buffer_desc();
  const auto buffer_uri = make_resource_uri("@buffer/cyberverse-engine/benchmark");
  if (!buffer_uri.has_value()) {
    return;
  }
  const cyberverse::runtime::frame_budget budget = cyberverse::runtime::frame_budget::unlimited();
  for (auto _ : state) {
    auto result = manager.submit_buffer(*buffer_uri, desc);
    if (result.has_value()) {
      auto ref = std::move(std::get<0>(*result));
      benchmark::DoNotOptimize(ref);
      manager.tick(budget);
      manager.tick(budget);
      ref.reset();
      manager.tick(budget);
    }
  }
}
BENCHMARK(BM_ResourceManagerSubmitBuffer);

static void BM_DrawItemSortFrontToBack(benchmark::State& state) {
  const std::size_t count = static_cast<std::size_t>(state.range(0));
  const std::vector<render_packet> source = make_items(count);
  std::vector<render_packet> items = source;
  for (auto _ : state) {
    state.PauseTiming();
    items = source;
    state.ResumeTiming();
    std::sort(items.begin(), items.end(), render_packet_front_to_back{});
    benchmark::DoNotOptimize(items.data());
  }
}
BENCHMARK(BM_DrawItemSortFrontToBack)->RangeMultiplier(4)->Range(1024, 1 << 16);

static void BM_DrawItemSortBackToFront(benchmark::State& state) {
  const std::size_t count = static_cast<std::size_t>(state.range(0));
  const std::vector<render_packet> source = make_items(count);
  std::vector<render_packet> items = source;
  for (auto _ : state) {
    state.PauseTiming();
    items = source;
    state.ResumeTiming();
    std::sort(items.begin(), items.end(), render_packet_back_to_front{});
    benchmark::DoNotOptimize(items.data());
  }
}
BENCHMARK(BM_DrawItemSortBackToFront)->RangeMultiplier(4)->Range(1024, 1 << 16);

static void BM_RenderPhraseListExecute(benchmark::State& state) {
  const std::size_t phrase_count = static_cast<std::size_t>(state.range(0));
  const std::vector<render_packet> items = make_items(10000);

  std::vector<std::unique_ptr<counting_phrase>> phrases;
  phrases.reserve(phrase_count);
  for (std::size_t index = 0; index < phrase_count; ++index) {
    phrases.push_back(
        std::make_unique<counting_phrase>(index % 2 == 0 ? phrase_sort::front_to_back : phrase_sort::back_to_front));
  }

  render_phrase_list list;
  for (const std::unique_ptr<counting_phrase>& phrase : phrases) {
    list.add(phrase.get());
  }
  list.reserve(phrase_count, items.size());

  for (auto _ : state) {
    render_phrase_list_stats stats = list.execute(frame_context{}, items);
    benchmark::DoNotOptimize(stats);
  }
  benchmark::DoNotOptimize(phrases.data());
}
BENCHMARK(BM_RenderPhraseListExecute)->RangeMultiplier(4)->Range(1, 16);

static void BM_RenderPhraseListUnsorted(benchmark::State& state) {
  const std::size_t count = static_cast<std::size_t>(state.range(0));
  const std::vector<render_packet> items = make_items(count);
  counting_phrase phrase{ phrase_sort::none };
  render_phrase_list list;
  list.add(&phrase);
  list.reserve(1, items.size());

  for (auto _ : state) {
    list.execute(frame_context{}, items);
  }
  benchmark::DoNotOptimize(phrase.last_count);
}
BENCHMARK(BM_RenderPhraseListUnsorted)->RangeMultiplier(4)->Range(1024, 1 << 16);

static void BM_DistanceSquaredKey(benchmark::State& state) {
  for (auto _ : state) {
    for (std::uint32_t index = 0; index < (1u << 16); ++index) {
      benchmark::DoNotOptimize(distance_squared_key(static_cast<float>(index), 1.0f, 2.0f));
    }
  }
}
BENCHMARK(BM_DistanceSquaredKey);

namespace {

class benchmark_shader_compiler final : public shader_compiler {
public:
  Diligent::IShader* compile(const shader_key& /*key*/,
                             std::string_view source,
                             const std::filesystem::path& /*source_path*/,
                             std::string& compile_log) override {
    benchmark::DoNotOptimize(source.data());
    compile_log.clear();
    return reinterpret_cast<Diligent::IShader*>(0x1000);
  }

  void destroy(Diligent::IShader*) noexcept override {}
};

} // namespace

static void BM_ShaderKeyHash(benchmark::State& state) {
  const shader_key key = make_shader_key(
      "opaque.hlsl", shader_stage::vertex, { { "QUALITY", "HIGH" }, { "SHADOWS", "1" }, { "INSTANCING", "1" } });
  for (auto _ : state) {
    for (std::uint32_t repeat = 0; repeat < (1u << 16); ++repeat) {
      benchmark::DoNotOptimize(shader_key_hash{}(key));
    }
  }
}
BENCHMARK(BM_ShaderKeyHash);

static void BM_ShaderManagerCacheHit(benchmark::State& state) {
  benchmark_shader_compiler compiler;
  shader_manager manager{ compiler };
  const shader_key key = make_shader_key("opaque.hlsl", shader_stage::pixel);
  const std::string source = "float4 main() : SV_Target { return 0; }";
  if (manager.get_or_create_from_source(key, source) == nullptr) {
    state.SkipWithError("shader manager warm-up failed");
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(manager.get_or_create_from_source(key, source));
  }
  benchmark::DoNotOptimize(manager.get_stats());
}
BENCHMARK(BM_ShaderManagerCacheHit);

BENCHMARK_MAIN();

static void BM_PipelineVariantCacheLookup(benchmark::State& state) {
  cyberverse::runtime::set_this_as_render_thread();
  auto backend = std::make_unique<benchmark_backend>();
  gpu_data_manager gpu{ std::move(backend) };
  benchmark_shader_compiler compiler;
  shader_manager shaders{ compiler };
  pipeline_manager pipelines{ reinterpret_cast<Diligent::IRenderDevice*>(static_cast<std::uintptr_t>(1)),
                              gpu,
                              shaders };
  pipeline_variant_cache cache{ pipelines };

  const auto base_uri = make_resource_uri("@pso/cyberverse-engine/variant-base");
  if (!base_uri.has_value()) {
    return;
  }
  pipeline_variant_desc desc;
  desc.base.add_shader_source("variant.vs", shader_stage::vertex, "vs");
  desc.base.add_shader_source("variant.ps", shader_stage::pixel, "ps");
  const pipeline_variant_key key{
    *base_uri, static_cast<pipeline_variant_flags>(pipeline_variant_flag::transparent), 1, 2
  };
  auto warmup = cache.request(key, desc);
  benchmark::DoNotOptimize(warmup);

  for (auto _ : state) {
    auto result = cache.request(key, desc);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_PipelineVariantCacheLookup);
