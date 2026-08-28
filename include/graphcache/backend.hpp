#pragma once
// Vendor-neutral backend interfaces: capture, validate, load/instantiate,
// replay, rebind, unload/destroy, and resource accounting. CUDA Graphs are one
// proven backend; they never define the abstract Graph Cache semantics.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/ids.hpp"
#include "graphcache/result.hpp"
#include "graphcache/sha256.hpp"
#include "graphcache/topology.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace gc {

struct CaptureDescriptor {
  CaptureAttemptId capture_attempt;
  CaptureMode mode{CaptureMode::None};
  std::string source;
  std::uint64_t timestamp_ms{0};
};

struct ReplayDescriptor {
  ReplayAttemptId replay_attempt;
  std::uint64_t timestamp_ms{0};
  std::uint32_t expected_replays{1};
  std::string reason;   // explainable reason for this replay
};

// Backend-native buffer handles supplied by the caller at replay time. For the
// CPU backend these are host pointers; for the CUDA backend these are device
// pointers (or host pointers when the CUDA backend executes on host-side
// staging buffers it manages).
struct ReplayBuffers {
  std::vector<const void*> inputs;
  std::vector<void*> outputs;
  std::vector<std::size_t> input_bytes;
  std::vector<std::size_t> output_bytes;
};

enum class RebindRole : std::uint32_t {
  InputBuffer = 1,
  OutputBuffer = 2,
  KernelParam = 3,
  MemcpySrc = 4,
  MemcpyDst = 5,
  MemsetTarget = 6,
  LaunchGeometry = 7
};

struct RebindUpdate {
  RebindRole role{RebindRole::InputBuffer};
  const void* address{nullptr};      // new address, or nullptr to keep existing
};

struct RebindSpec {
  std::vector<RebindUpdate> updates;
  std::optional<LaunchGeometry> launch_geometry;  // optional new geometry
};

struct GraphReplayResult {
  bool ok{false};
  std::uint64_t replay_attempt{0};
  std::uint64_t replayed_nodes{0};
  Sha256::digest_t output_digest{};    // host-side digest over primary outputs
  std::string output_digest_hex;
  std::uint64_t latency_us{0};
};

// A backend exposes the graph lifecycle primitives for a single vendor.
class GraphBackend {
 public:
  virtual ~GraphBackend() = default;

  [[nodiscard]] virtual BackendIdentity identity() const = 0;

  // Capture a graph descriptor into a backend-executable artifact.
  [[nodiscard]] virtual Result<std::shared_ptr<void>> capture(
      const GraphDescriptor& desc, CaptureAttemptId attempt) = 0;

  // Validation hook (execution smoke / reference compare run by the engine).
  [[nodiscard]] virtual Result<void> validate(
      const GraphDescriptor& desc, const std::shared_ptr<void>& exec) = 0;

  // Instantiate/load backend-resident state for a previously captured artifact.
  [[nodiscard]] virtual Result<void> load(
      const GraphDescriptor& desc, std::shared_ptr<void>& exec) = 0;

  // Replay the graph over supplied buffers.
  [[nodiscard]] virtual Result<GraphReplayResult> replay(
      const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
      const ReplayBuffers& buffers, const ReplayDescriptor& rd) = 0;

  // Legal parameter/address rebinding where the backend allows it.
  [[nodiscard]] virtual Result<void> rebind(
      const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
      const RebindSpec& spec) = 0;

  // Query whether a graph may be legally rebound.
  [[nodiscard]] virtual Result<bool> can_rebind(const GraphDescriptor& desc) const = 0;

  // Unload backend-resident state (analogous to cudaGraphDestroy / free).
  [[nodiscard]] virtual Result<void> unload(
      const GraphDescriptor& desc, std::shared_ptr<void>& exec) = 0;

  // Backend-resident resource accounting (bytes resident on the backend).
  [[nodiscard]] virtual std::uint64_t backend_resident_bytes(
      const GraphDescriptor& desc, const std::shared_ptr<void>& exec) const = 0;
};

// Factory for the built-in backends. Throws/returns failure for unknown kinds.
[[nodiscard]] std::unique_ptr<GraphBackend> create_backend(BackendKind kind);

// Register a backend implementation by identifier (used by the CUDA shim).
void register_backend_factory(std::string name,
                              std::function<std::unique_ptr<GraphBackend>()> factory);

// CUDA diagnostic helpers (defined in cuda_backend when CUDA is available).
[[nodiscard]] bool cuda_available();
[[nodiscard]] std::string cuda_device_name();
[[nodiscard]] std::uint32_t cuda_compute_major();
[[nodiscard]] std::uint32_t cuda_compute_minor();
[[nodiscard]] Result<void> cuda_require_capability(std::uint32_t major, std::uint32_t minor);

} // namespace gc
