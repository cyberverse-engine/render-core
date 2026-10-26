# render-core 路线图

- **状态：** 持续更新的文档
- **最后更新：** 2026-10-26
- **范围：** `cyberverse-engine/render-core`
- **依赖：** DiligentEngine（Vulkan / MoltenVK、Direct3D 12）、`cyberverse::platform`、`cyberverse::runtime`、GLM

本文档说明 `render-core` 的定位、当前基线和后续里程碑。它列出的是工作项和验收边界，不是
排期承诺；具体 API 以头文件和测试为准。

## 定位

`render-core` 是 Cyberverse Engine 的渲染核心层，位于 `platform` 与具体游戏/场景代码之间：

- 消费 `platform` 暴露的原生窗口句柄，创建 DiligentEngine Vulkan 或 Direct3D 12 设备、立即上下文和交换链；
- 提供 `render_phrase`（渲染阶段）抽象，把不透明、透明、阴影等合批渲染流程拆成可复用、
  可排序、可单独启停的对象；
- 提供 `gpu_data_manager`，用逻辑资源 ref、原子引用计数和内部版本管理纹理/缓冲区的生命周期；
- `render_device` 持久持有 device context、`gpu_data_manager`、`shader_manager` 和 `pipeline_manager`；每帧通过 `frame_context` 提供非拥有快照；
- 提供带缓存和热重载的 `shader_manager`，以及可排序的 `render_packet` 和按阶段执行的 `render_phrase_list`；
- 提供 `pipeline_manager`，以 `resource_uri` 管理 PSO、binding rule 和 SRB，并通过 `runtime` 的任务队列和帧预算异步执行创建/重载/失效；
- 未来在这一层之上扩展材质/渲染图和 Lua 渲染管线脚本。

不把窗口/输入、帧时钟、场景图或 ECS 放进 `render-core`。`platform` 负责窗口和事件，
`runtime` 负责帧调度和固定步长，`camera`/`transform` 负责视图投影矩阵，`render-core` 只负责
“拿到一帧的上下文并把它画出来”。

## 依赖方向

```text
geometry        (数学/几何)
    ↑
transform ← camera
    ↑
animation

platform  ← render-core → DiligentEngine
runtime   ← render-core（仅 task_queue / frame_budget）
```

- `render-core` → `platform`：只消费 `native_window_handle` 和窗口尺寸，不轮询事件、不创建窗口。
- `render-core` → DiligentEngine：公共头文件暴露 Diligent 接口类型，设备/交换链/绘制调用由
  Diligent 完成；Vulkan 在 macOS/Linux/Windows 可用，Direct3D 12 在 Windows 可用。
- `render-core` → `runtime`：只复用 `task_queue` 和 `frame_budget`，不接管帧循环、时钟或阶段调度；
  `runtime` 仍然不依赖渲染器。
- `render-core` 不依赖 `camera`、`geometry` 或场景类型；上层把相机矩阵和实例数据写入缓冲区或
  SRB 后交给渲染阶段。
- DiligentEngine 通过 CMake 模块解析：优先复用已存在的 target，其次读取
  `CYBERVERSE_DILIGENT_DIR` 或同级 `../DiligentEngine`，最后才 FetchContent。

## 当前基线（v0.2）

v0.2 在 v0.1 骨架上完成异步上传调度、PSO 变体、shader include/bytecode 和逐 packet SRB 接入；Vulkan 交换链已在 macOS/MoltenVK 上真实运行验证，
Direct3D 12 路径已实现并待 Windows CI/开发机完成运行时验证。

