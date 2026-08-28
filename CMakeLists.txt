
cmake_minimum_required(VERSION 3.22)
project(GraphCache VERSION 1.0.0 DESCRIPTION "Graph Cache runtime for reusable executable AI execution graphs"
        LANGUAGES CXX)

# ---- Global configuration ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(GRAPHCACHE_BUILD_TESTS "Build Graph Cache tests" ON)
option(GRAPHCACHE_BUILD_EXAMPLES "Build Graph Cache examples" ON)
option(GRAPHCACHE_BUILD_BENCHMARKS "Build Graph Cache benchmarks" ON)
option(GRAPHCACHE_BUILD_CLI "Build Graph Cache CLI" ON)
option(GRAPHCACHE_ENABLE_CUDA "Enable CUDA Graph backend if CUDA is available" ON)

if(MSVC)
  # MSVC strictness applies to C++ translation units only; CUDA .cu files are
  # compiled by nvcc and must not receive raw MSVC /W4 flags. Each flag is added
  # individually inside a CXX-only generator expression (CMake cannot express
  # multiple flags in one $<$<...>:...  expression).
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
  set(GRAPHCACHE_MSVC_FLAGS /W4 /WX /permissive- /Zc:__cplusplus /utf-8)
endif()

# ---- Detect CUDA (optional) ----
set(GRAPHCACHE_HAS_CUDA OFF)
if(GRAPHCACHE_ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
      set(GRAPHCACHE_HAS_CUDA ON)
      message(STATUS "GraphCache: CUDA enabled (version " ${CUDAToolkit_VERSION} ")")
    else()
      message(STATUS "GraphCache: CUDAToolkit not found; CUDA backend disabled")
    endif()
  else()
    message(STATUS "GraphCache: no CUDA compiler; CUDA backend disabled")
  endif()
endif()

# ---- Core library ----
set(GC_CORE_SRCS
  src/result.cpp
  src/types.cpp
  src/topology.cpp
  src/compatibility.cpp
  src/lifecycle.cpp
  src/backend.cpp
  src/cpu_backend.cpp
  src/cache.cpp
  src/persistence.cpp
  src/serialization.cpp
  src/protocol.cpp
)

if(GRAPHCACHE_HAS_CUDA)
  list(APPEND GC_CORE_SRCS
    src/cuda/cuda_backend.cu
  )
endif()

add_library(graphcache STATIC ${GC_CORE_SRCS})
set_target_properties(graphcache PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME GraphCache
)
target_include_directories(graphcache PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_definitions(graphcache PUBLIC GC_STATIC)
if(WIN32)
  target_link_libraries(graphcache PUBLIC ws2_32)
endif()
if(GRAPHCACHE_HAS_CUDA)
  target_compile_definitions(graphcache PUBLIC GC_HAS_CUDA=1)
  target_link_libraries(graphcache PUBLIC CUDA::cudart CUDA::cuda_driver)
endif()

if(GRAPHCACHE_HAS_CUDA)
  set_target_properties(graphcache PROPERTIES CUDA_ARCHITECTURES 120)
endif()

# ---- CLI / examples / tests / benchmarks ----
if(GRAPHCACHE_BUILD_CLI)
  add_subdirectory(cli)
endif()
if(GRAPHCACHE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
add_subdirectory(tools)
if(GRAPHCACHE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(GRAPHCACHE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---- Install / export ----
include(GNUInstallDirs)

install(TARGETS graphcache
  EXPORT GraphCacheTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/graphcache DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT GraphCacheTargets
  FILE GraphCacheTargets.cmake
  NAMESPACE GraphCache::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphCache
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/GraphCacheConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/GraphCacheConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphCache
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/GraphCacheConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/GraphCacheConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/GraphCacheConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphCache
)

# ---- Convenience alias matching the documented consumer name ----
add_library(GraphCache::GraphCache ALIAS graphcache)
