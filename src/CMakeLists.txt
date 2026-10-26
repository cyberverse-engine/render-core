cmake_minimum_required(VERSION 4.0.0)

include(FetchContent)

# GLM is header-only; don't build its library, tests, or install rules. Resolve an
# existing target first so the library can be embedded in a larger build.
if(NOT TARGET glm::glm)
  set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
  set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW ON
  )
  FetchContent_MakeAvailable(glm)
endif()

add_library(cyberverse_render_core_static_lib STATIC)
add_library(cyberverse::render_core::static_lib ALIAS cyberverse_render_core_static_lib)

target_include_directories(cyberverse_render_core_static_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Public headers
set(
  HEADER_FILES
  cyberverse/render_core.hpp
  cyberverse/render_core/batch_loader.hpp
  cyberverse/render_core/binding_rule.hpp
  cyberverse/render_core/pipeline_desc.hpp
  cyberverse/render_core/pipeline_variant.hpp
  cyberverse/render_core/pipeline_manager.hpp
  cyberverse/render_core/render_packet.hpp
  cyberverse/render_core/resource_ref.hpp
  cyberverse/render_core/resource_uri.hpp
  cyberverse/render_core/frame_context.hpp
  cyberverse/render_core/gpu_data_manager.hpp
  cyberverse/render_core/mesh_phrase.hpp
  cyberverse/render_core/render_device.hpp
  cyberverse/render_core/render_phrase.hpp
  cyberverse/render_core/render_phrase_list.hpp
  cyberverse/render_core/core_frame_prelude.hpp
  cyberverse/render_core/error.hpp
  cyberverse/render_core/gpu_data_type.hpp
  cyberverse/render_core/gpu_data_ref.hpp
  cyberverse/render_core/shader_key.hpp
  cyberverse/render_core/shader_manager.hpp
  cyberverse/render_core/signature_index.hpp
)

# Source files
set(
  SOURCE_FILES
  cyberverse/render_core/batch_loader.cpp
  cyberverse/render_core/core_frame_prelude.cpp
  cyberverse/render_core/error.cpp
  cyberverse/render_core/gpu_data_manager.cpp
  cyberverse/render_core/pipeline_desc.cpp
  cyberverse/render_core/pipeline_variant.cpp
  cyberverse/render_core/pipeline_manager.cpp
  cyberverse/render_core/mesh_phrase.cpp
  cyberverse/render_core/frame_context.cpp
  cyberverse/render_core/resource_uri.cpp
  cyberverse/render_core/render_device.cpp
  cyberverse/render_core/render_phrase_list.cpp
  cyberverse/render_core/detail/gpu_data_backend.cpp
  cyberverse/render_core/detail/shader_compiler.cpp
  cyberverse/render_core/gpu_data_ref.cpp
  cyberverse/render_core/shader_key.cpp
  cyberverse/render_core/shader_manager.cpp
)

set(
  DETAIL_HEADER_FILES
  cyberverse/render_core/detail/gpu_data_backend.hpp
  cyberverse/render_core/detail/shader_compiler.hpp
)

target_sources(cyberverse_render_core_static_lib PUBLIC ${HEADER_FILES})
target_sources(cyberverse_render_core_static_lib PRIVATE ${SOURCE_FILES} ${DETAIL_HEADER_FILES})

target_link_libraries(
  cyberverse_render_core_static_lib
  PUBLIC
  glm::glm
  cyberverse::platform::static_lib
  cyberverse::runtime::static_lib
  cyberverse::render_core::diligent
)
