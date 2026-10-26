/**
 * @file pipeline_manager.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline state object registry
 * @version 0.1
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <PipelineResourceSignature.h>
#include <PipelineState.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <ShaderResourceBinding.h>

#include "cyberverse/render_core/binding_rule.hpp"
#include "cyberverse/render_core/error.hpp"
#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/pipeline_desc.hpp"
#include "cyberverse/render_core/resource_uri.hpp"
#include "cyberverse/render_core/shader_manager.hpp"
#include "cyberverse/render_core/signature_index.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/task_queue.hpp"

namespace Diligent {
struct IRenderStateNotationLoader;
}

namespace cyberverse {
namespace render_core {

/**
 * @brief Pipeline resource lifecycle state
 *
 */
enum class pipeline_resource_state { ready, pending, failed };

/**
 * @brief Pipeline resource state snapshot
 *
 */
struct pipeline_resource_status {
  pipeline_resource_state type = pipeline_resource_state::failed;
  std::string error; ///< Diagnostic message; may be set while a reloaded resource keeps its last good state
};

/**
 * @brief Pipeline manager statistics snapshot
 *
 */
struct pipeline_manager_stats {
  std::size_t pso_count = 0;
  std::size_t pso_ready = 0;
  std::size_t pso_pending = 0;
  std::size_t pso_failed = 0;
  std::size_t binding_rule_count = 0;
  std::size_t binding_rule_ready = 0;
  std::size_t binding_rule_pending = 0;
  std::size_t binding_rule_failed = 0;
  std::size_t srb_count = 0;
  std::size_t srb_ready = 0;
  std::size_t srb_pending = 0;
  std::size_t srb_failed = 0;
};

struct pipeline_srb_key {
  resource_uri pso_uri;
  resource_uri binding_rule;
  signature_index_type signature_index = anonymous_signature_index;

  bool operator==(const pipeline_srb_key&) const noexcept = default;
};

struct pipeline_srb_key_hash {
  std::size_t operator()(const pipeline_srb_key& key) const noexcept {
    std::size_t seed = key.pso_uri.hash();
    seed ^= key.binding_rule.hash() + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= static_cast<std::size_t>(key.signature_index) + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
            (seed << 6) + (seed >> 2);
    return seed;
  }
};

/**
 * @brief Owns PSOs, binding rules, and SRBs
 *
 * All request_* methods validate their arguments synchronously and enqueue a
 * task. Nothing touches the GPU or the registries until tick() runs on the
 * render thread. Tasks are processed in submission order, render-thread requests
 * first, and each task is executed only while the supplied frame_budget has
 * remaining time.
 *
 * A failed task leaves a failed entry in the corresponding map so callers can
 * inspect get_state()/get_srb_state() and the diagnostic string. Entries are
 * only removed by request_invalidate(), request_invalidate_failed(), or
 * request_clear() during a later tick. Invalidating a PSO or binding rule also
 * removes all SRBs that depend on it.
 *
 * SRBs are keyed by (pso URI, binding rule URI, signature index). An index of
 * -1 addresses the anonymous/implicit signature; non-negative values address
 * explicit resource signatures. Each key owns one SRB. Creation waits for
 * mutable GPU data to reach the ready state; the SRB stays pending until all of
 * its selected-signature dependencies are ready, and failed dependencies make
 * it failed. Dynamic bindings are resolved but left unbound for
 * per-render-packet binding.
 *
 * PSOs may have any number of explicit resource signatures. A signature with
 * static resources is rejected: all static/mutable resources are expected to be
 * bound through the SRB at creation time.
 *
 * Requests are safe to submit from other threads through runtime::task_queue.
 * tick() and all getters must run on the render thread.
 *
 */
class pipeline_manager {
public:
  /**
   * @brief Construct a pipeline manager
   *
   * @param device Render device
   * @param gpu_data GPU data manager that owns bound resources; must outlive the manager
   * @param shader_mgr Shader manager used to compile pipeline_desc shaders; must outlive the manager
   * @param drsn_loader Optional DRSN loader; required for request_load_drsn. The
   *        loader must outlive the manager and must already have parsed its input.
   *        It is only called from tick() on the render thread.
   * @throws std::invalid_argument If device is null
   */
  pipeline_manager(Diligent::IRenderDevice* device,
                   gpu_data_manager& gpu_data,
                   shader_manager& shader_mgr,
                   Diligent::IRenderStateNotationLoader* drsn_loader = nullptr);

  ~pipeline_manager();

  pipeline_manager(const pipeline_manager&) = delete;
  pipeline_manager(pipeline_manager&&) = delete;
  pipeline_manager& operator=(const pipeline_manager&) = delete;
  pipeline_manager& operator=(pipeline_manager&&) = delete;

