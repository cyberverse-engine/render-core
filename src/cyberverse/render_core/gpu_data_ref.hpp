/**
 * @file gpu_data_ref.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Reference-counted GPU data references
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "cyberverse/render_core/gpu_data_type.hpp"

namespace Diligent {
struct IBuffer;
struct ITexture;
} // namespace Diligent

namespace cyberverse {
namespace render_core {

class gpu_data_manager;

/**
 * @brief Monotonic submission version
 *
 */
using submit_version = std::uint64_t;

/**
 * @brief Lifecycle state of GPU data and submission versions
 *
 */
enum class gpu_data_state {
  pending,   ///< Submission accepted, upload has not started
  uploading, ///< Upload request is in flight
  ready,     ///< Version is usable
  failed,    ///< Upload or creation failed
  canceled,  ///< Superseded by a later submission
  releasing, ///< Reference count reached zero, waiting for tick
};

/**
 * @brief Reference-counted logical GPU resource
 *
 * A reference refers to a logical URI resource, not to a particular submitted
 * version. Versions are an internal implementation detail of gpu_data_manager.
 * Copying a reference increments the resource's atomic reference count;
 * destroying it decrements it. The manager performs every destruction in tick().
 * References must be destroyed before the manager they came from.
 *
 */
class gpu_data_ref {
public:
  gpu_data_ref() noexcept = default;

  gpu_data_ref(const gpu_data_ref& other) noexcept;
  gpu_data_ref(gpu_data_ref&& other) noexcept;

  gpu_data_ref& operator=(const gpu_data_ref& other) noexcept;
  gpu_data_ref& operator=(gpu_data_ref&& other) noexcept;

  ~gpu_data_ref();

  /**
   * @brief Logical resource type
   *
   * @return gpu_data_type
   */
  [[nodiscard]] gpu_data_type type() const noexcept { return type_; }

  /**
   * @brief Whether this reference owns a manager entry
   *
   * A failed or pending resource still has a valid reference.
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_valid() const noexcept { return manager_ != nullptr; }

  /**
   * @brief Whether the current version is ready
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool is_ready() const noexcept;

  /**
   * @brief Current logical resource state
   *
   * @return gpu_data_state
   */
  [[nodiscard]] gpu_data_state state() const noexcept;

  /**
   * @brief Error of the current version
   *
   * A non-empty error means there is no usable current version.
   *
   * @return std::string_view
   */
  [[nodiscard]] std::string_view error() const noexcept;

  /**
   * @brief Current version number; 0 when there is no current version
   *
   * @return submit_version
   */
  [[nodiscard]] submit_version current_version() const noexcept;

  /**
   * @brief Upload state of a submitted version
   *
   * @param version Version returned by submit_*()
   * @return std::optional<gpu_data_state> Null when the version record was pruned
   */
  [[nodiscard]] std::optional<gpu_data_state> upload_state(submit_version version) const noexcept;

  /**
   * @brief Error of a submitted version
   *
   * @param version Version returned by submit_*()
   * @return std::string_view
   */
  [[nodiscard]] std::string_view upload_error(submit_version version) const noexcept;

  /**
   * @brief Explicit bool conversion; true when the current version is ready
   *
   * @return true
   * @return false
   */
  [[nodiscard]] explicit operator bool() const noexcept { return is_ready(); }

protected:
  gpu_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept;

  void reset() noexcept;

  gpu_data_manager* manager_ = nullptr;
  void* entry_ = nullptr;
  gpu_data_type type_ = gpu_data_type::undefined;
};

/**
 * @brief Reference-counted texture
 *
 */
class texture_data_ref final : public gpu_data_ref {
public:
  texture_data_ref() noexcept = default;

  texture_data_ref(const texture_data_ref& other) noexcept;
  texture_data_ref(texture_data_ref&& other) noexcept;

  texture_data_ref& operator=(const texture_data_ref& other) noexcept;
  texture_data_ref& operator=(texture_data_ref&& other) noexcept;

  ~texture_data_ref() = default;

  /**
   * @brief Current texture
   *
   * @return Diligent::ITexture* Null while the current version is not ready
   */
  [[nodiscard]] Diligent::ITexture* get() const noexcept;

  [[nodiscard]] Diligent::ITexture* operator->() const noexcept { return get(); }

  [[nodiscard]] Diligent::ITexture& operator*() const noexcept { return *get(); }

  /**
   * @brief Release this reference
   *
   */
  void reset() noexcept { gpu_data_ref::reset(); }

private:
  friend class gpu_data_manager;

  texture_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept;
};

/**
 * @brief Reference-counted buffer
 *
 */
class buffer_data_ref final : public gpu_data_ref {
public:
  buffer_data_ref() noexcept = default;

  buffer_data_ref(const buffer_data_ref& other) noexcept;
  buffer_data_ref(buffer_data_ref&& other) noexcept;

  buffer_data_ref& operator=(const buffer_data_ref& other) noexcept;
  buffer_data_ref& operator=(buffer_data_ref&& other) noexcept;

  ~buffer_data_ref() = default;

  /**
   * @brief Current buffer
   *
   * @return Diligent::IBuffer* Null while the current version is not ready
   */
  [[nodiscard]] Diligent::IBuffer* get() const noexcept;

  [[nodiscard]] Diligent::IBuffer* operator->() const noexcept { return get(); }

  [[nodiscard]] Diligent::IBuffer& operator*() const noexcept { return *get(); }

  /**
   * @brief Release this reference
   *
   */
  void reset() noexcept { gpu_data_ref::reset(); }

private:
  friend class gpu_data_manager;

  buffer_data_ref(gpu_data_manager* manager, void* entry, gpu_data_type type) noexcept;
};

} // namespace render_core
} // namespace cyberverse
