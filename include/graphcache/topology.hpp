#pragma once
// Graph topology: typed node/edge descriptors, invariant validation,
// deterministic canonical encoding, and topology hashing.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/dependencies.hpp"
#include "graphcache/ids.hpp"
#include "graphcache/result.hpp"
#include "graphcache/sha256.hpp"
#include "graphcache/types.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace gc {

enum class DependencyKind : std::uint32_t {
  Execution = 1,
  Memory = 2,
  Stream = 3,
  Order = 4,
  Data = 5
};

struct GraphNodeDescriptor {
  GraphNodeId node_id;
  NodeCategory category{NodeCategory::Kernel};
  std::string name;
  // Kernel node.
  KernelIdentityRef kernel;
  LaunchGeometry launch;
  // Memory operations.
  std::uint64_t memory_bytes{0};
  std::uint8_t memset_value{0};
  // Tensor specialization (primary tensor).
  ShapeDescriptor shape;
  Datatype datatype{Datatype::None};
  TensorLayout layout{TensorLayout::Contiguous};
  ScalarSpecialization scalar;
  QuantizationConfig quantization;
  // Binding / alignment.
  BindingDescriptor binding;
  std::uint64_t required_alignment{128};
  // Child graph (nested) reference.
  std::uint64_t child_graph_hi{0};
  std::uint64_t child_graph_lo{0};
  // Backend opaque payload (descriptor encoded, not a live handle).
  std::vector<std::uint8_t> backend_opaque;
  // Buffer access role hint for rebinding (indices are caller-supplied ids).
  std::vector<std::uint32_t> reactive_input_indices;
  std::vector<std::uint32_t> reactive_output_indices;

  [[nodiscard]] bool operator==(const GraphNodeDescriptor&) const = default;
};

struct GraphEdgeDescriptor {
  GraphEdgeId edge_id;
  GraphNodeId from;
  GraphNodeId to;
  DependencyKind kind{DependencyKind::Execution};
  std::string label;

  [[nodiscard]] bool operator==(const GraphEdgeDescriptor&) const = default;
};

struct GraphDescriptor {
  GraphArtifactId artifact_id;
  GraphGeneration generation;
  BackendIdentity backend;
  RuntimeDescriptor runtime;
  DeviceDescriptor device;
  SynchronizationDescriptor sync;
  std::vector<GraphNodeDescriptor> nodes;
  std::vector<GraphEdgeDescriptor> edges;
  std::vector<KernelIdentityRef> dependencies;
  ProvenanceDescriptor provenance;

  // Derived digest state; computed by finalize().
  std::vector<std::uint8_t> topology_canonical;
  Sha256::digest_t topology_digest{};
  std::vector<std::uint8_t> semantic_canonical;
  Sha256::digest_t semantic_digest{};

  // Compute derived digests and canonical forms. Requires a valid, acyclic,
  // normalized topology.
  [[nodiscard]] Result<void> finalize();
  // Validate invariants without mutating.
  [[nodiscard]] Result<void> validate_topology() const;
};

// ---- topology invariants ----
[[nodiscard]] Result<void> validate_topology(std::span<const GraphNodeDescriptor> nodes,
                                             std::span<const GraphEdgeDescriptor> edges);

// Sort nodes and edges into canonical order (by id) and return a deep copy.
[[nodiscard]] GraphDescriptor canonicalize_topology(const GraphDescriptor& g);

// Deterministic canonical topology encoding (nodes then edges, each sorted).
[[nodiscard]] std::vector<std::uint8_t> canonical_topology(
    std::span<const GraphNodeDescriptor> nodes, std::span<const GraphEdgeDescriptor> edges);

// Canonical semantic encoding: topology plus backend/runtime/device identity,
// capture mode, and dependency registry.
[[nodiscard]] std::vector<std::uint8_t> canonical_semantic(const GraphDescriptor& g);

// Deterministic topology hash (over canonical_topology).
[[nodiscard]] Sha256::digest_t topology_digest(std::span<const GraphNodeDescriptor> nodes,
                                               std::span<const GraphEdgeDescriptor> edges);

} // namespace gc