| 模块 | 当前状态 |
| --- | --- |
| `render_device` | 从 `platform::native_window_handle` 创建 DiligentEngine Vulkan 或 Direct3D 12 设备、立即上下文和交换链，并持有 `gpu_data_manager` / `shader_manager` / `pipeline_manager`；存在 `Diligent-GraphicsTools` 时创建 `IGPUUploadManager` 并交给 GPU data backend；提供 `gpu_data()`、`shaders()`、`pipelines()`、`begin_frame(delta, alpha, budget)`、`present()`、`resize()`、`wait_idle()`、`release_all_resources()`。多个 stage 可在同一帧分别调用 `begin_frame`；frame index 只在 `present()` 时递增一次。创建时可注入可选 DRSN loader。macOS 使用 `NSView*`，Windows 使用 `HWND`（Vulkan/D3D12），Linux 支持 X11/Wayland 句柄。`is_backend_available()` 在创建前报告当前构建支持的后端。 |
| `core_frame_prelude` | `runtime::frame_prelude` 的 render-core 实现；每帧使用当前传入的 `frame_budget` 依次 tick `gpu_data_manager` 与 `pipeline_manager`。不缓存 budget/frame delta、不创建 frame context、不 present。 |
| `error` / `render_core_errc` | 所有 render-core 错误码合并为一个 `render_core_errc`；统一 `std::error_category`，category name 为 `cyberverse render-core`，提供 `make_error_code()` 和 `to_string()`。 |
| `detail/` | 不期待公开的接缝类型（`gpu_data_backend`、`diligent_gpu_data_backend`、`shader_compiler`、`diligent_shader_compiler`）放在 `src/cyberverse/render_core/detail/`，命名空间为 `cyberverse::render_core::detail`；public umbrella 不直接包含 detail header。 |
| `gpu_data_type` / `gpu_data_ref` | 纹理/缓冲区逻辑资源类型；`texture_data_ref` / `buffer_data_ref` 以原子引用计数保活逻辑资源。ref 不感知内部版本；`get()` 解析当前版本，`current_version()`、`upload_state(version)`、`upload_error(version)` 查询提交进度。不再暴露 handle 类型。 |
| `gpu_data_manager` | 单线程渲染线程 API（`runtime::is_this_render_thread()` 断言）；所有资源必须有 URI，entry 只保存在一个 URI-keyed map 中；`submit_*(uri, desc)` 异步入队并返回 `std::expected<std::tuple<ref, submit_version>, std::error_code>`，tick 中计算大小、取消旧上传、发起/轮询上传、按全局版本号覆盖 current；所有销毁只在 tick。每个 entry 保留最近 5 个版本记录，支持 immediate/LRU、`retire_delay_ticks`、内存预算、上传优先级（low/normal/high）、每 tick 上传字节预算和 `detail::gpu_data_backend` 上传接缝。缓冲上传使用 `IGPUUploadManager`（可用时），纹理由 staging texture + GPU fence 异步拷贝。`get_stats()` 返回类型/状态/字节/版本和上传字节的零分配快照。 |
| `shader_key` | 规范化 shader permutation key：逻辑名、stage、entry point、排序后的宏定义；提供 `shader_key_hash` 和 `normalize_shader_key()`。 |
| `shader_manager` | 线程安全的 shader 缓存：文件/内存源码加载、递归引号 `#include` 解析（相对文件目录和 `add_include_path()` 搜索）、预编译 bytecode 加载、内容 hash、按 key 复用、`reload(force)`、`is_source_changed()`、编译日志、失败时保留上一次成功的 shader。通过 `detail::shader_compiler` 接缝支持假编译器和未来的离线/跨平台编译。Diligent 实现走 `IRenderDevice::CreateShader`，按扩展名推断源码语言，并处理 GLSL `#version` 的 verbatim 编译。 |
| `frame_context` | 每帧非拥有快照：`render_device*`（可为空）、device context、颜色/深度/只读深度目标、viewport、camera SRB、帧索引、dt、固定步 alpha，以及 `runtime::frame_budget`（默认无限）。`device()` 返回 device 引用，调用前需检查 `has_device()`；默认构造为无效上下文。 |
| `render_packet` | 值类型渲染包：排序键、实例缓冲/数量、纹理/材质索引、顶点/索引缓冲和范围、索引类型、可见性；可选逐 packet PSO/SRB 覆盖、有序 SRB 集合（相机/材质/实例）和动态 variable/object 绑定；提供确定性前向/后向比较器和距离平方辅助函数。 |
| `resource_uri` | 统一资源命名 `@type/org/path`：`type` 为 `pso` / `binding` / `texture` / `buffer`，`org` 防冲突，`path` 用 `/` 语义分层；大小写不敏感且规范化为小写，字符集为 `a-z A-Z 0-9 - _`；构造时缓存 hash，通过 `std::expected<resource_uri, std::error_code>` 解析并给出明确错误。`gpu_data_manager` 支持按 URI 注册/查询纹理和缓冲区。 |
| `resource_ref` / `binding_rule` | 把 shader 变量名绑定到 texture/buffer URI；支持 whole、array element、whole array 和 constant-buffer range 模式。`binding_rule` 区分创建 SRB 时固定的 mutable 绑定和逐 render packet 绑定的 dynamic 绑定。 |
| `pipeline_desc` | 值类型 PSO 描述：graphics/compute、内存/文件/bytecode shader、Diligent graphics state、显式 resource signature 列表；由 `pipeline_manager` 保留以支持 reload。 |
| `pipeline_manager` | 由 `render_device` 持有；基于 URI 管理 PSO、binding rule 和 SRB。`get_stats()` 返回 PSO/binding rule/SRB 的 ready/pending/failed 零分配聚合。SRB 统一使用 `pipeline_srb_key{ pso_uri, binding_rule, signature_index }`；`-1` 表示匿名/implicit signature，非负值表示显式 signature。所有 SRB 创建/查询 API 使用该 key，内部 map 也直接以它为键。`request_*` 同步校验后入队，`tick(frame_budget)` 在预算内执行；支持 DRSN load、desc create、手工 add、reload、clear/invalidate。失败任务保留失败条目；invalidate PSO/binding rule 会级联删除所有 signature entry。 |
| `pipeline_variant_cache` | 从 base `pipeline_desc` 派生透明、阴影、深度预 pass 变体；应用 graphics state 和 `CYBERVERSE_*` 宏，按渲染状态 + shader hash + input layout hash 复用稳定 PSO URI。 |
| `render_phrase` | 阶段基类：名称、排序模式、启停、`begin/execute/end`。阶段不拥有 GPU 资源，PSO/SRB 由调用方注入。 |
| `mesh_phrase` | 通用网格阶段：绑定颜色/深度目标、设置 viewport、按需清屏、绑定 PSO/SRB、逐项绑定顶点/索引/实例缓冲并发出 indexed/non-indexed instanced draw。支持逐 packet PSO/SRB 覆盖、多 SRB 提交和动态 variable/object 绑定。模式在构造时固定：unmanaged 直接持有 PSO/SRB 并可热更新；managed 持有 `pso_uri` / `binding_rule_uri` / `signature_index`（默认 `-1`），执行时从 pipeline manager 解析，PSO/SRB 未 ready 时安全 no-op。 |
| `opaque_phrase` | 不透明合批：front-to-back，写颜色和深度，清颜色+深度。 |
| `transparent_phrase` | 透明合批：back-to-front，读只读深度，不清屏。 |
| `shadow_phrase` | 阴影合批：depth-only 目标，front-to-back，清深度。 |
| `batch_loader` | 可拷贝/移动的场景初始化加载工具；从 `render_device` 取得 pipeline/gpu manager 裸指针（测试可直接注入 manager）。从 manifest 的 PSO/SRB/GPU URI 自动推导 PSO、binding rule 依赖和 GPU data；每 tick 在 budget 内等待资源 ready、请求 SRB，并用 texture/buffer ref 保活。缺失 PSO/binding rule/GPU data 立即失败；不负责创建这些资源。 |
| `render_phrase_list` | 按注册顺序运行已启用阶段；为每个阶段在复用 scratch 中排序，不改写调用方数组；支持传入 `frame_budget`，在 phrase 边界停止并报告 skipped/budget_exhausted。 |
| 测试与基准 | 122 个常规 Catch2 测试 + 2 个可选的真实窗口/GPU 集成测试（Vulkan 与 D3D12 可用时逐后端运行，包含真实 shader 编译、PSO/SRB 和异步 GPU data 上传），覆盖资源 URI、ref 生命周期、异步提交/版本覆盖、上传优先级/字节预算、内存预算、应用/帧上下文、shader include/bytecode、shader key/缓存/热重载、render packet 逐 packet SRB、阶段、阶段列表、pipeline manager 统计/任务/失效/预算语义、后端可用性和设备描述校验；21 组 Google Benchmark 用例覆盖 shader key、资源提交/回收、render packet 排序、阶段列表执行和 PSO 变体缓存命中。 |
| 构建与依赖 | CMake 4.x + C++23；DiligentEngine 依赖模块按可用 target 同时接入 Vulkan / Direct3D 12，并注入 `CYBERVERSE_RENDER_CORE_BACKEND_*`；`platform` / `runtime` / GLM 通过现有 `engine_dep_*` 模块解析。`CYBERVERSE_RENDER_CORE_WITH_DRSN=ON`（默认）构建 DiligentTools/RenderStateNotation，关闭时 `request_load_drsn()` 返回 `drsn_unavailable`。 |

