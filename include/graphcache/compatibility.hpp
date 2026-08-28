#pragma once
// GraphCompatibilityKey and the compatibility engine.
//
// A graph cache hit is a correctness decision. Compatibility is never inferred
// from a name, hash, operator list, or workload label. It is a deterministic,
// typed field-by-field comparison of the exact execution request against the
// candidate graph's captured compatibility authority.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/dependencies.hpp"
#include "graphcache/ids.hpp"
#include "graphcache/result.hpp"
#include "graphcache/sha256.hpp"
#include "graphcache/topology.hpp"
#include "graphcache/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gc {

// The complete, fully-typed set of facts the compatibility decision depends on.
// Two FactSets with identical bytes encode to an identical GraphCompatibilityKey.
struct CompatibilityFacts {
  WorkloadIdentity workload;
  BackendIdentity backend;
  RuntimeDescriptor runtime;
  DeviceDescriptor device;
  CaptureMode capture_mode{CaptureMode::None};
  StreamSemantics stream_semantics{StreamSemantics::Default};
  std::vector<std::uint8_t> topology_canonical;   // canonical topology bytes
  std::vector<ShapeDescriptor> input_shapes;
  std::vector<ShapeDescriptor> output_shapes;
  std::vector<Datatype> datatypes;
  std::vector<TensorLayout> layouts;
  ScalarSpecialization scalar;
  QuantizationConfig quantization;
  BindingDescriptor binding;
  std::uint64_t required_alignment{128};
  std::string memory_binding_schema;
  std::vector<KernelIdentityRef> dependencies;
  std::string model_operator_revision;
  std::string policy_generation;
};

// A typed deterministic compatibility identity over a Facts set.
class GraphCompatibilityKey {
 public:
  GraphCompatibilityKey() = default;

  // Deterministically encode facts and compute the SHA-256 digest.
  [[nodiscard]] static Result<GraphCompatibilityKey> build(const CompatibilityFacts& facts);

  // Rebuild a key from a canonical payload, validating that the payload is a
  // well-formed Facts encoding and that its digest agrees. Malformed/truncated
  // input is rejected.
  [[nodiscard]] static Result<GraphCompatibilityKey> from_canonical(std::span<const std::uint8_t> canonical);

  [[nodiscard]] const std::vector<std::uint8_t>& canonical() const noexcept { return canonical_; }
  [[nodiscard]] const Sha256::digest_t& digest() const noexcept { return digest_; }
  [[nodiscard]] std::string digest_hex() const { return Sha256::hex(digest_); }
  [[nodiscard]] const CompatibilityFacts& facts() const noexcept { return facts_; }
  [[nodiscard]] bool valid() const noexcept { return !canonical_.empty(); }

  [[nodiscard]] bool operator==(const GraphCompatibilityKey& o) const {
    return digest_ == o.digest_ && canonical_ == o.canonical_;
  }
  [[nodiscard]] auto operator<=>(const GraphCompatibilityKey& o) const {
    return canonical_ <=> o.canonical_;
  }

 private:
  CompatibilityFacts facts_;
  std::vector<std::uint8_t> canonical_;
  Sha256::digest_t digest_{};
};

// Decision classes.
enum class GraphCompatibilityClass : int {
  ExactCompatible = 0,
  CompatibleWithRebinding = 1,
  CompatibleWithDynamicShapeConstraint = 2,
  CompatibleWithRuntimeValidation = 3,
  IncompatibleBackend = 10,
  IncompatibleArchitecture = 11,
  IncompatibleRuntime = 12,
  IncompatibleDriverGeneration = 13,
  IncompatibleGraphABI = 14,
  IncompatibleKernelABI = 15,
  IncompatibleTopology = 16,
  IncompatibleDependency = 17,
  IncompatibleDatatype = 18,
  IncompatibleLayout = 19,
  IncompatibleShape = 20,
  IncompatibleAlignment = 21,
  IncompatibleCaptureMode = 22,
  IncompatibleStreamSemantics = 23,
  IncompatibleMemoryBinding = 24,
  IncompatibleSpecialization = 25,
  IncompatibleQuantization = 26,
  IncompatibleModelRevision = 27,
  IncompatibleWorkload = 28,
  InvalidGraph = 30,
  StaleGraph = 31,
  CorruptGraph = 32,
  PolicyRejected = 33,
  NotACandidate = 40
};

