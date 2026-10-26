/**
 * @file shader_key.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Shader key implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/shader_key.hpp"

#include <algorithm>
#include <functional>

namespace cyberverse {
namespace render_core {

namespace {

void hash_combine(std::size_t& seed, std::size_t value) noexcept {
  seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
}

bool define_less(const shader_define& lhs, const shader_define& rhs) noexcept {
  if (lhs.first != rhs.first) {
    return lhs.first < rhs.first;
  }
  return lhs.second < rhs.second;
}

} // namespace

shader_key normalize_shader_key(shader_key key) noexcept {
  std::sort(key.defines.begin(), key.defines.end(), define_less);
  if (key.entryPoint.empty()) {
    key.entryPoint = "main";
  }
  return key;
}

shader_key
make_shader_key(std::string name, shader_stage stage, shader_defines defines, std::string entryPoint) noexcept {
  shader_key key;
  key.name = std::move(name);
  key.stage = stage;
  key.entryPoint = std::move(entryPoint);
  key.defines = std::move(defines);
  return normalize_shader_key(std::move(key));
}

std::size_t shader_key_hash::operator()(const shader_key& key) const noexcept {
  std::size_t seed = std::hash<std::string>{}(key.name);
  hash_combine(seed, std::hash<int>{}(static_cast<int>(key.stage)));
  hash_combine(seed, std::hash<std::string>{}(key.entryPoint));
  for (const shader_define& define : key.defines) {
    hash_combine(seed, std::hash<std::string>{}(define.first));
    hash_combine(seed, std::hash<std::string>{}(define.second));
  }
  return seed;
}

} // namespace render_core
} // namespace cyberverse
