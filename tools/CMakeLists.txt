# Distributed control-plane binaries.

add_executable(gc-coordinator coordinator_main.cpp)
target_link_libraries(gc-coordinator PRIVATE graphcache)
target_include_directories(gc-coordinator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(GRAPHCACHE_HAS_CUDA)
  target_compile_definitions(gc-coordinator PRIVATE GC_HAS_CUDA=1)
endif()

add_executable(gc-worker worker_main.cpp)
target_link_libraries(gc-worker PRIVATE graphcache)
target_include_directories(gc-worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(gc-worker PRIVATE GC_HAS_CUDA=1)
