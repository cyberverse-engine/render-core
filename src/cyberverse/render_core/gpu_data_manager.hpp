/**
 * @file gpu_data_manager.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief GPU data manager
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <RenderDevice.h>

#include "cyberverse/render_core/error.hpp"
#include "cyberverse/render_core/gpu_data_ref.hpp"
#include "cyberverse/render_core/gpu_data_type.hpp"
#include "cyberverse/render_core/resource_uri.hpp"
#include "cyberverse/runtime/frame_budget.hpp"

namespace Diligent {
struct IGPUUploadManager;
}

namespace cyberverse {
namespace render_core {

namespace detail {
class gpu_data_backend;
} // namespace detail

/**
 * @brief Upload priority
 *
 */
enum class gpu_data_upload_priority : std::uint8_t {
  low = 0,
  normal = 1,
  high = 2,
};

/**
 * @brief Texture submission description
 *
 */
struct texture_submit_desc {
  const void* data = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t mip_levels = 1;
  std::uint32_t array_size = 1;
  std::uint64_t row_pitch = 0;
  Diligent::TEXTURE_FORMAT format = Diligent::TEX_FORMAT_UNKNOWN;
  Diligent::RESOURCE_STATE target_state = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
  Diligent::BIND_FLAGS bind_flags = Diligent::BIND_SHADER_RESOURCE;
  Diligent::USAGE usage = Diligent::USAGE_IMMUTABLE;
  gpu_data_upload_priority upload_priority = gpu_data_upload_priority::normal;
  std::string debug_name;
};

/**
 * @brief Buffer submission description
 *
 */
struct buffer_submit_desc {
  const void* data = nullptr;
  std::uint64_t size = 0;
  std::uint32_t element_byte_stride = 0;
  Diligent::BUFFER_MODE mode = Diligent::BUFFER_MODE_UNDEFINED;
  Diligent::USAGE usage = Diligent::USAGE_IMMUTABLE;
  Diligent::BIND_FLAGS bind_flags = Diligent::BIND_VERTEX_BUFFER;
  gpu_data_upload_priority upload_priority = gpu_data_upload_priority::normal;
  std::string debug_name;
};

/**
 * @brief Release policy for zero-reference logical resources
 *
 */
enum class gpu_data_release_policy {
  immediate, ///< Destroy on the first tick after the last reference is released
  lru,       ///< Destroy on tick according to LRU, retire delay, and memory pressure
};

/**
 * @brief gpu_data_manager statistics snapshot
 *
 */
struct gpu_data_manager_stats {
  std::size_t texture_count = 0;
  std::size_t buffer_count = 0;
  std::size_t referenced_count = 0;
  std::size_t ready_count = 0;
  std::size_t failed_count = 0;
  std::size_t pending_count = 0;
  std::size_t uploading_count = 0;
  std::size_t canceled_count = 0;
  std::size_t releasing_count = 0;
  std::uint64_t textures_submitted = 0;
  std::uint64_t buffers_submitted = 0;
  std::uint64_t allocated_bytes = 0;
  std::uint64_t evictable_bytes = 0;
  std::uint64_t memory_budget_bytes = 0;
  std::uint64_t versions_submitted = 0;
  std::uint64_t versions_ready = 0;
  std::uint64_t versions_failed = 0;
  std::uint64_t versions_canceled = 0;
  std::uint64_t versions_pruned = 0;
  std::uint64_t resources_destroyed = 0;
  std::uint64_t texture_bytes = 0;
  std::uint64_t buffer_bytes = 0;
  std::size_t version_count = 0;
  std::uint64_t uploaded_bytes_last_tick = 0;
  std::uint64_t uploaded_bytes_total = 0;
  std::uint64_t upload_byte_budget_per_tick = 0;
};

/**
 * @brief Owns logical GPU data, submission versions, and reference lifetimes
 *
 * Every resource has a URI and entries live in one URI-keyed map. submit_*()
 * validates arguments, snapshots the source data, assigns a global
 * submit_version, and returns a logical reference. It never calls the backend.
 * tick() computes sizes, starts or cancels uploads, polls completions, promotes
 * versions, and destroys unreferenced logical resources.
 *
 * All manager APIs assert runtime::is_this_render_thread() and run without
 * locks. gpu_data_ref copy/destruction only touches atomic counts and a release
 * flag; resource destruction is always performed by tick().
 *
 */