### v0.1 关键语义

- **引用与释放。** GPU 数据没有 `release()` / `release_all()`；只有 `texture_data_ref` /
  `buffer_data_ref` 的原子引用计数。最后一个 ref 消失后，entry 在后续 `tick()` 中按
  immediate/LRU 策略删除，所有内部版本一起释放。
- **异步提交。** `submit_*()` 只做参数校验、数据快照和版本号分配，不调用 backend；tick 才计算
  大小、处理预算并提交/轮询上传。参数错误直接返回 `std::error_code`，异步错误通过
  `upload_state(version)` / `upload_error(version)` 查询。
- **版本覆盖。** 默认 `overwrite=true`；同一 URI 的新提交创建内部版本，完成后成为 current，
  `ref.get()` 后续解析到新版本。旧版本在当前 entry 生命周期内保留，受“最近 5 个版本”上限约束。
- **阶段顺序。** 阶段列表严格按注册顺序执行；同一份 render packet 列表可被不透明阶段和透明阶段
  以不同方向排序，输入数组保持不变。
- **空指针安全。** 设备上下文或 PSO 为空时，阶段的 `begin`/`execute` 是安全 no-op，便于
  无 GPU 单元测试和资源热重载窗口期。
- **后端选择。** `render_device_desc::backend` 可选 `vulkan` 或 `d3d12`；Windows 构建默认优先 D3D12，
  其他平台默认 Vulkan。`is_backend_available()` 在创建前报告当前构建是否包含该后端，
  `validate_render_device_desc()` 对不可用后端给出 `std::invalid_argument`。
