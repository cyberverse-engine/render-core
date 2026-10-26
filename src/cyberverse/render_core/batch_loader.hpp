/**
 * @file batch_loader.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Batch scene resource loader
 * @version 0.1
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "cyberverse/render_core/gpu_data_manager.hpp"
#include "cyberverse/render_core/pipeline_manager.hpp"
#include "cyberverse/render_core/resource_uri.hpp"
#include "cyberverse/runtime/frame_budget.hpp"

namespace cyberverse {
namespace render_core {

class render_device;

/**
 * @brief Resources requested by a scene load
 *
 */
struct batch_load_manifest {
  std::vector<resource_uri> gpu_data; ///< Texture/buffer URIs that must be ready
  std::vector<resource_uri> pso_uris; ///< PSO URIs that must be ready
  std::vector<pipeline_srb_key> srbs; ///< SRBs that must be created
};

/**
 * @brief Batch loader lifecycle state
 *
 */
enum class batch_load_state { pending, loading, ready, failed, cancelled };

/**
 * @brief Kind of batch-load failure
 *
 */
enum class batch_load_error_kind { manifest, pso, binding_rule, gpu_data, srb };

/**
 * @brief One batch-load failure
 *
 */
struct batch_load_error {
  batch_load_error_kind kind = batch_load_error_kind::manifest;
  resource_uri uri;     ///< Resource URI associated with the failure
  pipeline_srb_key srb; ///< SRB key when kind == srb
  std::string message;  ///< Diagnostic message
};

/**
 * @brief Asynchronous readiness and reference owner for a scene load
 *
 * batch_loader expands the manifest by inferring PSO dependencies from SRBs and
 * GPU data dependencies from registered binding rules. It does not create GPU
 * data, PSOs, or binding rules; those resources must already be submitted or
 * registered with the managers. It does request SRB creation once its PSO and
 * binding rule are ready, and it holds texture_data_ref / buffer_data_ref
 * objects for every GPU dependency until the loader is destroyed or cancelled.
 *
 * The loader is driven by tick() and stops on the first failure. The caller
 * should create it only after the scene's PSO/binding-rule requests have been
 * enqueued; the normal pipeline tick processes those requests before the scene
 * stage ticks the loader.
 *
 */
class batch_loader {
public:
  /**
   * @brief Construct a loader and expand the manifest
   *
   * Invalid URI types and malformed SRB keys produce a failed loader
   * immediately. Missing resources are reported by tick().
   *
   * @param manifest Requested resources
   * @param device Render device that owns the pipeline and GPU data managers
   */
  batch_loader(batch_load_manifest manifest, render_device& device);

  /**
   * @brief Construct a loader from already-owned managers
   *
   * This overload is for hosts that inject managers directly, such as tests or
   * tools that do not own a render_device.
   *
   * @param manifest Requested resources
   * @param pipelines Pipeline manager
   * @param gpu_data GPU data manager
   */
  batch_loader(batch_load_manifest manifest, pipeline_manager& pipelines, gpu_data_manager& gpu_data);

  batch_loader(const batch_loader&) = default;
  batch_loader(batch_loader&&) noexcept = default;
  batch_loader& operator=(const batch_loader&) = default;
  batch_loader& operator=(batch_loader&&) noexcept = default;

  ~batch_loader() = default;

  /**
   * @brief Whether the loader is ready
   *
   * @return true
   * @return false
   */
  [[nodiscard]] operator bool() const noexcept;

  /**
   * @brief Expanded manifest
   *
   * Inferred PSOs and GPU data are appended as binding rules become available.
   * SRBs are preserved from the input.
   *
   * @return const batch_load_manifest&
   */
  [[nodiscard]] const batch_load_manifest& manifest() const noexcept;

  /**
   * @brief Advance loading while the budget has time
   *
   * The budget is checked before each dependency is processed. Remaining work
   * stays queued for the next tick.
   *
   * @param budget Frame budget
   */
  void tick(const runtime::frame_budget& budget);

  /**
   * @brief Cancel loading and release all GPU references
   *
   */
  void cancel() noexcept;

  /**
   * @brief Current loader state
   *
   * @return batch_load_state
   */
  [[nodiscard]] batch_load_state state() const noexcept;

  /**
   * @brief Fraction of dependencies that are ready, in [0, 1]
   *
   * @return float
   */
  [[nodiscard]] float progress() const noexcept;

  /**
   * @brief Failures recorded by the loader
   *
   * @return std::span<const batch_load_error>
   */
  [[nodiscard]] std::span<const batch_load_error> errors() const noexcept;

private:
  struct pso_item {
    resource_uri uri;
    bool ready = false;
  };

  struct rule_item {
    resource_uri uri;
    bool inferred = false;
    bool ready = false;
  };

  struct gpu_item {
    resource_uri uri;
    bool acquired = false;
  };

  struct srb_item {
    pipeline_srb_key key;
    bool requested = false;
    bool ready = false;
  };

  bool expand_manifest();
  bool infer_binding_rule(const resource_uri& uri);
  void add_pso(const resource_uri& uri);
  void add_rule(const resource_uri& uri);
  void add_gpu(const resource_uri& uri);
  void fail(batch_load_error error);
  void release_gpu_refs() noexcept;
  void reset() noexcept;
  [[nodiscard]] std::size_t ready_count() const noexcept;
  [[nodiscard]] std::size_t total_count() const noexcept;
  void process_pso(const runtime::frame_budget& budget);
  void process_rules(const runtime::frame_budget& budget);
  void process_gpu(const runtime::frame_budget& budget);
  void process_srbs(const runtime::frame_budget& budget);

  pipeline_manager* pipelines_ = nullptr;
  gpu_data_manager* gpu_data_ = nullptr;
  batch_load_manifest manifest_;
  batch_load_state state_ = batch_load_state::pending;
  std::vector<batch_load_error> errors_;

  std::vector<pso_item> pso_items_;
  std::vector<rule_item> rule_items_;
  std::vector<gpu_item> gpu_items_;
  std::vector<srb_item> srb_items_;

  std::vector<texture_data_ref> texture_refs_;
  std::vector<buffer_data_ref> buffer_refs_;
};

} // namespace render_core
} // namespace cyberverse
