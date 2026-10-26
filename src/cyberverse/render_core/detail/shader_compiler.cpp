/**
 * @file detail/shader_compiler.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader compiler implementation
 * @version 0.2
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/detail/shader_compiler.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace cyberverse {
namespace render_core {
namespace detail {

namespace {

Diligent::SHADER_TYPE to_diligent_shader_type(shader_stage stage) noexcept {
  switch (stage) {
  case shader_stage::vertex:
    return Diligent::SHADER_TYPE_VERTEX;
  case shader_stage::pixel:
    return Diligent::SHADER_TYPE_PIXEL;
  case shader_stage::compute:
    return Diligent::SHADER_TYPE_COMPUTE;
  case shader_stage::undefined:
  default:
    return Diligent::SHADER_TYPE_UNKNOWN;
  }
}

bool starts_with_version_directive(std::string_view source) noexcept {
  std::size_t index = 0;
  while (index < source.size()) {
    const char value = source[index];
    if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
      break;
    }
    ++index;
  }
  return source.substr(index, 8) == "#version";
}

std::string lowercase_extension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return extension;
}

Diligent::SHADER_SOURCE_LANGUAGE source_language_for_path(const std::filesystem::path& path) {
  const std::string extension = lowercase_extension(path);
  if (extension == ".hlsl" || extension == ".hlsli") {
    return Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  }
  if (extension == ".glsl" || extension == ".vert" || extension == ".frag" || extension == ".comp" ||
      extension == ".geom" || extension == ".tesc" || extension == ".tese") {
    return Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
  }
  if (extension == ".msl" || extension == ".metal") {
    return Diligent::SHADER_SOURCE_LANGUAGE_MSL;
  }
  if (extension == ".wgsl") {
    return Diligent::SHADER_SOURCE_LANGUAGE_WGSL;
  }
  return Diligent::SHADER_SOURCE_LANGUAGE_DEFAULT;
}

} // namespace

diligent_shader_compiler::diligent_shader_compiler(Diligent::IRenderDevice* device) : device_{ device } {
  if (device_ == nullptr) {
    throw std::invalid_argument("diligent_shader_compiler requires a non-null render device");
  }
}

Diligent::IShader* diligent_shader_compiler::compile(const shader_key& key,
                                                     std::string_view source,
                                                     const std::filesystem::path& source_path,
                                                     std::string& compile_log) {
  compile_log.clear();

  const Diligent::SHADER_TYPE shader_type = to_diligent_shader_type(key.stage);
  if (shader_type == Diligent::SHADER_TYPE_UNKNOWN) {
    compile_log = "invalid shader stage";
    return nullptr;
  }
  if (key.name.empty()) {
    compile_log = "empty shader name";
    return nullptr;
  }

  std::vector<Diligent::ShaderMacro> macros;
  macros.reserve(key.defines.size());
  for (const shader_define& define : key.defines) {
    macros.emplace_back(define.first.c_str(), define.second.c_str());
  }

  Diligent::ShaderCreateInfo create_info;
  create_info.Desc.Name = key.name.c_str();
  create_info.Desc.ShaderType = shader_type;
  create_info.EntryPoint = key.entryPoint.c_str();
  create_info.Source = source.data();
  create_info.SourceLength = source.size();

  const std::filesystem::path language_path = source_path.empty() ? std::filesystem::path{ key.name } : source_path;
  Diligent::SHADER_SOURCE_LANGUAGE source_language = source_language_for_path(language_path);
  if (source_language == Diligent::SHADER_SOURCE_LANGUAGE_GLSL && starts_with_version_directive(source)) {
    // Diligent prepends a #version directive to GLSL sources. A source that
    // already declares one must be compiled verbatim to avoid a duplicate.
    source_language = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
  }
  create_info.SourceLanguage = source_language;

  create_info.Macros = Diligent::ShaderMacroArray{
    macros.data(),
    static_cast<Diligent::Uint32>(macros.size()),
  };

  Diligent::IShader* shader = nullptr;
  Diligent::IDataBlob* raw_log = nullptr;
  device_->CreateShader(create_info, &shader, &raw_log);

  if (raw_log != nullptr) {
    const std::size_t size = raw_log->GetSize();
    const void* data = raw_log->GetConstDataPtr();
    if (data != nullptr && size != 0) {
      compile_log.assign(static_cast<const char*>(data), size);
    }
    raw_log->Release();
  }

  if (shader == nullptr && compile_log.empty()) {
    compile_log = "shader compilation failed";
  }
  return shader;
}

Diligent::IShader*
shader_compiler::compile_bytecode(const shader_key&, std::span<const std::byte>, std::string& compile_log) {
  compile_log = "bytecode compilation is not supported by this compiler";
  return nullptr;
}

Diligent::IShader* diligent_shader_compiler::compile_bytecode(const shader_key& key,
                                                              std::span<const std::byte> bytecode,
                                                              std::string& compile_log) {
  compile_log.clear();
  const Diligent::SHADER_TYPE shader_type = to_diligent_shader_type(key.stage);
  if (shader_type == Diligent::SHADER_TYPE_UNKNOWN || key.name.empty() || bytecode.empty()) {
    compile_log = "invalid shader bytecode request";
    return nullptr;
  }

  Diligent::ShaderCreateInfo create_info;
  create_info.Desc.Name = key.name.c_str();
  create_info.Desc.ShaderType = shader_type;
  create_info.ByteCode = bytecode.data();
  create_info.ByteCodeSize = bytecode.size();

  Diligent::IShader* shader = nullptr;
  Diligent::IDataBlob* raw_log = nullptr;
  device_->CreateShader(create_info, &shader, &raw_log);
  if (raw_log != nullptr) {
    if (raw_log->GetSize() != 0) {
      compile_log.assign(static_cast<const char*>(raw_log->GetConstDataPtr()), raw_log->GetSize());
    }
    raw_log->Release();
  }
  if (shader == nullptr && compile_log.empty()) {
    compile_log = "shader bytecode creation failed";
  }
  return shader;
}

void diligent_shader_compiler::destroy(Diligent::IShader* shader) noexcept {
  if (shader != nullptr) {
    shader->Release();
  }
}

} // namespace detail
} // namespace render_core
} // namespace cyberverse
