#pragma once
// Executable dependency authority. Graph Cache references executable
// dependencies (e.g. kernels) abstractly by stable identity, generation, ABI,
// provenance, and a content digest. A graph is stale when a dependency that
// contributes to correctness changes incompatibly.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/ids.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gc {

struct KernelIdentityRef {
  std::string name;                    // stable dependency identity
  std::uint64_t id_hi{0};              // stable ID (128-bit)
  std::uint64_t id_lo{0};
  GraphGeneration generation;          // dependency generation
  std::uint32_t abi{1};                // kernel ABI version
  std::string content_digest;          // SHA-256 hex of dependency content
  std::string provenance;              // where the dependency came from
  bool contributes_to_correctness{true}; // if false, changes need not invalidate
};

[[nodiscard]] inline bool operator==(const KernelIdentityRef& a, const KernelIdentityRef& b) {
  return a.name == b.name && a.id_hi == b.id_hi && a.id_lo == b.id_lo &&
         a.generation == b.generation && a.abi == b.abi &&
         a.content_digest == b.content_digest &&
         a.contributes_to_correctness == b.contributes_to_correctness;
}

// A dependency set as it contributed to an already published graph.
struct DependencySnapshot {
  std::vector<KernelIdentityRef> dependencies;

  [[nodiscard]] bool operator==(const DependencySnapshot&) const = default;
};

} // namespace gc