class gpu_data_manager {
public:
  /**
   * @brief Construct a manager over an owned backend
   *
   * This is the dependency-injection seam used by tests and tools that need a
   * deterministic backend. The manager takes ownership of the backend.
   *
   * @param backend Backend to own; must not be null
   */
  explicit gpu_data_manager(std::unique_ptr<detail::gpu_data_backend> backend) noexcept;

  explicit gpu_data_manager(Diligent::IRenderDevice* device,
                            Diligent::IDeviceContext* context,
                            Diligent::IGPUUploadManager* upload_manager = nullptr);

  ~gpu_data_manager();

  gpu_data_manager(const gpu_data_manager&) = delete;
  gpu_data_manager(gpu_data_manager&&) = delete;

  gpu_data_manager& operator=(const gpu_data_manager&) = delete;
  gpu_data_manager& operator=(gpu_data_manager&&) = delete;

  /**
   * @brief Queue a texture submission
   *
   * @param uri Required texture URI
   * @param desc Texture description
   * @param overwrite Whether a live URI accepts a new version
   * @return std::expected<std::tuple<texture_data_ref, submit_version>, std::error_code>
   */
  std::expected<std::tuple<texture_data_ref, submit_version>, std::error_code>
  submit_texture(const resource_uri& uri, texture_submit_desc desc, bool overwrite = true) noexcept;

  /**
   * @brief Queue a buffer submission
   *
   * @param uri Required buffer URI
   * @param desc Buffer description
   * @param overwrite Whether a live URI accepts a new version
   * @return std::expected<std::tuple<buffer_data_ref, submit_version>, std::error_code>
   */
  std::expected<std::tuple<buffer_data_ref, submit_version>, std::error_code>
  submit_buffer(const resource_uri& uri, buffer_submit_desc desc, bool overwrite = true) noexcept;

  /**
   * @brief Get the logical texture for a URI
   *
   * @param uri Texture URI
   * @return texture_data_ref
   */
  [[nodiscard]] texture_data_ref get_texture(const resource_uri& uri) noexcept;

  /**
   * @brief Get the logical buffer for a URI
   *
   * @param uri Buffer URI
   * @return buffer_data_ref
   */
  [[nodiscard]] buffer_data_ref get_buffer(const resource_uri& uri) noexcept;

  /**
   * @brief State of the URI's logical resource
   *
   * @param uri Resource URI
   * @return gpu_data_state
   */
  [[nodiscard]] gpu_data_state get_state(const resource_uri& uri) const noexcept;

  /**
   * @brief Advance the manager by one tick
   *
   * @param budget Frame time budget; checked before every backend operation
   */
  void tick(const runtime::frame_budget& budget);

  void set_release_policy(gpu_data_release_policy policy) noexcept { release_policy_ = policy; }

  [[nodiscard]] gpu_data_release_policy release_policy() const noexcept { return release_policy_; }

  void set_retire_delay_ticks(std::uint32_t ticks) noexcept { retire_delay_ticks_ = ticks; }

  [[nodiscard]] std::uint32_t retire_delay_ticks() const noexcept { return retire_delay_ticks_; }

  void set_memory_budget_bytes(std::uint64_t bytes) noexcept { memory_budget_bytes_ = bytes; }

  [[nodiscard]] std::uint64_t memory_budget_bytes() const noexcept { return memory_budget_bytes_; }

  /**
   * @brief Set the maximum upload bytes started per tick; 0 means unlimited
   *
   * A single upload larger than the budget is still started when no upload has
   * started in the current tick, so work cannot stall forever.
   */
  void set_upload_byte_budget_per_tick(std::uint64_t bytes) noexcept { upload_byte_budget_per_tick_ = bytes; }

  [[nodiscard]] std::uint64_t upload_byte_budget_per_tick() const noexcept { return upload_byte_budget_per_tick_; }

  [[nodiscard]] std::uint64_t uploaded_bytes_last_tick() const noexcept { return uploaded_bytes_last_tick_; }

  [[nodiscard]] std::uint64_t memory_capacity_bytes() const noexcept;

  [[nodiscard]] gpu_data_manager_stats get_stats() const noexcept;

  [[nodiscard]] std::size_t live_resource_count() const noexcept;