- **命名迁移。** `resource_manager` → `gpu_data_manager`，`resource_type` →
  `gpu_data_type`，`resource_backend` → `detail::gpu_data_backend`，`draw_item` → `render_packet`；
  `resource_state` → `gpu_data_state`，handle 类型已删除。
- **资源命名。** `resource_uri` 使用 `@type/org/path`；解析时校验类型、字符集和路径段，统一转小写并缓存 hash；默认构造和 moved-from 对象是无效 URI。
- **异步 pipeline 请求。** `pipeline_manager::request_*` 只做即时参数校验并入队；真正创建/重载/失效在
  渲染线程的 `tick(frame_budget)` 中执行，预算耗尽时剩余任务留到下一帧。失败任务写入 map，必须通过
  `request_invalidate` / `request_invalidate_failed` 后的一次 tick 才会真正删除。
- **匿名与显式 SRB。** `pipeline_srb_key` 区分匿名 signature `-1` 和显式 signature index `0, 1, ...`；创建/查询/手工添加 SRB 都使用该 key。显式 PSO 使用匿名 SRB 会得到 failed entry；需要按实际 signature 逐个创建/查询。
- **SRB 依赖。** mutable GPU data 未 ready 时 SRB 保持 `pending`；依赖失败则 SRB `failed`。PSO 或
  binding rule 失效会级联删除依赖 SRB；PSO reload 成功后会用现有 binding rule 自动重建 SRB。
- **静态资源拒绝。** `pipeline_manager` 拒绝包含 static shader resource 的 PSO：静态/mutable 资源
  统一在 SRB 创建阶段绑定，避免 PSO 与 SRB 两套初始化路径。
- **phrase 模式固定。** `mesh_phrase` 在构造时选择 unmanaged 或 managed；之后 direct setter 只对
  unmanaged phrase 生效，managed phrase 的 URI 与 signature index 不能在运行时通过 setter 切换模式。
  managed phrase 的默认路径使用一个 signature；多 signature 可通过 `render_packet::srbs` 逐 packet 提交。
- **上下文分层。** `render_device` 持久持有 device context、`gpu_data_manager`、`shader_manager`
  和 `pipeline_manager`；`frame_context` 是每帧创建的非拥有快照，保存 `render_device*` 和帧状态，
  阶段只接收 `frame_context`。frame context 必须短于 device 生命周期。
