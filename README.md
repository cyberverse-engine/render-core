# render-core

Cyberverse Engine render core: DiligentEngine Vulkan / Direct3D 12 device
creation from a `platform` native window handle, render phrases (batched render
passes), a generational GPU data manager, a cached shader manager, a resource
URI layer, and an asynchronous PSO/binding-rule/SRB pipeline manager.

## Features

- `render_device`: creates the DiligentEngine Vulkan or Direct3D 12 device,
  immediate context, and swap chain from `platform::native_window_handle`; owns
  the GPU data manager, shader manager, pipeline manager, `begin_frame()`,
  `present()`, `resize()`, and `wait_idle()`. Use `is_backend_available()` to
  query the backends compiled into the build.
- `gpu_data_type` / `gpu_data_ref` / `gpu_data_manager`: reference-counted
  logical textures and buffers; every resource must have a valid URI and entries
  live in a single URI-keyed map. `submit_*(uri, desc)` is asynchronous,
  snapshots source data, assigns a global `submit_version`, and returns a typed ref. `tick()`
  computes sizes, starts/polls uploads, applies overwrite versions, and destroys
  resources only after their last ref is gone. Each entry keeps the five most
  recent version records. Uploads honor a per-tick byte budget and priority;
  buffers use Diligent `IGPUUploadManager` when available, and textures use an
  asynchronous staging-texture copy fenced on the device. `detail::gpu_data_backend`
  is the injectable upload seam.
- `error` / `render_core_errc`: one unified `std::error_category` named
  `"cyberverse render-core"` for URI parsing, pipeline manager, and GPU data
  errors; `to_string()` and `make_error_code()` share the same enum.
- Internal seams such as `detail::gpu_data_backend`, `detail::diligent_gpu_data_backend`,
  `detail::shader_compiler`, and `detail::diligent_shader_compiler` live under
  `src/cyberverse/render_core/detail/` and are not included by the public umbrella.
- `frame_context` / `viewport`: non-owning per-frame view with a raw pointer to
  the render device, targets, viewport, camera SRB, frame index, delta time,
  interpolation alpha, and a `runtime::frame_budget` (unlimited by default).
  Each render stage may call `render_device::begin_frame(dt, alpha, budget)`;
  the frame index advances once in `present()`, so all stages in one presented
  frame share it. Check `has_device()` before calling `device()`.
- `shader_key` / `shader_manager`: canonical shader permutation keys, file or
  in-memory source loading, recursive quoted `#include` resolution, precompiled
  bytecode loading, compiled-shader cache, content hashing, and hot-reload that
  keeps the last good shader after a failed compile.
- `resource_uri`: case-insensitive `@type/org/path` names for PSOs, bindings,
  textures, and buffers, with validation, canonical lowercase storage, parse
  errors via `std::expected`, and a cached hash. `gpu_data_manager` can register
  textures and buffers under a URI and resolve them by name.
- `resource_ref` / `binding_rule`: bind shader variable names to texture or
  buffer URIs, including whole resources, array elements, and constant-buffer
  ranges. Mutable bindings are fixed when an SRB is created; dynamic bindings
  are resolved but left for per-packet binding.
- `pipeline_variant_cache`: derives transparent, shadow, and depth-prepass PSO
  variants from a base `pipeline_desc`, appends variant shader macros, and
  reuses them by render-state/shader/input-layout key.
- `pipeline_desc` / `pipeline_manager`: device-owned asynchronous PSO loading
  from DRSN or a `pipeline_desc`, manual PSO/binding-rule/SRB insertion,
  per-signature SRB creation, reload, and dependency-aware invalidation. SRB
  APIs use a `signature_index`: `-1` selects the anonymous/implicit signature,
  while non-negative values select an explicit resource signature.
  `pipeline_desc` accepts source or precompiled bytecode shaders, and
  `pipeline_manager::get_stats()` reports aggregate ready/pending/failed counts.
  `render_device::pipelines()` returns the manager. Requests are queued and
  executed in `tick()` under a `runtime::frame_budget`; failed entries stay
  inspectable until explicitly invalidated. PSOs with static resources are
  rejected because resource signatures are expected to be initialized through
  the SRB.
- `core_frame_prelude`: a `runtime::frame_prelude` implementation that ticks
  `gpu_data_manager` and `pipeline_manager` with the current frame budget. It
  does not cache frame state, create frame contexts, or present.
- `batch_loader`: copyable/movable scene-loading helper constructed from a
  `render_device` (or injected managers for tests). It expands a manifest from
  explicit PSO/SRB/GPU URIs, infers binding-rule GPU dependencies, waits for
  asynchronous resources, requests SRBs, and holds GPU data refs until
  ready/cancelled. It does not create PSOs, binding rules, or GPU objects; those
  must already be queued or registered by the scene/asset layer.
