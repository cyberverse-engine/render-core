include_guard(GLOBAL)
include(FetchContent)

# Resolve cyberverse::geometry::static_lib for the transform library.
# Idempotent: calling it more than once is safe.
#
# Resolution order:
#   1. An already-defined target (superproject or an earlier call).
#   2. An installed cyberverse_geometry CMake package.
#   3. A geometry source tree: CYBERVERSE_GEOMETRY_DIR, its environment
#      equivalent, or the sibling ../../geometry directory.
#   4. FetchContent from CYBERVERSE_GEOMETRY_GIT_REPOSITORY.
function(resolve_engine_dep_geometry)
  if(TARGET cyberverse::geometry::static_lib)
    return()
  endif()

  # Geometry is a dependency here, so never build its tests or benchmarks.
  set(CYBERVERSE_GEOMETRY_BUILD_TESTS OFF CACHE BOOL "Build cyberverse_geometry tests" FORCE)
  set(CYBERVERSE_GEOMETRY_BUILD_BENCHMARKS OFF CACHE BOOL "Build cyberverse_geometry benchmarks" FORCE)

  # First: an installed package.
  find_package(cyberverse_geometry CONFIG QUIET)

  if(TARGET cyberverse::geometry::static_lib)
    message(STATUS "cyberverse_geometry: resolved from installed package")
    return()
  endif()

  # Second: a source tree injected by the workspace or the environment.
  set(_dir "${CYBERVERSE_GEOMETRY_DIR}")

  if(NOT _dir AND DEFINED ENV{CYBERVERSE_GEOMETRY_DIR})
    set(_dir "$ENV{CYBERVERSE_GEOMETRY_DIR}")
  endif()

  if(NOT _dir)
    # Relative to this file: transform/cmake/../../geometry
    set(_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../geometry")
  endif()

  get_filename_component(_dir "${_dir}" ABSOLUTE)

  if(_dir AND EXISTS "${_dir}/CMakeLists.txt")
    message(STATUS "cyberverse_geometry: resolved from local source ${_dir}")
    add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_deps/geometry_build")

    if(TARGET cyberverse::geometry::static_lib)
      return()
    endif()
  elseif(CYBERVERSE_GEOMETRY_DIR)
    message(STATUS "cyberverse_geometry: CYBERVERSE_GEOMETRY_DIR does not contain a CMakeLists.txt, falling back")
  endif()

  # Third: fetch from the configured Git repository.
  # Use the real public host here. github-simon2077s.com is a local SSH alias
  # used by developer remotes and is not resolvable on other machines or CI.
  set(
    CYBERVERSE_GEOMETRY_GIT_REPOSITORY
    "git@github.com:cyberverse-engine/geometry.git"
    CACHE STRING "Git repository used to fetch cyberverse_geometry"
  )
  set(
    CYBERVERSE_GEOMETRY_GIT_TAG
    "main"
    CACHE STRING "Git tag, branch, or commit used to fetch cyberverse_geometry"
  )
  option(CYBERVERSE_GEOMETRY_GIT_SHALLOW "Use a shallow clone when fetching cyberverse_geometry" ON)

  if(CYBERVERSE_GEOMETRY_GIT_REPOSITORY)
    message(
      STATUS
      "cyberverse_geometry: fetching ${CYBERVERSE_GEOMETRY_GIT_REPOSITORY} (${CYBERVERSE_GEOMETRY_GIT_TAG})"
    )
    FetchContent_Declare(
      cyberverse_geometry
      GIT_REPOSITORY "${CYBERVERSE_GEOMETRY_GIT_REPOSITORY}"
      GIT_TAG "${CYBERVERSE_GEOMETRY_GIT_TAG}"
      GIT_SHALLOW "${CYBERVERSE_GEOMETRY_GIT_SHALLOW}"
    )
    FetchContent_MakeAvailable(cyberverse_geometry)

    if(TARGET cyberverse::geometry::static_lib)
      message(STATUS "cyberverse_geometry: resolved from fetched source")
      return()
    endif()
  endif()

  message(FATAL_ERROR
    "cyberverse_geometry: cannot be resolved. "
    "Provide an installed cyberverse_geometry package, set CYBERVERSE_GEOMETRY_DIR to "
    "geometry's source directory, or set CYBERVERSE_GEOMETRY_GIT_REPOSITORY.")
endfunction()