- **shader 缓存。** `shader_manager` 按规范化 key 缓存 `IShader`；文件内容变化后 `reload()` 重新编译，
  编译失败保留上一次成功的 shader 并通过 `get_compile_log()` 暴露错误；`get_or_create_from_source()`
  支持内存源码。
- **单线程。** v0.1 的 `render_device`、`frame_context`、`gpu_data_manager`、`pipeline_manager`
  和阶段列表都不加锁；
  `shader_manager` 是唯一线程安全组件，其编译在内部互斥锁下串行执行。

### v0.2 关键语义

- **上传优先级与字节预算。** 同一 tick 内先启动高优先级上传；`set_upload_byte_budget_per_tick()` 限制
  新启动上传的总字节数。若首个上传大于预算仍会启动，避免永久饥饿。
- **异步资源上传。** `IGPUUploadManager` 可用时，缓冲以 `USAGE_DEFAULT` 创建并通过 upload manager
  调度 staging copy；纹理通过 staging texture + fence 异步拷贝。上传状态由 `gpu_data_manager::tick()`
  轮询推进，staging 资源在 backend tick 中按 fence 回收。
- **shader include/bytecode。** 文件 shader 递归展开引号 `#include`，相对路径优先，随后搜索
  `add_include_path()`；展开后的内容参与 hash/reload。bytecode shader 通过
  `get_or_create_from_bytecode()` / `pipeline_desc::add_shader_bytecode()` 进入同一缓存。
- **PSO 变体。** `pipeline_variant_cache` 将透明/阴影/深度预 pass 转换为 graphics state 和
  `CYBERVERSE_TRANSPARENT` / `CYBERVERSE_SHADOW` / `CYBERVERSE_DEPTH_PREPASS` 宏；缓存键包含
  base PSO、flags、shader hash 和 input layout hash。
- **逐 packet SRB。** `render_packet` 可直接携带 PSO/SRB、多个 SRB（相机/材质/实例）和
  `render_packet_dynamic_binding`；`mesh_phrase` 在 PSO/SRB 变化时提交，按 packet 应用动态绑定。
- **统计。** GPU data、pipeline 和 shader 统计均为只读扫描快照，不分配内存；可直接用于 overlay 和
  预算控制器。

## 已知限制

- Vulkan 后端已在 macOS/MoltenVK 上验证；Direct3D 12 后端已实现，但需要在 Windows CI/开发机上
  完成运行时验证。D3D12 仅在 Windows 构建中可用，其他平台请求 D3D12 会得到明确错误。
  Metal/D3D11/OpenGL 后端尚未接入。
- 缓冲上传在 `IGPUUploadManager` 可用时走异步 staging/copy；纹理通过 staging texture + fence 异步
  拷贝。mipmap/array 纹理的初始数据仅支持单 mip、单 slice；fence 回收在 backend tick 中完成。
- `shader_manager` 已支持递归引号 `#include` 和 bytecode，但不解析 angle includes 的编译器搜索路径，
  也没有反射信息；PSO 变体由显式 flags + 调用方提供的 shader/input-layout hash 生成，不是自动反射。
  `mesh_phrase` 已支持逐 packet PSO/SRB 和多 SRB 提交；动态绑定需要调用方提前解析 variable 指针。
- `pipeline_manager` 的 `pipeline_desc` 只覆盖 graphics（VS/PS）和 compute（CS）；mesh/ray-tracing
  管线尚未接入。`pipeline_desc` 中保存的 Diligent 指针（render pass、static sampler 等）必须由调用方
  保证长生命周期。
- managed `mesh_phrase` 的默认路径仍是一个 signature 的 SRB；逐 packet `srbs` 可提交多个
  signature，但 phrase 本身不自动按 signature index 解析。
- 没有材质/实例数据的 CPU 侧布局约定；实例缓冲由调用方按 PSO 的输入布局准备。
- 没有 bindless、纹理数组绑定、动态描述符索引或逐项材质切换；逐项状态只通过实例数据和
  同一 SRB 表达。
- 没有命令列表/多线程录制、GPU 时间戳、遮挡查询、异步计算、动态分辨率、HDR 或 MSAA 的
  管线级策略。