  /**
   * @brief Queue a PSO load through the DRSN loader
   *
   * @param uri PSO URI
   * @param drsn_name DRSN name; empty means uri.to_string()
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_load_drsn(const resource_uri& uri,
                                                         std::string_view drsn_name = {}) noexcept;

  /**
   * @brief Queue a PSO creation from a pipeline description
   *
   * @param uri PSO URI
   * @param desc Pipeline description, retained for reload
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_create_pso(const resource_uri& uri, pipeline_desc desc) noexcept;

  /**
   * @brief Queue an SRB creation from a PSO URI and binding rule URI
   *
   * @param key PSO URI, binding rule URI, and signature index
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_create_srb(const pipeline_srb_key& key) noexcept;

  /**
   * @brief Queue an already-created PSO
   *
   * @param uri PSO URI
   * @param pso Pipeline state
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_add(const resource_uri& uri,
                                                   Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso) noexcept;

  /**
   * @brief Queue a binding rule
   *
   * @param uri Binding rule URI
   * @param rule Binding rule
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_add(const resource_uri& uri, binding_rule rule) noexcept;

  /**
   * @brief Queue an already-created SRB for one resource signature
   *
   * @param key PSO URI, binding rule URI, and signature index
   * @param srb Shader resource binding
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code>
  request_add(const pipeline_srb_key& key, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb) noexcept;

  /**
   * @brief Queue a reload
   *
   * Reloading a PSO rebuilds dependent SRBs. Reloading a binding rule rebuilds
   * SRBs that use it. Manually added PSOs cannot be reloaded; the entry keeps
   * its last good PSO and records an error.
   *
   * @param uri PSO or binding rule URI
   * @return std::expected<void, std::error_code>
   */
  std::expected<void, std::error_code> request_reload(const resource_uri& uri) noexcept;

  /**
   * @brief Queue invalidation of a PSO or binding rule and its SRBs
   *
   * @param uri PSO or binding rule URI
   */
  void request_invalidate(const resource_uri& uri) noexcept;

  /**
   * @brief Queue removal of every failed PSO, binding rule, and SRB
   *
   */
  void request_invalidate_failed() noexcept;

  /**
   * @brief Queue removal of all pipeline resources
   *
   */
  void request_clear() noexcept;

  /**
   * @brief Immediately drop queued tasks and all pipeline resources
   *
   * Unlike request_clear(), this does not wait for a tick. Use it during
   * render-device shutdown, after the GPU is idle and before the GPU data and
   * shader managers are cleared. Must be called on the render thread with no
   * concurrent request_* producers.
   *
   */
  void release_all() noexcept;

  /**
   * @brief Execute queued tasks and retry pending resources
   *
   * Processing stops when the budget is exhausted; remaining tasks and pending
   * resources stay queued/registered for the next tick.
   *
   * @param budget Frame task budget
   */
  void tick(const runtime::frame_budget& budget);

  /**
   * @brief Whether a PSO or binding rule URI is registered
   *
   * @param uri URI
   * @return true
   * @return false
   */
  [[nodiscard]] bool contains(const resource_uri& uri) const noexcept;

  /**
   * @brief Get the state of a PSO or binding rule
   *
   * @param uri URI
   * @return pipeline_resource_status
   */
  [[nodiscard]] pipeline_resource_status get_state(const resource_uri& uri) const;

  /**
   * @brief Get the state of an SRB
   *
   * @param key PSO URI, binding rule URI, and signature index
   * @return pipeline_resource_status
   */
  [[nodiscard]] pipeline_resource_status get_srb_state(const pipeline_srb_key& key) const;

  /**
   * @brief Get a ready PSO
   *
   * @param uri PSO URI
   * @return Diligent::IPipelineState* Null unless ready
   */
  [[nodiscard]] Diligent::IPipelineState* get_pso(const resource_uri& uri) const noexcept;

  /**
   * @brief Get a binding rule
   *
   * @param uri Binding rule URI
   * @return const binding_rule* Null when absent
   */
  [[nodiscard]] const binding_rule* get_binding_rule(const resource_uri& uri) const noexcept;

  /**
   * @brief Get a ready SRB by resource-signature index
   *
   * @param key PSO URI, binding rule URI, and signature index
   * @return Diligent::IShaderResourceBinding* Null when absent or not ready
   */
  [[nodiscard]] Diligent::IShaderResourceBinding* get_srb(const pipeline_srb_key& key) const noexcept;

  /**
   * @brief Number of SRB entries for a PSO and binding rule
   *
   * Counts anonymous and explicit signature entries, including pending and
   * failed ones.
   *
   * @param pso_uri PSO URI
   * @param br_uri Binding rule URI
   * @return std::size_t
   */
  [[nodiscard]] std::size_t get_srb_count(const resource_uri& pso_uri, const resource_uri& br_uri) const noexcept;

