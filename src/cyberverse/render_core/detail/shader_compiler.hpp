/**
 * @file detail/shader_compiler.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader compiler seam and Diligent implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <RenderDevice.h>

#include "cyberverse/render_core/shader_key.hpp"

namespace cyberverse {
namespace render_core {
namespace detail {

/**
 * @brief Shader compilation backend
 *
 * The manager owns cache, file IO, and hot-reload policy. The compiler owns the
 * actual device-specific compile call. The seam lets cache and reload behavior
 * be tested without a GPU and leaves room for offline/cross-compilers.
 *
 * A compiler is only called by shader_manager while its mutex is held, so it
 * does not need to be thread-safe.
 *
 */
class shader_compiler {
public:
  shader_compiler() = default;

  virtual ~shader_compiler() = default;

  shader_compiler(const shader_compiler&) = delete;
  shader_compiler(shader_compiler&&) = delete;

  shader_compiler& operator=(const shader_compiler&) = delete;
  shader_compiler& operator=(shader_compiler&&) = delete;

  /**
   * @brief Compile a shader
   *
   * @param key Normalized shader key
   * @param source Source text
   * @param source_path Source path, empty for in-memory sources
   * @param compile_log Receives compiler output, including errors
   * @return Diligent::IShader* New shader with one reference, or null on failure
   */
  [[nodiscard]] virtual Diligent::IShader* compile(const shader_key& key,
                                                   std::string_view source,
                                                   const std::filesystem::path& source_path,
                                                   std::string& compile_log) = 0;

  /**
   * @brief Compile a shader from precompiled bytecode
   *
   * The default implementation rejects bytecode. Compilers that support it
   * override this method.
   *
   * @param key Normalized shader key
   * @param bytecode Backend-specific shader bytecode
   * @param compile_log Receives compiler output
   * @return Diligent::IShader* New shader with one reference, or null
   */
  [[nodiscard]] virtual Diligent::IShader*
  compile_bytecode(const shader_key& key, std::span<const std::byte> bytecode, std::string& compile_log);

  /**
   * @brief Destroy a shader
   *
   * @param shader Shader returned by compile
   */
  virtual void destroy(Diligent::IShader* shader) noexcept = 0;
};

/**
 * @brief DiligentEngine shader compiler
 *
 * Compiles from source text through IRenderDevice::CreateShader. The source
 * language is inferred from the file extension when possible; otherwise the
 * backend default is used (HLSL for D3D12, GLSL for Vulkan).
 *
 */
class diligent_shader_compiler final : public shader_compiler {
public:
  /**
   * @brief Construct a compiler
   *
   * @param device Render device. Must not be null.
   * @throws std::invalid_argument If device is null
   */
  explicit diligent_shader_compiler(Diligent::IRenderDevice* device);

  ~diligent_shader_compiler() override = default;

  diligent_shader_compiler(const diligent_shader_compiler&) = delete;
  diligent_shader_compiler(diligent_shader_compiler&&) = delete;

  diligent_shader_compiler& operator=(const diligent_shader_compiler&) = delete;
  diligent_shader_compiler& operator=(diligent_shader_compiler&&) = delete;

  [[nodiscard]] Diligent::IShader* compile(const shader_key& key,
                                           std::string_view source,
                                           const std::filesystem::path& source_path,
                                           std::string& compile_log) override;

  [[nodiscard]] Diligent::IShader*
  compile_bytecode(const shader_key& key, std::span<const std::byte> bytecode, std::string& compile_log) override;

  void destroy(Diligent::IShader* shader) noexcept override;

  /**
   * @brief Render device used by this compiler
   *
   * @return Diligent::IRenderDevice*
   */
  [[nodiscard]] Diligent::IRenderDevice* device() const noexcept { return device_; }

private:
  Diligent::IRenderDevice* device_ = nullptr;
};

} // namespace detail
} // namespace render_core
} // namespace cyberverse
