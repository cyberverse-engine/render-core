/**
 * @file detail/gpu_data_backend.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief GPU data creation backend
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <DeviceContext.h>
#include <RenderDevice.h>

namespace Diligent {
struct IGPUUploadManager;
}

namespace cyberverse {
namespace render_core {

struct texture_submit_desc;
struct buffer_submit_desc;

namespace detail {

/**
 * @brief State of an asynchronous upload request
 *
 */
enum class gpu_data_upload_state {
  pending,
  ready,
  failed,
};

/**
 * @brief Opaque upload request ticket
 *
 */
struct gpu_data_upload_ticket {
  std::uint64_t id = 0;

  [[nodiscard]] bool is_valid() const noexcept { return id != 0; }
};

/**
 * @brief GPU data creation and upload interface used by gpu_data_manager
 *
 * The manager owns logical resources, versions, reference counting, and
 * deferred release. The backend owns GPU object creation, upload execution,
 * and destruction. submit_*() never calls the backend; tick() requests uploads
 * and polls their completion.
 *
 */
class gpu_data_backend {
public:
  gpu_data_backend() = default;

  virtual ~gpu_data_backend() = default;

  gpu_data_backend(const gpu_data_backend&) = delete;
  gpu_data_backend(gpu_data_backend&&) = delete;

  gpu_data_backend& operator=(const gpu_data_backend&) = delete;
  gpu_data_backend& operator=(gpu_data_backend&&) = delete;

  [[nodiscard]] virtual Diligent::ITexture* create_texture(const texture_submit_desc& desc) = 0;

  [[nodiscard]] virtual Diligent::IBuffer* create_buffer(const buffer_submit_desc& desc) = 0;

  virtual void destroy_texture(Diligent::ITexture* texture) noexcept = 0;

  virtual void destroy_buffer(Diligent::IBuffer* buffer) noexcept = 0;

  /**
   * @brief Release backend-internal resources that are no longer in flight
   *
   * Called by gpu_data_manager::tick(). The default implementation is a no-op.
   */
  virtual void tick() noexcept {}

  /**
   * @brief Request a texture upload
   *
   * Implementations must consume or copy desc.data before returning. A null or
   * invalid ticket means the request failed immediately; error receives the
   * reason.
   *
   */
  [[nodiscard]] virtual gpu_data_upload_ticket request_texture_upload(const texture_submit_desc& desc,
                                                                      std::string& error) = 0;

  [[nodiscard]] virtual gpu_data_upload_ticket request_buffer_upload(const buffer_submit_desc& desc,
                                                                     std::string& error) = 0;

  /**
   * @brief Poll a texture upload
   *
   * On ready, texture receives the created object and ownership transfers to
   * the manager. On failed, error receives the reason. The ticket is consumed
   * on ready or failed.
   *
   */
  [[nodiscard]] virtual gpu_data_upload_state
  poll_texture_upload(gpu_data_upload_ticket ticket, Diligent::ITexture*& texture, std::string& error) = 0;

  [[nodiscard]] virtual gpu_data_upload_state
  poll_buffer_upload(gpu_data_upload_ticket ticket, Diligent::IBuffer*& buffer, std::string& error) = 0;

  virtual void cancel_texture_upload(gpu_data_upload_ticket ticket) noexcept = 0;

  virtual void cancel_buffer_upload(gpu_data_upload_ticket ticket) noexcept = 0;

  /**
   * @brief Adapter memory capacity in bytes; 0 when unknown
   *
   */
  [[nodiscard]] virtual std::uint64_t memory_capacity_bytes() const noexcept { return 0; }
};

/**
 * @brief DiligentEngine GPU data backend
 *
 * The current implementation creates the GPU object inside request_* and
 * returns it on the first poll in the next tick. The interface is ready for a
 * truly asynchronous IGPUUploadManager implementation.
 *
 */
class diligent_gpu_data_backend final : public gpu_data_backend {
public:
  explicit diligent_gpu_data_backend(Diligent::IRenderDevice* device,
                                     Diligent::IDeviceContext* context = nullptr,
                                     Diligent::IGPUUploadManager* upload_manager = nullptr);

  ~diligent_gpu_data_backend() override;

  diligent_gpu_data_backend(const diligent_gpu_data_backend&) = delete;
  diligent_gpu_data_backend(diligent_gpu_data_backend&&) = delete;

  diligent_gpu_data_backend& operator=(const diligent_gpu_data_backend&) = delete;
  diligent_gpu_data_backend& operator=(diligent_gpu_data_backend&&) = delete;

  [[nodiscard]] Diligent::ITexture* create_texture(const texture_submit_desc& desc) override;

  [[nodiscard]] Diligent::IBuffer* create_buffer(const buffer_submit_desc& desc) override;

  void destroy_texture(Diligent::ITexture* texture) noexcept override;

  void destroy_buffer(Diligent::IBuffer* buffer) noexcept override;

  void tick() noexcept override;

  [[nodiscard]] gpu_data_upload_ticket request_texture_upload(const texture_submit_desc& desc,
                                                              std::string& error) override;

  [[nodiscard]] gpu_data_upload_ticket request_buffer_upload(const buffer_submit_desc& desc,
                                                             std::string& error) override;

  [[nodiscard]] gpu_data_upload_state
  poll_texture_upload(gpu_data_upload_ticket ticket, Diligent::ITexture*& texture, std::string& error) override;

  [[nodiscard]] gpu_data_upload_state
  poll_buffer_upload(gpu_data_upload_ticket ticket, Diligent::IBuffer*& buffer, std::string& error) override;

  void cancel_texture_upload(gpu_data_upload_ticket ticket) noexcept override;

  void cancel_buffer_upload(gpu_data_upload_ticket ticket) noexcept override;

  [[nodiscard]] std::uint64_t memory_capacity_bytes() const noexcept override;

  [[nodiscard]] Diligent::IRenderDevice* device() const noexcept { return device_; }

  [[nodiscard]] Diligent::IGPUUploadManager* upload_manager() const noexcept { return upload_manager_; }

  [[nodiscard]] Diligent::IDeviceContext* context() const noexcept { return context_; }

private:
  struct pending_upload;
  struct upload_store;
  struct deferred_upload;

  Diligent::IRenderDevice* device_ = nullptr;
  Diligent::IDeviceContext* context_ = nullptr;
  Diligent::IGPUUploadManager* upload_manager_ = nullptr;
  std::unique_ptr<upload_store> uploads_;
};

} // namespace detail
} // namespace render_core
} // namespace cyberverse
