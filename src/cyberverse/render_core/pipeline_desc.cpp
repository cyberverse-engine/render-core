/**
 * @file pipeline_desc.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Pipeline description implementation
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/pipeline_desc.hpp"

namespace cyberverse {
namespace render_core {

pipeline_desc& pipeline_desc::add_shader(std::string name,
                                         shader_stage stage,
                                         std::filesystem::path source,
                                         shader_defines defines,
                                         std::string entry_point) {
  pipeline_shader shader{
    make_shader_key(std::move(name), stage, std::move(defines), std::move(entry_point)),
    std::move(source),
    {},
  };

  for (pipeline_shader& existing : shaders) {
    if (existing.key.stage == stage) {
      existing = std::move(shader);
      return *this;
    }
  }

  shaders.push_back(std::move(shader));
  return *this;
}

pipeline_desc& pipeline_desc::add_shader_source(
    std::string name, shader_stage stage, std::string source, shader_defines defines, std::string entry_point) {
  pipeline_shader shader{
    make_shader_key(std::move(name), stage, std::move(defines), std::move(entry_point)),
    {},
    std::move(source),
  };

  for (pipeline_shader& existing : shaders) {
    if (existing.key.stage == stage) {
      existing = std::move(shader);
      return *this;
    }
  }

  shaders.push_back(std::move(shader));
  return *this;
}

pipeline_desc& pipeline_desc::add_shader_bytecode(std::string name,
                                                  shader_stage stage,
                                                  std::span<const std::byte> bytecode,
                                                  std::string entry_point) {
  pipeline_shader shader;
  shader.key = make_shader_key(std::move(name), stage, {}, std::move(entry_point));
  shader.bytecode.assign(bytecode.begin(), bytecode.end());
  shaders.push_back(std::move(shader));
  return *this;
}

const pipeline_shader* pipeline_desc::find_shader(shader_stage stage) const noexcept {
  for (const pipeline_shader& shader : shaders) {
    if (shader.key.stage == stage) {
      return &shader;
    }
  }
  return nullptr;
}

pipeline_desc&
pipeline_desc::add_resource_signature(Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> signature) {
  resource_signatures.push_back(std::move(signature));
  return *this;
}

} // namespace render_core
} // namespace cyberverse
