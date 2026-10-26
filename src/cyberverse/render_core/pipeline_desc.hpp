/**
 * @file pipeline_desc.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline description
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <PipelineState.h>
#include <RefCntAutoPtr.hpp>

#include "cyberverse/render_core/shader_key.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Pipeline kind
 *
 */
enum class pipeline_kind { graphics, compute };

/**
 * @brief Source of one pipeline shader stage
 *
 */
struct pipeline_shader {
  shader_key key;                  ///< Logical shader key
  std::filesystem::path source;    ///< Source file; empty means key.name
  std::string source_text;         ///< In-memory source; takes precedence over source
  std::vector<std::byte> bytecode; ///< Precompiled bytecode; takes precedence over source text
};

/**
 * @brief Description used to create a pipeline state object
 *
 * The description is stored by pipeline_manager for reload. Explicit resource
 * signatures are preferred; when none are supplied, Diligent's implicit
 * signature is used from PipelineStateDesc::ResourceLayout. Any pointers stored
 * in PipelineStateDesc::ResourceLayout or GraphicsPipelineDesc (render pass,
 * static samplers, ...) must outlive the pipeline manager and all reloads.
 *
 * Shader sources are loaded through shader_manager. A shader whose source path
 * is empty falls back to shader_key.name.
 *
 */
struct pipeline_desc {
  pipeline_kind kind = pipeline_kind::graphics;
  std::vector<pipeline_shader> shaders;
  Diligent::PipelineStateDesc pso_desc;
  Diligent::GraphicsPipelineDesc graphics_desc;
  Diligent::PSO_CREATE_FLAGS flags = Diligent::PSO_CREATE_FLAG_NONE;
  std::vector<Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature>> resource_signatures;

  /**
   * @brief Add a shader stage
   *
   * @param name Logical shader name
   * @param stage Shader stage
   * @param source Source path; empty means use name
   * @param defines Macro definitions
   * @param entry_point Entry point
   * @return pipeline_desc&
   */
  pipeline_desc& add_shader(std::string name,
                            shader_stage stage,
                            std::filesystem::path source = {},
                            shader_defines defines = {},
                            std::string entry_point = "main");

  /**
   * @brief Add an in-memory shader stage
   *
   * @param name Logical shader name
   * @param stage Shader stage
   * @param source Source text
   * @param defines Macro definitions
   * @param entry_point Entry point
   * @return pipeline_desc&
   */
  pipeline_desc& add_shader_source(std::string name,
                                   shader_stage stage,
                                   std::string source,
                                   shader_defines defines = {},
                                   std::string entry_point = "main");

  /**
   * @brief Add an in-memory bytecode shader stage
   *
   * @param name Logical shader name
   * @param stage Shader stage
   * @param bytecode Precompiled backend bytecode
   * @param entry_point Entry point retained for diagnostics
   * @return pipeline_desc&
   */
  pipeline_desc& add_shader_bytecode(std::string name,
                                     shader_stage stage,
                                     std::span<const std::byte> bytecode,
                                     std::string entry_point = "main");

  /**
   * @brief Find a shader by stage
   *
   * @param stage Shader stage
   * @return const pipeline_shader* Null when absent
   */
  [[nodiscard]] const pipeline_shader* find_shader(shader_stage stage) const noexcept;

  /**
   * @brief Add an explicit resource signature
   *
   * @param signature Resource signature
   * @return pipeline_desc&
   */
  pipeline_desc& add_resource_signature(Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> signature);
};

} // namespace render_core
} // namespace cyberverse
