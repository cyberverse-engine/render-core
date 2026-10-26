include_guard(GLOBAL)
include(FetchContent)

# Resolve the DiligentEngine targets used by render-core.
# Idempotent: calling it more than once is safe.
#
# Resolution order:
#   1. Already-defined Diligent targets (superproject or an earlier call).
#   2. A source tree: CYBERVERSE_DILIGENT_DIR, its environment equivalent, or
#      the sibling ../DiligentEngine directory.
#   3. FetchContent from CYBERVERSE_DILIGENT_GIT_REPOSITORY.
#
# render-core implements the Vulkan and Direct3D 12 backends of DiligentEngine.
# At least one backend target must be available; Vulkan is used on macOS/Linux,
# D3D12 on Windows. Both may be available in the same Windows build.
function(resolve_engine_dep_diligent)
  if(TARGET cyberverse::render_core::diligent)
    return()
  endif()

  if(NOT TARGET Diligent-GraphicsEngineVk-static AND NOT TARGET Diligent-GraphicsEngineD3D12-static)
    if(TARGET Diligent-GraphicsEngine)
      message(FATAL_ERROR
        "DiligentEngine is already configured without a supported backend. "
        "render-core requires Diligent-GraphicsEngineVk-static or "
        "Diligent-GraphicsEngineD3D12-static; enable Vulkan or Direct3D 12 in the "
        "DiligentEngine configuration, or provide a suitable DiligentEngine source.")
    endif()

    # render-core only needs DiligentCore, no samples or FX modules. Tools are
    # required when DRSN support is enabled.
    if(CYBERVERSE_RENDER_CORE_WITH_DRSN OR CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER)
      set(DILIGENT_BUILD_TOOLS ON CACHE BOOL "Build DiligentTools" FORCE)
      set(DILIGENT_NO_RENDER_STATE_PACKAGER ON CACHE BOOL "Disable the render state packager" FORCE)
    else()
      set(DILIGENT_BUILD_TOOLS OFF CACHE BOOL "Build DiligentTools" FORCE)
    endif()
    set(DILIGENT_BUILD_FX OFF CACHE BOOL "Build DiligentFX" FORCE)
    set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "Build DiligentSamples" FORCE)
    set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "Build Diligent tests" FORCE)
    set(DILIGENT_BUILD_DOCS OFF CACHE BOOL "Build Diligent docs" FORCE)
    set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "Disable the Diligent archiver" FORCE)
    set(DILIGENT_NO_SUPER_RESOLUTION ON CACHE BOOL "Disable Diligent super resolution" FORCE)
    set(DILIGENT_NO_OPENGL ON CACHE BOOL "Disable the OpenGL backend" FORCE)
    set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "Disable the Direct3D11 backend" FORCE)
    set(DILIGENT_NO_METAL ON CACHE BOOL "Disable the native Metal backend" FORCE)
    set(DILIGENT_NO_WEBGPU ON CACHE BOOL "Disable the WebGPU backend" FORCE)

    set(_diligent_dir "${CYBERVERSE_DILIGENT_DIR}")

    if(NOT _diligent_dir AND DEFINED ENV{CYBERVERSE_DILIGENT_DIR})
      set(_diligent_dir "$ENV{CYBERVERSE_DILIGENT_DIR}")
    endif()

    if(NOT _diligent_dir)
      set(_diligent_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../DiligentEngine")
    endif()

    get_filename_component(_diligent_dir "${_diligent_dir}" ABSOLUTE)

    if(_diligent_dir AND EXISTS "${_diligent_dir}/CMakeLists.txt")
      message(STATUS "DiligentEngine: resolved from local source ${_diligent_dir}")
      add_subdirectory(
        "${_diligent_dir}"
        "${CMAKE_BINARY_DIR}/_deps/diligent_build"
        EXCLUDE_FROM_ALL
      )
    else()
      set(
        CYBERVERSE_DILIGENT_GIT_REPOSITORY
        "https://github.com/DiligentGraphics/DiligentEngine.git"
        CACHE STRING "Git repository used to fetch DiligentEngine"
      )
      set(
        CYBERVERSE_DILIGENT_GIT_TAG
        "API256015"
        CACHE STRING "Git tag, branch, or commit used to fetch DiligentEngine"
      )
      option(CYBERVERSE_DILIGENT_GIT_SHALLOW "Use a shallow clone when fetching DiligentEngine" ON)

      message(
        STATUS
        "DiligentEngine: fetching ${CYBERVERSE_DILIGENT_GIT_REPOSITORY} (${CYBERVERSE_DILIGENT_GIT_TAG})"
      )
      FetchContent_Declare(
        cyberverse_diligent
        GIT_REPOSITORY "${CYBERVERSE_DILIGENT_GIT_REPOSITORY}"
        GIT_TAG "${CYBERVERSE_DILIGENT_GIT_TAG}"
        GIT_SHALLOW "${CYBERVERSE_DILIGENT_GIT_SHALLOW}"
      )
      FetchContent_MakeAvailable(cyberverse_diligent)
    endif()
  endif()

  set(_cyberverse_diligent_backends)
  set(_cyberverse_diligent_definitions)

  if(TARGET Diligent-GraphicsEngineVk-static)
    list(APPEND _cyberverse_diligent_backends Diligent-GraphicsEngineVk-static)
    list(APPEND _cyberverse_diligent_definitions CYBERVERSE_RENDER_CORE_BACKEND_VULKAN=1)
  endif()

  if(TARGET Diligent-GraphicsEngineD3D12-static)
    list(APPEND _cyberverse_diligent_backends Diligent-GraphicsEngineD3D12-static)
    list(APPEND _cyberverse_diligent_definitions CYBERVERSE_RENDER_CORE_BACKEND_D3D12=1)
  endif()

  if(CYBERVERSE_RENDER_CORE_WITH_DRSN AND TARGET Diligent-RenderStateNotation)
    list(APPEND _cyberverse_diligent_backends Diligent-RenderStateNotation)
    list(APPEND _cyberverse_diligent_definitions CYBERVERSE_RENDER_CORE_WITH_DRSN=1)
    message(STATUS "DiligentEngine: DRSN support enabled")
  elseif(CYBERVERSE_RENDER_CORE_WITH_DRSN)
    message(WARNING "DiligentEngine: DRSN support requested but Diligent-RenderStateNotation is unavailable")
  endif()

  if(TARGET Diligent-GraphicsTools)
    list(APPEND _cyberverse_diligent_backends Diligent-GraphicsTools)
    list(APPEND _cyberverse_diligent_definitions CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER=1)
    message(STATUS "DiligentEngine: GPU upload manager enabled")
  endif()

  if(NOT _cyberverse_diligent_backends)
    message(FATAL_ERROR
      "DiligentEngine: neither Diligent-GraphicsEngineVk-static nor "
      "Diligent-GraphicsEngineD3D12-static is available. render-core requires at least one "
      "supported backend (Vulkan on macOS/Linux, Vulkan or Direct3D 12 on Windows). "
      "Provide CYBERVERSE_DILIGENT_DIR, enable a backend, or set "
      "CYBERVERSE_DILIGENT_GIT_REPOSITORY.")
  endif()

  if(NOT TARGET cyberverse_render_core_diligent)
    add_library(cyberverse_render_core_diligent INTERFACE)
    add_library(cyberverse::render_core::diligent ALIAS cyberverse_render_core_diligent)
    target_link_libraries(
      cyberverse_render_core_diligent
      INTERFACE
      ${_cyberverse_diligent_backends}
      Diligent-GraphicsAccessories
    )
    target_compile_definitions(
      cyberverse_render_core_diligent
      INTERFACE ${_cyberverse_diligent_definitions}
    )
  endif()
endfunction()