  [[nodiscard]] std::uint64_t tick_count() const noexcept { return tick_count_; }

private:
  struct version {
    submit_version version = 0;
    gpu_data_type type = gpu_data_type::undefined;
    gpu_data_state state = gpu_data_state::pending;
    void* resource = nullptr;
    std::uint64_t size_bytes = 0;
    std::string error;
    std::unique_ptr<std::byte[]> upload_data;
    std::uint64_t upload_data_size = 0;
    texture_submit_desc texture_desc;
    buffer_submit_desc buffer_desc;
    std::uint64_t upload_ticket = 0;
    std::uint64_t last_access = 0;
    gpu_data_upload_priority priority = gpu_data_upload_priority::normal;
  };

  struct entry {
    gpu_data_type type = gpu_data_type::undefined;
    resource_uri uri;
    std::atomic<std::uint32_t> ref_count{ 0 };
    std::atomic<bool> release_requested{ false };
    submit_version current_version = 0;
    version* current = nullptr;
    version* active_upload = nullptr;
    std::deque<std::unique_ptr<version>> versions;
    gpu_data_state state = gpu_data_state::pending;
    std::string error;
    std::uint64_t release_tick = 0;
    std::uint64_t last_access = 0;
  };

  [[nodiscard]] entry* entry_at(const resource_uri& uri) noexcept;
  [[nodiscard]] const entry* entry_at(const resource_uri& uri) const noexcept;

  [[nodiscard]] texture_data_ref make_texture_ref(const resource_uri& uri) noexcept;
  [[nodiscard]] buffer_data_ref make_buffer_ref(const resource_uri& uri) noexcept;

  void retain_ref(void* entry) noexcept;
  void release_ref(void* entry) noexcept;

  [[nodiscard]] gpu_data_state entry_state(void* entry) const noexcept;
  [[nodiscard]] std::string_view entry_error(void* entry) const noexcept;
  [[nodiscard]] submit_version entry_current_version(void* entry) const noexcept;
  [[nodiscard]] std::optional<gpu_data_state> entry_version_state(void* entry, submit_version version) const noexcept;
  [[nodiscard]] std::string_view entry_version_error(void* entry, submit_version version) const noexcept;
  [[nodiscard]] Diligent::ITexture* current_texture(void* entry) const noexcept;
  [[nodiscard]] Diligent::IBuffer* current_buffer(void* entry) const noexcept;

  void assert_render_thread() const noexcept;
  void update_entry_state(entry& value) noexcept;
  void destroy_version(version& value) noexcept;
  void release_entry(entry& value) noexcept;
  void erase_entry(const resource_uri& uri) noexcept;
  void prune_versions(entry& value) noexcept;
  void cancel_superseded_upload(entry& value) noexcept;
  [[nodiscard]] bool has_budget(const runtime::frame_budget& budget) const noexcept;
  [[nodiscard]] bool make_room(std::uint64_t required_bytes) noexcept;

  std::unique_ptr<detail::gpu_data_backend> owned_backend_;
  std::unordered_map<resource_uri, std::unique_ptr<entry>, resource_uri_hash> entries_;

  std::atomic<std::uint64_t> next_submit_version_{ 1 };

  std::uint64_t tick_count_ = 0;
  gpu_data_release_policy release_policy_ = gpu_data_release_policy::lru;
  std::uint32_t retire_delay_ticks_ = 0;
  std::uint64_t memory_budget_bytes_ = 0;
  std::uint64_t allocated_bytes_ = 0;
  std::uint64_t upload_byte_budget_per_tick_ = 0;
  std::uint64_t uploaded_bytes_last_tick_ = 0;
  std::uint64_t uploaded_bytes_total_ = 0;
  std::uint64_t textures_submitted_ = 0;
  std::uint64_t buffers_submitted_ = 0;
  std::uint64_t versions_submitted_ = 0;
  std::uint64_t versions_ready_ = 0;
  std::uint64_t versions_failed_ = 0;
  std::uint64_t versions_canceled_ = 0;
  std::uint64_t versions_pruned_ = 0;
  std::uint64_t resources_destroyed_ = 0;

  friend class gpu_data_ref;
  friend class texture_data_ref;
  friend class buffer_data_ref;
};

} // namespace render_core
} // namespace cyberverse
