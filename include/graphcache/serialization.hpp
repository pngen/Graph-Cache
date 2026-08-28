#pragma once
// Deterministic binary serialization for GraphDescriptor and cache-entry
// metadata. Used by persistence and the distributed protocol.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/backend.hpp"
#include "graphcache/result.hpp"
#include "graphcache/topology.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace gc {

// ---- GraphDescriptor codec ----
[[nodiscard]] Result<void> serialize_descriptor(const GraphDescriptor& g, std::vector<std::uint8_t>& out);
[[nodiscard]] Result<GraphDescriptor> deserialize_descriptor(std::span<const std::uint8_t> bytes);

// ---- Cache-entry metadata codec ----
struct PersistedMetadata {
  GraphArtifactId artifact_id;
  GraphGeneration generation;
  CacheGeneration cache_generation;
  WorkloadIdentity workload;
  std::uint64_t artifact_size{0};
  std::uint64_t capture_cost_us{0};
  std::uint64_t instantiate_cost_us{0};
  std::uint64_t dependency_generation{0};
  bool invalidated{false};
};

[[nodiscard]] Result<void> serialize_metadata(const PersistedMetadata& m, std::vector<std::uint8_t>& out);
[[nodiscard]] Result<PersistedMetadata> deserialize_metadata(std::span<const std::uint8_t> bytes);

} // namespace gc