- 没有设备丢失/交换链重建的完整恢复路径；`resize()` 只调用 `ISwapChain::Resize()`。
- GPU data 的版本覆盖只保留每个 entry 最近 5 个版本；更旧版本的进度查询返回 `nullopt`，
  绑定到已淘汰旧版本的 SRB 需要重建。
- 上传接缝已异步化；`IGPUUploadManager` 用于缓冲，纹理使用 staging texture + GPU fence；texture 初始数据仍只支持单 mip/slice，且 fence 回收在 backend tick 中完成。
- 没有全局线程安全 submit；版本号分配是原子的，但 manager map/vector 仍要求渲染线程。
- 没有安装/导出包、版本头、CI 矩阵或 sanitizer 门禁。

## 路线图

### v0.2 — 异步资源与 PSO 管线（已完成）

**目标：** 让资源提交从“同步创建”演进为可调度、可观测的上传管线，并在 shader 缓存之上建立 PSO 管理。

- **异步资源上传（已完成基础版）。** `submit_*(uri, desc)` 异步入队并返回 ref + submit_version；
  `tick()` 推进 `pending → uploading → ready`，支持取消旧上传、版本覆盖和错误查询；`IGPUUploadManager`
  已用于缓冲上传，纹理使用 staging texture + fence 异步拷贝。
- **上传队列与预算（已完成）。** tick 时间预算、内存预算、immediate/LRU、最近 5 个版本记录、
  low/normal/high 上传优先级和每 tick 上传字节预算均已接入。
- **PSO/SRB 管线（已完成）。** URI 级 PSO 缓存、binding rule、匿名/显式 signature SRB、DRSN 加载、
  reload 和级联失效；shader 支持递归 include 和 bytecode；PSO/SRB 已接入 `render_packet`。
- **PSO 缓存（已完成）。** `pipeline_variant_cache` 以渲染状态 + shader hash + input layout hash
  生成并复用透明、阴影、深度预 pass 变体。
- **SRB 布局（已完成）。** mutable/dynamic 绑定分离；`render_packet` 支持逐 packet PSO/SRB、
  有序 SRB 集合（相机/材质/实例）和动态 variable/object 绑定。
- **资源统计（已完成）。** `gpu_data_manager_stats`、`pipeline_manager_stats` 和
  `shader_manager_stats` 提供类型/状态/字节/版本/上传字节的零分配快照。
- **验收标准。** 上传不阻塞渲染线程（缓冲路径）；ref/版本状态机确定；shader 编译失败不崩溃；
  PSO 变体缓存命中率可在基准中观测；旧版本在所有路径上安全释放。

### v0.3 — 渲染图与阶段调度（P0）

**目标：** 从“手动阶段列表”升级为显式依赖的渲染图，同时保留 v0.1 阶段 API。

- **render graph（P0）。** 资源（纹理/缓冲区）和阶段作为节点，声明读写依赖；自动推导执行顺序、
  生命周期和屏障；支持仅深度、颜色+深度、只读深度等目标模式。
- **阶段依赖（P0）。** 阴影 → 不透明 → 透明 → HUD 的显式依赖；支持一个阶段输出被多个阶段消费。
- **目标别名与瞬态资源（P1）。** 阴影图、HDR 目标、后处理链的临时纹理复用；按帧回收。
- **多视图/子视口（P1）。** 每个阶段可覆盖 viewport/scissor；支持分屏和编辑器视口。
- **GPU 时间戳（P1）。** 每阶段 GPU 时间、draw call 数、实例数；与 `runtime` 的 CPU 阶段统计
  对齐。
- **验收标准。** 阶段顺序由依赖而不是注册顺序决定；循环依赖在构建期报错；同一帧内资源
  读写冲突可检测；无 GPU 的图构建/排序测试可运行。

### v0.4 — Lua 渲染管线（P1）

**目标：** 让渲染管线可以通过 Lua 配置和扩展，而不把脚本类型泄漏进 C++ 核心。

- **管线描述（P0）。** Lua 表描述阶段顺序、目标、清屏值、PSO 变体和资源引用；C++ 侧构建
  `render_phrase_list` 或 render graph。
- **参数绑定（P0）。** 相机/材质/实例数据通过命名参数写入 SRB；脚本不直接持有 GPU 指针。
- **热重载（P1）。** 修改 Lua 管线描述后重建阶段/PSO，不重启设备；失败时保留上一版本。
- **安全边界（P0）。** 脚本只能访问注册过的资源名和阶段 API；错误带文件名/行号和阶段名。
- **验收标准。** 不重新编译 C++ 即可切换阶段顺序、清屏颜色和 PSO 变体；脚本错误不会导致
  设备丢失；Lua 关闭时核心库不产生脚本依赖。

