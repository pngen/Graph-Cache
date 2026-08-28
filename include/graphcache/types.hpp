#pragma once
// Graph Cache typed descriptor and identity namespace.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/canonical.hpp"
#include "graphcache/ids.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gc {

// ---- Backend / vendor / device ----
enum class BackendKind : std::uint32_t {
  Cpu = 1,
  Cuda = 2
};

enum class AcceleratorVendor : std::uint32_t {
  Cpu = 1,
  Nvidia = 2,
  Amd = 3,
  Intel = 4,
  Generic = 99
};

struct BackendIdentity {
  BackendKind kind{BackendKind::Cpu};
  std::string backend_name;   // stable lowercase id, e.g. "cpu", "cuda-graph"
  std::uint32_t backend_version{1};
  std::string implementation_name;

  [[nodiscard]] bool operator==(const BackendIdentity&) const = default;
};

struct RuntimeDescriptor {
  std::uint32_t runtime_version{1};       // graphcache runtime version
  std::uint32_t graph_abi{1};             // executable graph ABI version
  std::uint32_t kernel_abi{1};            // kernel ABI version
  std::uint32_t driver_version{0};        // accelerator driver generation
  std::string driver_version_string;
  friend bool operator==(const RuntimeDescriptor&, const RuntimeDescriptor&) = default;
};

struct DeviceDescriptor {
  AcceleratorVendor vendor{AcceleratorVendor::Cpu};
  std::string architecture;               // e.g. "blackwell", "cpu"
  std::uint32_t compute_major{0};
  std::uint32_t compute_minor{0};
  std::uint32_t device_index{0};
  std::string name;

  friend bool operator==(const DeviceDescriptor&, const DeviceDescriptor&) = default;

  [[nodiscard]] std::string compute_capability() const {
    if (compute_major == 0) return {};
    return std::to_string(compute_major) + "." + std::to_string(compute_minor);
  }
};

// ---- Datatype / layout / shapes ----
enum class Datatype : std::uint32_t {
  None = 0, F32 = 1, F16 = 2, BF16 = 3, F64 = 4,
  I8 = 5, I16 = 6, I32 = 7, I64 = 8,
  U8 = 9, U16 = 10, U32 = 11, U64 = 12, Bool = 13
};

enum class TensorLayout : std::uint32_t {
  Contiguous = 1, RowMajor = 2, ColumnMajor = 3,
  NCHW = 4, NHWC = 5, NCDHW = 6, NDHWC = 7,
  CustomBlocked = 8
};

struct ShapeDescriptor {
  std::vector<std::int64_t> dims;         // empty == scalar / zero-rank
  bool dynamic{false};                     // dynamic shape constraint
  std::uint32_t max_rank{0};               // constraint for dynamic shapes
  friend bool operator==(const ShapeDescriptor&, const ShapeDescriptor&) = default;
};

struct ScalarSpecialization {
  bool specialized{false};
  std::int64_t int_value{0};
  double float_value{0.0};
  friend bool operator==(const ScalarSpecialization&, const ScalarSpecialization&) = default;
};

struct QuantizationConfig {
  enum class Mode : std::uint32_t { None = 0, Int8 = 1, Int4 = 2, FP8 = 3, Custom = 4 };
  Mode mode{Mode::None};
  std::uint32_t group_size{0};     // 0 == per-tensor / not applicable
  bool symmetric{true};
  std::string custom_scheme;
  friend bool operator==(const QuantizationConfig&, const QuantizationConfig&) = default;
};

// ---- Launch configuration ----
struct LaunchGeometry {
  std::uint32_t grid_x{1}, grid_y{1}, grid_z{1};
  std::uint32_t block_x{1}, block_y{1}, block_z{1};
  std::uint32_t shared_memory_bytes{0};
  friend bool operator==(const LaunchGeometry&, const LaunchGeometry&) = default;
};

// ---- Bindings ----
enum class BindingClass : std::uint32_t {
  ImmutableBinding = 1,
  ReplayMutableBinding = 2,     // replay-mutable where backend allows patching
  RecaptureRequiredBinding = 3, // any change invalidates the graph
  BackendValidatedBinding = 4   // backend decides legality at instantiate time
};

struct BindingDescriptor {
  BindingClass binding_class{BindingClass::ImmutableBinding};
  std::uint32_t alignment{128};      // required alignment in bytes
  std::string memory_binding_schema; // canonical schema id for the binding layout
  bool rebinding_eligible{false};    // may the binding be legally changed at replay?
  friend bool operator==(const BindingDescriptor&, const BindingDescriptor&) = default;
};

// ---- Synchronization ----
enum class CaptureMode : std::uint32_t {
  None = 0,
  StreamCapture = 1,
  LegacyStreamCapture = 2,
  ThreadLocalCapture = 3,
  RelaxedStreamCapture = 4,
  DestroyStreamCapture = 5,
  BackendManaged = 6
};

enum class StreamSemantics : std::uint32_t {
  Default = 1,
  StreamOrdered = 2,
  Contextified = 3,
  BackendSpecific = 4
};

struct SynchronizationDescriptor {
  CaptureMode capture_mode{CaptureMode::None};
  StreamSemantics stream_semantics{StreamSemantics::Default};
  bool requires_external_sync{false};  // graph assumes external sync around replay
  std::uint32_t event_dependency_count{0};
  friend bool operator==(const SynchronizationDescriptor&, const SynchronizationDescriptor&) = default;
};

// ---- Node topology identity ----
enum class NodeCategory : std::uint32_t {
  Kernel = 1,
  MemoryCopy = 2,
  MemorySet = 3,
  HostOperation = 4,
  Synchronization = 5,
  ChildGraph = 6,
  EventPrimitive = 7,
  BackendOpaque = 8
};

// ---- Provenance ----
struct ProvenanceDescriptor {
  std::uint64_t capture_timestamp_ms{0};
  std::string capture_source;       // e.g. "synthetic", "operator-fusion"
  std::uint64_t capture_duration_us{0};
  std::string capture_toolchain;
  std::string graph_name;           // stable logical name for explainability
};

// ---- Logical workload identity ----
enum class WorkloadRevisionMode : std::uint32_t { Unspecified = 0, Explicit = 1, ContentDerived = 2 };

struct WorkloadIdentity {
  std::string logical_name;         // workload logical identity
  std::uint32_t logical_version{1};
  std::string namespace_name{""};   // empty == default namespace
  WorkloadRevisionMode revision_mode{WorkloadRevisionMode::Unspecified};
  std::string model_operator_revision;  // model/operator revision where relevant
  std::string policy_generation;        // graph policy generation tag
  friend bool operator==(const WorkloadIdentity&, const WorkloadIdentity&) = default;
};

// ---- Helper: canonical digest of a typed field set ----
[[nodiscard]] inline std::vector<std::uint8_t> canonical_slice(std::uint16_t tag, std::string_view str) {
  CanonicalWriter w;
  w.put_str(tag, str);
  return w.take();
}

// ---- to_string helpers for diagnostics / explain ----
[[nodiscard]] const char* to_string(BackendKind) noexcept;
[[nodiscard]] const char* to_string(AcceleratorVendor) noexcept;
[[nodiscard]] const char* to_string(Datatype) noexcept;
[[nodiscard]] const char* to_string(TensorLayout) noexcept;
[[nodiscard]] const char* to_string(BindingClass) noexcept;
[[nodiscard]] const char* to_string(CaptureMode) noexcept;
[[nodiscard]] const char* to_string(StreamSemantics) noexcept;
[[nodiscard]] const char* to_string(NodeCategory) noexcept;

} // namespace gc