- `render_packet` / `render_phrase_list`: deterministic front-to-back and
  back-to-front sorting without mutating the caller's item array. Packets carry
  optional per-packet PSO/SRB overrides, an ordered SRB set, and dynamic
  variable/object bindings. `execute()` accepts an optional frame budget and
  stops before starting a phrase when the budget is exhausted; each phrase runs
  atomically.
- `render_phrase` and built-in `opaque_phrase`, `transparent_phrase`, and
  `shadow_phrase` mesh passes. A mesh phrase is either unmanaged (direct
  PSO/SRB pointers, mutable after construction) or managed (PSO URI + binding
  rule URI + optional signature index, fixed at construction); the mode cannot
  change later.
- Catch2 tests and Google Benchmark micro benchmarks.

The library targets the DiligentEngine Vulkan backend (MoltenVK on macOS, native
Vulkan on Windows/Linux) and the Direct3D 12 backend on Windows. Windows builds
prefer Direct3D 12 when both are available; other platforms use Vulkan. See
[docs/roadmap.md](docs/roadmap.md) for the render graph, PSO cache, and Lua
pipeline milestones.

## Build and test

```bash
# Tests and benchmarks must be built and run outside the repository.
cmake -S . -B /tmp/cyberverse-render-core-test
cmake --build /tmp/cyberverse-render-core-test
ctest --test-dir /tmp/cyberverse-render-core-test --output-on-failure

# Benchmarks
/tmp/cyberverse-render-core-test/benchmark/cyberverse_render_core_benchmarks

# Optional real window + GPU integration test (opens a window briefly)
CYBERVERSE_RENDER_CORE_TEST_GPU=1 \
  /tmp/cyberverse-render-core-test/test/cyberverse_render_core_tests "[gpu]"
```

DiligentEngine is resolved from an existing target, `CYBERVERSE_DILIGENT_DIR`
(a source tree or its environment equivalent), a sibling `../DiligentEngine`,
or a tagged FetchContent clone. On macOS the Vulkan SDK (or MoltenVK) must be
visible, for example through `VULKAN_SDK`. On Windows, Direct3D 12 requires the
Windows SDK and `fxc`; Vulkan additionally requires the Vulkan SDK.

```bash
CYBERVERSE_DILIGENT_DIR=/path/to/DiligentEngine \
VULKAN_SDK=/path/to/VulkanSDK \
cmake -S . -B /tmp/cyberverse-render-core-test
```

Skip components with:

```bash
-DCYBERVERSE_RENDER_CORE_BUILD_TESTS=OFF
-DCYBERVERSE_RENDER_CORE_BUILD_BENCHMARKS=OFF
```

DRSN loading is enabled by default and requires DiligentTools/RenderStateNotation.
Disable it with `-DCYBERVERSE_RENDER_CORE_WITH_DRSN=OFF`; `request_load_drsn()`
then returns `drsn_unavailable` and the rest of `pipeline_manager` still builds.
With DRSN enabled, the application still creates the parser and
`IRenderStateNotationLoader`; the manager does not create either, nor a stream
factory, and the loader must outlive the device. Because a loader needs the
already-created `IRenderDevice`, the usual order is:

1. create the device with `render_device::create(desc)` (no loader);
2. create the parser/loader from `device->device()`;
3. call `device->pipelines().set_drsn_loader(loader)` before the first
   `request_load_drsn()`.

`set_drsn_loader()` exists specifically to break the device/loader construction
cycle. The older `render_device::create(desc, loader)` overload remains for
hosts that already own a loader.

When the swap-chain surface format differs from the DRSN file (for example
BGRA8 on a MoltenVK surface), pass `drsn_load_options` to
`request_load_drsn()`:

```cpp
render_core::drsn_load_options options;
options.color_format = swap_chain->GetDesc().ColorBufferFormat;
options.depth_format = swap_chain->GetDesc().DepthBufferFormat;
pipelines.request_load_drsn(pso_uri, "my-pipeline", options);
```

The colors/depth are applied before the optional `modify_pipeline` callback and
the PSO is created. The options are retained for `request_reload()`; callback
user data must outlive the PSO.

## Usage sketch

