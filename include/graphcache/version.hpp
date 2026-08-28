#pragma once
// Graph Cache - version and API macros.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#define GRAPHCACHE_VERSION_MAJOR 1
#define GRAPHCACHE_VERSION_MINOR 0
#define GRAPHCACHE_VERSION_PATCH 0
#define GRAPHCACHE_VERSION_STRING "1.0.0"

#if defined(_WIN32)
#  if defined(GRAPHCACHE_STATIC)
#    define GC_API
#  elif defined(GRAPHCACHE_BUILDING)
#    define GC_API __declspec(dllexport)
#  else
#    define GC_API __declspec(dllimport)
#  endif
#  define GC_NODISCARD _NODISCARD
#else
#  define GC_API __attribute__((visibility("default")))
#  define GC_NODISCARD [[nodiscard]]
#endif

// Common shared ABI note: the library is distributed as a static target
// (GraphCache::GraphCache). GC_API is kept for symmetry and to permit a DLL
// build if a downstream consumer prefers one.
