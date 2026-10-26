include_guard(GLOBAL)
include(FetchContent)

# Resolve cyberverse::platform::static_lib for the platform library.
# Idempotent: calling it more than once is safe.
#
# Resolution order:
# 1. An already-defined target (superproject or an earlier call).
# 2. An installed cyberverse_platform CMake package.
# 3. A platform source tree: CYBERVERSE_PLATFORM_DIR, its environment
# equivalent, or the sibling ../../platform directory.
# 4. FetchContent from CYBERVERSE_PLATFORM_GIT_REPOSITORY.
function(resolve_engine_dep_platform)
  if(TARGET cyberverse::platform::static_lib)
    return()
  endif()

  # Geometry is a dependency here, so never build its tests or benchmarks.
  set(CYBERVERSE_PLATFORM_BUILD_TESTS OFF CACHE BOOL "Build cyberverse_platform tests" FORCE)
  set(CYBERVERSE_PLATFORM_BUILD_BENCHMARKS OFF CACHE BOOL "Build cyberverse_platform benchmarks" FORCE)

  # First: an installed package.
  find_package(cyberverse_platform CONFIG QUIET)

  if(TARGET cyberverse::platform::static_lib)
    message(STATUS "cyberverse_platform: resolved from installed package")
    return()
  endif()

  # Second: a source tree injected by the workspace or the environment.
  set(_dir "${CYBERVERSE_PLATFORM_DIR}")

  if(NOT _dir AND DEFINED ENV{CYBERVERSE_PLATFORM_DIR})
    set(_dir "$ENV{CYBERVERSE_PLATFORM_DIR}")
  endif()

  if(NOT _dir)
    # Relative to this file: platform/cmake/../../platform
    set(_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../platform")
  endif()

  get_filename_component(_dir "${_dir}" ABSOLUTE)

  if(_dir AND EXISTS "${_dir}/CMakeLists.txt")
    message(STATUS "cyberverse_platform: resolved from local source ${_dir}")
    add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_deps/platform_build")

    if(TARGET cyberverse::platform::static_lib)
      return()
    endif()
  elseif(CYBERVERSE_PLATFORM_DIR)
    message(STATUS "cyberverse_platform: CYBERVERSE_PLATFORM_DIR does not contain a CMakeLists.txt, falling back")
  endif()

  # Third: fetch from the configured Git repository.
  # Use the real public host here. github-simon2077s.com is a local SSH alias
  # used by developer remotes and is not resolvable on other machines or CI.
  set(
    CYBERVERSE_PLATFORM_GIT_REPOSITORY
    "git@github.com:cyberverse-engine/platform.git"
    CACHE STRING "Git repository used to fetch cyberverse_platform"
  )
  set(
    CYBERVERSE_PLATFORM_GIT_TAG
    "main"
    CACHE STRING "Git tag, branch, or commit used to fetch cyberverse_platform"
  )
  option(CYBERVERSE_PLATFORM_GIT_SHALLOW "Use a shallow clone when fetching cyberverse_platform" ON)

  if(CYBERVERSE_PLATFORM_GIT_REPOSITORY)
    message(
      STATUS
      "cyberverse_platform: fetching ${CYBERVERSE_PLATFORM_GIT_REPOSITORY} (${CYBERVERSE_PLATFORM_GIT_TAG})"
    )
    FetchContent_Declare(
      cyberverse_platform
      GIT_REPOSITORY "${CYBERVERSE_PLATFORM_GIT_REPOSITORY}"
      GIT_TAG "${CYBERVERSE_PLATFORM_GIT_TAG}"
      GIT_SHALLOW "${CYBERVERSE_PLATFORM_GIT_SHALLOW}"
    )
    FetchContent_MakeAvailable(cyberverse_platform)

    if(TARGET cyberverse::platform::static_lib)
      message(STATUS "cyberverse_platform: resolved from fetched source")
      return()
    endif()
  endif()

  message(FATAL_ERROR
    "cyberverse_platform: cannot be resolved. "
    "Provide an installed cyberverse_platform package, set CYBERVERSE_PLATFORM_DIR to "
    "platform's source directory, or set CYBERVERSE_PLATFORM_GIT_REPOSITORY.")
endfunction()