```cpp
#include <cstdint>
#include <vector>

#include "cyberverse/render_core.hpp"

using namespace cyberverse;

auto platform = platform::make_platform(platform::default_backend());
platform::window_config window_config;
window_config.width = 1280;
window_config.height = 720;
auto window = platform->create_window(window_config);

render_core::render_device_desc device_desc;
// Defaults to Direct3D 12 on Windows when available, otherwise Vulkan.
device_desc.window = window->native_handle();
device_desc.width = static_cast<std::uint32_t>(window->width());
device_desc.height = static_cast<std::uint32_t>(window->height());
auto device = render_core::render_device::create(device_desc);

// Shader keys are canonical permutations; the manager caches and hot-reloads.
const render_core::shader_key vs_key =
  render_core::make_shader_key("opaque", render_core::shader_stage::vertex);
Diligent::IShader* vertex_shader =
  device->shaders().get_or_create_from_file(vs_key, "shaders/opaque.hlsl");

render_core::render_phrase_list phrases;
// PSOs come from the shader/pipeline manager; nullptr is a valid placeholder
// and makes execute() a no-op.
render_core::opaque_phrase opaque{nullptr};
render_core::transparent_phrase transparent{nullptr};
phrases.add(&opaque);
phrases.add(&transparent);

std::vector<render_core::render_packet> items;
// Fill items with buffers, ranges, and camera-distance sort keys.

while (!window->should_close()) {
  window->poll_events();

  render_core::frame_context frame = device->begin_frame(/* dt */ 0.016);

  // Tick GPU data and pipeline tasks with the frame's budget.
  device->gpu_data().tick(frame.frame_budget());
  device->pipelines().tick(frame.frame_budget());

  phrases.execute(frame, items);
  device->present();
}
```

Phrases do not own GPU objects. The caller owns pipeline states, shader resource
bindings, and buffers; a phrase binds the render targets, viewport, PSO/SRB, and
per-item vertex/index/instance buffers. Calls are null-safe so a missing device
context or PSO is a no-op rather than a crash.

The device-owned `pipeline_manager` can own that lifecycle:

```cpp
render_core::pipeline_manager& pipelines = device->pipelines();
runtime::steady_frame_clock pipeline_clock;

render_core::pipeline_desc pso_desc;
pso_desc.add_shader_source("opaque.vs", render_core::shader_stage::vertex, vertex_source);
pso_desc.add_shader_source("opaque.ps", render_core::shader_stage::pixel, pixel_source);
pipelines.request_create_pso(pso_uri, std::move(pso_desc));

render_core::binding_rule rule;
rule.mutable_bindings.push_back(
  {"g_Texture", render_core::resource_ref{texture_uri, render_core::resource_bind_whole{}}});
pipelines.request_add(rule_uri, std::move(rule));
// Anonymous SRB: use an explicit index (0, 1, ...) for explicit signatures.
pipelines.request_create_srb({pso_uri, rule_uri});

// The default managed phrase also targets the anonymous signature.
render_core::opaque_phrase managed_opaque{pso_uri, rule_uri};

// Explicit signature example:
// render_core::opaque_phrase explicit_opaque{pso_uri, rule_uri, 0};

// Once per frame, after gpu_data().tick(), give the pipeline tasks a budget.
runtime::frame_budget budget{0.002, pipeline_clock};
pipelines.tick(budget);
```

Failed tasks stay inspectable through `get_state()` / `get_srb_state()` until
`request_invalidate_failed()` runs in a later `tick()`.

## Threading and lifetime

`render_device`, `frame_context`, `gpu_data_manager`, `pipeline_manager`, and
`render_phrase_list` are single-threaded and must be used from the render
thread. `pipeline_manager::request_*` may be called from
other threads because it forwards to `runtime::task_queue`, but all `tick()`
and getter calls must stay on the render thread. `shader_manager` is thread-safe
and serializes compiles internally.
GPU data lifetime is managed by `texture_data_ref` / `buffer_data_ref`: their
copy/destruction only updates an atomic count, and the manager destroys the
resource in a later `tick()`. `submit_*()` is asynchronous and never calls the
backend directly; failed uploads are reported through the returned version's
`upload_state()` / `upload_error()`. Overwriting a live URI creates a new
internal version while existing refs remain valid. Each entry retains only the
five most recent versions; an SRB bound to an older version must be rebuilt
after that version record is pruned. `texture_data_ref` /
`buffer_data_ref` objects must be destroyed before the owning `render_device` /
`gpu_data_manager`; ref destruction is atomic and does not allocate. The
platform window must outlive the render device; a `frame_context` only borrows
the device and must not outlive it.

`shader_manager` owns the shaders it returns; do not call `Release()` on them.
The manager resolves quoted `#include` directives recursively before compiling
and accepts precompiled bytecode; include search paths are configured with
`add_include_path()`. `pipeline_manager` keeps pipeline
descriptions and resource signatures alive for reload; pointers stored inside
`Diligent::PipelineStateDesc::ResourceLayout` and `GraphicsPipelineDesc` must
outlive the manager.