  /**
   * @brief Number of registered PSOs, including pending and failed entries
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t pso_count() const noexcept { return pso_map_.size(); }

  /**
   * @brief Number of registered binding rules, including pending and failed entries
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t binding_rule_count() const noexcept { return binding_rule_map_.size(); }

  /**
   * @brief Number of registered SRB sets, including pending and failed entries
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t srb_count() const noexcept { return srb_map_.size(); }

  /**
   * @brief Aggregate PSO, binding-rule, and SRB state counts
   *
   * The snapshot does not allocate.
   *
   * @return pipeline_manager_stats
   */
  [[nodiscard]] pipeline_manager_stats get_stats() const noexcept;

  /**
   * @brief Invoke a function for every PSO with a live object
   *
   * The callback must not request mutations. Must be called on the render
   * thread.
   *
   * @tparam Fn Callable with (const resource_uri&, Diligent::IPipelineState*)
   * @param fn Callback
   */
  template <typename Fn>
    requires(
        std::is_same_v<std::invoke_result_t<std::remove_cvref_t<Fn>, const resource_uri&, Diligent::IPipelineState*>,
                       void>)
  void for_each_pso(Fn&& fn) const {
    for (const auto& [uri, entry] : pso_map_) {
      if (entry.p_pso != nullptr) {
        std::invoke(fn, uri, entry.p_pso.RawPtr());
      }
    }
  }

private:
  enum class pso_from { drsn, desc, manual };

  enum class task_kind {
    load_drsn,
    create_pso,
    add_pso,
    add_binding_rule,
    create_srb,
    add_srb,
    reload,
    invalidate,
    invalidate_failed,
    clear,
  };

  struct pso_entry {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> p_pso;
    pso_from from = pso_from::manual;
    std::string drsn_name;
    pipeline_desc desc;
    pipeline_resource_state state = pipeline_resource_state::pending;
    std::string error;
  };

  struct binding_rule_entry {
    binding_rule rule;
    std::vector<resource_uri> dep_resources;
    pipeline_resource_state state = pipeline_resource_state::pending;
    std::string error;
  };

  struct srb_entry {
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> p_srb;
    std::vector<Diligent::IShaderResourceVariable*> dynamic_variables;
    std::vector<resource_uri> dep_resources;
    std::vector<texture_data_ref> texture_refs;
    std::vector<buffer_data_ref> buffer_refs;
    pipeline_resource_state state = pipeline_resource_state::pending;
    std::string error;
  };

  struct bind_result {
    std::uint32_t bound = 0;
    std::uint32_t total = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
  };

  struct task {
    task_kind kind = task_kind::clear;
    resource_uri uri;
    resource_uri binding_uri;
    pipeline_srb_key srb_key;
    std::string drsn_name;
    pipeline_desc desc;
    binding_rule rule;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  };

  bool enqueue(task value) noexcept;

  void execute_task(task& value);
  void process_pending(const runtime::frame_budget& budget);

  std::expected<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, std::error_code>
  build_pso_from_desc(const resource_uri& uri, const pipeline_desc& desc, std::string& error) noexcept;

  Diligent::RefCntAutoPtr<Diligent::IPipelineState>
  load_pso_from_drsn(const resource_uri& uri, std::string_view drsn_name, std::string& error) noexcept;

  bool has_static_resources(Diligent::IPipelineState* pso, std::string& error) const noexcept;

  void create_or_update_srb(const pipeline_srb_key& key);
  void rebuild_srbs_for_pso(const resource_uri& pso_uri);
  void rebuild_srbs_for_binding(const resource_uri& br_uri);
  void remove_srbs_for_pso(const resource_uri& pso_uri);
  void remove_srbs_for_binding(const resource_uri& br_uri);
  void invalidate_uri(const resource_uri& uri);
  void invalidate_failed();
  void clear_all();

  [[nodiscard]] bool collect_dependencies(const binding_rule& rule, std::vector<resource_uri>& dependencies) const;

  bind_result bind_mutable(const binding_rule& rule,
                           Diligent::IShaderResourceBinding* p_srb,
                           const Diligent::PipelineResourceSignatureDesc& signature,
                           srb_entry& entry);
  std::vector<Diligent::IShaderResourceVariable*>
  resolve_dynamic(const binding_rule& rule,
                  Diligent::IShaderResourceBinding* p_srb,
                  const Diligent::PipelineResourceSignatureDesc& signature);

  Diligent::IRenderDevice* device_ = nullptr;
  gpu_data_manager& gpu_data_mgr_;
  shader_manager& shader_mgr_;
  Diligent::IRenderStateNotationLoader* drsn_loader_ = nullptr;

  std::unordered_map<resource_uri, pso_entry> pso_map_;
  std::unordered_map<resource_uri, binding_rule_entry> binding_rule_map_;
  std::unordered_map<pipeline_srb_key, srb_entry, pipeline_srb_key_hash> srb_map_;

  runtime::task_queue<task> tq_;
};

} // namespace render_core
} // namespace cyberverse
