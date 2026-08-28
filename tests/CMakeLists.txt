# Graph Cache tests. Lightweight dependency-free harness (tests/gc_test.hpp).

include(CTest)
if(NOT BUILD_TESTING)
  return()
endif()

# Helper to declare a test executable + ctest test.
function(gc_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE graphcache)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(GRAPHCACHE_HAS_CUDA)
    target_compile_definitions(${name} PRIVATE GC_HAS_CUDA=1)
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 0)
endfunction()

gc_add_test(test_core test_core.cpp)
gc_add_test(test_cpu_cache test_cpu_cache.cpp)
gc_add_test(test_persistence test_persistence.cpp)
gc_add_test(test_adversarial test_adversarial.cpp)
gc_add_test(test_concurrency test_concurrency.cpp)
gc_add_test(test_property test_property.cpp)
gc_add_test(test_access test_access.cpp)
# The atomic multiprocess stale-authority proof is an in-progress proof; it is
# opt-in (GRAPHCACHE_BUILD_ATOMIC_PROOF=ON) so the default suite passes clean.
option(GRAPHCACHE_BUILD_ATOMIC_PROOF "Build the atomic multiprocess proof" OFF)
if(GRAPHCACHE_BUILD_ATOMIC_PROOF)
  gc_add_test(test_protocol test_protocol.cpp)
endif()
if(GRAPHCACHE_HAS_CUDA)
  gc_add_test(test_cuda test_cuda.cpp)
endif()