### v1.0 — 稳定 API 与引擎集成（P1）

- **API 冻结（P0）。** 冻结 `render_device`、`frame_context`、`gpu_data_manager`、`shader_manager`、
  `resource_uri`、`render_packet`、`render_phrase` 的公共接口；破坏性变更需要迁移说明。
- **安装/导出（P0）。** `find_package(cyberverse_render_core)`、版本头、CMake 导出目标。
- **平台矩阵（P0）。** macOS（MoltenVK）、Windows（Vulkan + Direct3D 12）、Linux（Vulkan）构建和
  运行；CI 矩阵和 sanitizer 门禁。
- **最小引擎示例（P0）。** `platform` 窗口 + `runtime` 帧循环 + `camera` 矩阵 +
  shadow/opaque/transparent 三阶段 + 资源流式加载的可运行示例。
- **恢复与诊断（P1）。** 交换链重建、设备丢失、资源泄漏检测、调试标签和捕获支持。
- **文档（P1）。** 资源生命周期、阶段协议、上传预算、线程模型、着色器/PSO 缓存和 Lua 管线
  的长期文档。

## 跨仓库工作项

| 工作项 | 涉及仓库 | 优先级 | 说明 |
| --- | --- | --- | --- |
| 帧循环与呈现集成 | `render-core`、`platform`、`runtime` | P0 | 每帧一次 poll，`runtime` 提供 dt/alpha，`render-core` 负责 begin/present |
| 相机常量布局 | `render-core`、`camera`、`transform` | P0 | 统一 view/proj、裁剪空间 `[0,1]` 和反向 Z 约定 |
| 资源导入 | `render-core`、`geometry`、`vfs9` | P1 | 网格/纹理从 VFS 读取后提交到 `gpu_data_manager` |
| 动态分辨率与预算 | `render-core`、`runtime` | P1 | runtime 输出预算信号，render-core 选择降载策略 |
| 并行录制 | `render-core`、`runtime` | P2 | 渲染命令录制与任务框架对齐，保持确定性顺序 |
| 安装/导出包 | 全部 | P1 | 统一 `find_package` 和版本策略 |
| CI/基准回归 | 全部 | P0 | macOS/Windows/Linux、ASan/UBSan/TSan、基准阈值 |

## 非目标

- 不在 `render-core` 中创建窗口、轮询输入或实现帧时钟；这些属于 `platform`/`runtime`。
- 不在核心库中引入场景图、ECS、材质图或游戏逻辑类型。
- 不把 Diligent 对象的所有权暴露给 Lua；脚本只操作命名资源和阶段描述。
- 不为未实现的后端提供隐式回退；后端不可用时在创建期给出明确错误。
- 不在 v0.1 引入隐藏工作线程或异步上传线程；并行化必须有显式的确定性契约和测试。

## 验证方式

```bash
# 在 render-core 目录下配置、构建和测试；测试产物不写入仓库。
cmake -S . -B /tmp/cyberverse-render-core-test
cmake --build /tmp/cyberverse-render-core-test
ctest --test-dir /tmp/cyberverse-render-core-test --output-on-failure

# 基准
/tmp/cyberverse-render-core-test/benchmark/cyberverse_render_core_benchmarks

# 可选：真实窗口 + GPU 集成测试（Vulkan/D3D12，会短暂打开窗口）
CYBERVERSE_RENDER_CORE_TEST_GPU=1 \
  /tmp/cyberverse-render-core-test/test/cyberverse_render_core_tests \
  "render_device creates a swap chain on a real window"
```

本地开发时可以用 `CYBERVERSE_DILIGENT_DIR` 指向已有的 DiligentEngine 源码树，避免重复拉取；
macOS 需要 Vulkan SDK 或 MoltenVK 可用（例如设置 `VULKAN_SDK`）；Windows 运行 D3D12 时需要
Windows SDK 和 `fxc`。CI 和非交互环境应固定
`CYBERVERSE_DILIGENT_GIT_TAG`，不要跟随浮动分支。