// Granular compatibility reason codes.
enum class CompatibilityReasonCode : int {
  None = 0,
  ExactMatch = 1,
  RebindingRequiredLegal = 2,
  DynamicShapeAccepted = 3,
  BackendMismatch = 10,
  ArchitectureMismatch = 11,
  RuntimeMismatch = 12,
  DriverGenerationMismatch = 13,
  GraphABIMismatch = 14,
  KernelABIMismatch = 15,
  TopologyMismatch = 16,
  DependencyMismatch = 17,
  DatatypeMismatch = 18,
  LayoutMismatch = 19,
  ShapeMismatch = 20,
  AlignmentMismatch = 21,
  CaptureModeMismatch = 22,
  StreamSemanticsMismatch = 23,
  MemoryBindingMismatch = 24,
  SpecializationMismatch = 25,
  QuantizationMismatch = 26,
  ModelRevisionMismatch = 27,
  WorkloadIdentityMismatch = 28,
  PolicyRejectedReason = 29,
  GraphInvalid = 30,
  GraphStale = 31,
  GraphCorrupt = 32,
  RebindingNotEligible = 33
};

struct CompatibilityReason {
  CompatibilityReasonCode code{CompatibilityReasonCode::None};
  std::string field;    // the domain that failed / was accepted
  std::string detail;   // human-readable, structured wording

  [[nodiscard]] bool accepted() const noexcept {
    return code == CompatibilityReasonCode::ExactMatch ||
           code == CompatibilityReasonCode::RebindingRequiredLegal ||
           code == CompatibilityReasonCode::DynamicShapeAccepted;
  }
};

struct GraphCompatibilityDecision {
  GraphCompatibilityClass klass{GraphCompatibilityClass::NotACandidate};
  std::vector<CompatibilityReason> reasons;

  [[nodiscard]] bool compatible() const noexcept {
    return klass == GraphCompatibilityClass::ExactCompatible ||
           klass == GraphCompatibilityClass::CompatibleWithRebinding ||
           klass == GraphCompatibilityClass::CompatibleWithDynamicShapeConstraint ||
           klass == GraphCompatibilityClass::CompatibleWithRuntimeValidation;
  }
  [[nodiscard]] bool needs_rebinding() const noexcept {
    return klass == GraphCompatibilityClass::CompatibleWithRebinding;
  }
};

// Compatibility policy: the tuple of toggles that constrain a decision.
struct GraphCompatibilityPolicy {
  bool allow_rebinding{true};
  bool allow_dynamic_shape{true};
  bool allow_runtime_validation{true};
  bool require_exact_backend{true};
  bool require_exact_architecture{true};
  bool require_exact_runtime{true};
  bool require_exact_abis{true};
  bool require_exact_topology{true};
  bool require_exact_dependencies{true};
  bool require_exact_scalar_specialization{true};
  bool require_exact_quantization{true};
  bool allow_model_revision_lax{false};
  std::string namespace_filter;  // empty == no filter
};

// Decide whether a request (reward facts) is replayable against a candidate
// graph's captured facts. Never silently coerces incompatible graphs into
// eligibility.
[[nodiscard]] GraphCompatibilityDecision decide_compatibility(
    const CompatibilityFacts& request, const CompatibilityFacts& candidate,
    const GraphCompatibilityPolicy& policy);

[[nodiscard]] const char* to_string(GraphCompatibilityClass) noexcept;
[[nodiscard]] const char* to_string(CompatibilityReasonCode) noexcept;

// Build a Facts set from a graph descriptor (the candidate's captured facts).
[[nodiscard]] CompatibilityFacts facts_from_graph(const GraphDescriptor& graph);

// Canonical round-trip of the Facts set (used by persistence / protocol).
[[nodiscard]] std::vector<std::uint8_t> facts_to_canonical(const CompatibilityFacts& facts);
[[nodiscard]] Result<CompatibilityFacts> facts_from_canonical(std::span<const std::uint8_t> canonical);

} // namespace gc
