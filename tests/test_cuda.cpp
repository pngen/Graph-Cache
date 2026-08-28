#include "test_util.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
gc::GraphLookupRequest make_cuda_request(const std::string& name, std::uint64_t bytes) {
  gc::GraphLookupRequest r;
  r.workload.logical_name = name;
  r.workload.logical_version = 1;
  r.backend.kind = gc::BackendKind::Cuda;
  r.backend.backend_name = "cuda-graph";
  r.backend.backend_version = 1;
  r.runtime.runtime_version = 1;
  r.runtime.graph_abi = 1;
  r.runtime.kernel_abi = 1;
  r.runtime.driver_version = 13040;
  r.device.vendor = gc::AcceleratorVendor::Nvidia;
  r.device.architecture = "blackwell";
  r.device.compute_major = 12;
  r.device.compute_minor = 0;
  r.device.name = "NVIDIA GeForce RTX 5090";

  gc::GraphNodeDescriptor k1;
  k1.node_id = gc::GraphNodeId(0x1, 0x1);
  k1.category = gc::NodeCategory::Kernel;
  k1.name = "scale";
  k1.kernel.name = "scale";
  k1.kernel.id_hi = 0x5000; k1.kernel.id_lo = 1;
  k1.kernel.generation = gc::GraphGeneration(1);
  k1.kernel.abi = 1;
  k1.kernel.content_digest = "k-scale";
  k1.kernel.contributes_to_correctness = true;
  k1.memory_bytes = bytes;
  k1.shape.dims = {static_cast<std::int64_t>(bytes / 4)};
  k1.datatype = gc::Datatype::F32;
  k1.layout = gc::TensorLayout::Contiguous;
  k1.scalar.specialized = true;
  k1.scalar.float_value = 3.0f;
  k1.binding.binding_class = gc::BindingClass::ReplayMutableBinding;
  k1.binding.rebinding_eligible = true;
  k1.binding.memory_binding_schema = "cuda:1";
  k1.required_alignment = 128;
  k1.reactive_input_indices = {0};
  k1.reactive_output_indices = {0};

  gc::GraphNodeDescriptor k2;
  k2.node_id = gc::GraphNodeId(0x2, 0x2);
  k2.category = gc::NodeCategory::Kernel;
  k2.name = "addscalar";
  k2.kernel.name = "addscalar";
  k2.kernel.id_hi = 0x6000; k2.kernel.id_lo = 2;
  k2.kernel.generation = gc::GraphGeneration(1);
  k2.kernel.abi = 1;
  k2.kernel.content_digest = "k-addscalar";
  k2.kernel.contributes_to_correctness = true;
  k2.memory_bytes = bytes;
  k2.shape.dims = {static_cast<std::int64_t>(bytes / 4)};
  k2.datatype = gc::Datatype::F32;
  k2.layout = gc::TensorLayout::Contiguous;
  k2.scalar.specialized = true;
  k2.scalar.float_value = 2.0f;
  k2.binding.binding_class = gc::BindingClass::ReplayMutableBinding;
  k2.binding.rebinding_eligible = true;
  k2.binding.memory_binding_schema = "cuda:1";
  k2.required_alignment = 128;
  k2.reactive_input_indices = {0};
  k2.reactive_output_indices = {0};

  gc::GraphEdgeDescriptor e;
  e.edge_id = gc::GraphEdgeId(0x1, 0x1);
  e.from = k1.node_id;
  e.to = k2.node_id;
  e.kind = gc::DependencyKind::Execution;
  e.label = "k1->k2";

  r.descriptor.nodes = {k1, k2};
  r.descriptor.edges = {e};
  r.descriptor.backend.kind = gc::BackendKind::Cuda;
  r.descriptor.backend.backend_name = "cuda-graph";
  r.descriptor.backend.backend_version = 1;
  r.descriptor.runtime = r.runtime;
  r.descriptor.device = r.device;
  r.descriptor.sync.capture_mode = gc::CaptureMode::StreamCapture;
  r.descriptor.sync.stream_semantics = gc::StreamSemantics::StreamOrdered;
  r.descriptor.provenance.graph_name = name;
  r.descriptor.dependencies = {k1.kernel, k2.kernel};
  r.policy = gc::GraphCompatibilityPolicy{};
  r.allow_capture = true;
  r.graph_name = name;
  return r;
}

std::vector<float> cuda_reference(const std::vector<float>& in) {
  std::vector<float> out(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) out[i] = 3.0f * in[i] + 2.0f;
  return out;
}
} // namespace

GC_TEST(cuda_capture_replay_rtx5090) {
  REQUIRE(gc::cuda_available());
  auto cap = gc::cuda_require_capability(12, 0);
  REQUIRE(cap.ok());
  std::printf("gc_test cuda: device=%s compute=%u.%u\n",
              gc::cuda_device_name().c_str(), gc::cuda_compute_major(), gc::cuda_compute_minor());

  const std::uint64_t bytes = 1024 * 4;   // N = 1024 elements
  const std::size_t n = bytes / 4;
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cuda;
  gc::GraphCache cache(cfg);
  auto req = make_cuda_request("cuda-proof", bytes);
  auto r = cache.lookup(req);
  REQUIRE(r.hit());
  CHECK(r.lease != nullptr);

  std::vector<float> in(n), out(n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(i % 7) * 0.5f - 1.0f;
  auto expect = cuda_reference(in);
  gc::ReplayBuffers bufs;
  bufs.inputs = {in.data()};
  bufs.outputs = {out.data()};
  bufs.input_bytes = {bytes};
  bufs.output_bytes = {bytes};

  // Repeated replay.
  std::uint64_t replays = 0;
  for (int rep = 0; rep < 6; ++rep) {
    gc::GraphReplayRequest rr;
    rr.lease = r.lease;
    rr.buffers = bufs;
    rr.descriptor.replay_attempt = gc::ReplayAttemptId(rep + 1);
    rr.expected_generation = r.generation;
    auto replay = cache.replay(rr);
    REQUIRE(replay.ok());
    CHECK(replay->ok);
    ++replays;
    bool ok = true;
    for (std::size_t i = 0; i < n; ++i) { if (std::fabs(out[i] - expect[i]) > 1e-2f) { ok = false; break; } }
    if (!ok) {
      std::printf("cuda replay %d mismatch at first: out[0]=%f expect[0]=%f\n", rep, out[0], expect[0]);
      CHECK(false);
    }
  }
  CHECK_EQ(replays, 6u);

  // Legal rebinding: change the input buffer pointer, same graph.
  std::vector<float> in2(n), out2(n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) in2[i] = 0.25f * static_cast<float>(i);
  auto expect2 = cuda_reference(in2);
  gc::ReplayBuffers bufs2;
  bufs2.inputs = {in2.data()};
  bufs2.outputs = {out2.data()};
  bufs2.input_bytes = {bytes};
  bufs2.output_bytes = {bytes};
  gc::GraphReplayRequest rr2;
  rr2.lease = r.lease;
  rr2.buffers = bufs2;
  rr2.descriptor.replay_attempt = gc::ReplayAttemptId(100);
  rr2.expected_generation = r.generation;
  auto replay2 = cache.replay(rr2);
  REQUIRE(replay2.ok());
  bool ok2 = true;
  for (std::size_t i = 0; i < n; ++i) if (std::fabs(out2[i] - expect2[i]) > 1e-2f) { ok2 = false; break; }
  CHECK(ok2);

  // Report dimensions and replay counts.
  std::printf("cuda proof: elements=%zu kernels=2 memcpy=2 replays=%llu device=%s compute=%u.%u\n",
              n, static_cast<unsigned long long>(replays),
              gc::cuda_device_name().c_str(), gc::cuda_compute_major(), gc::cuda_compute_minor());

  // Invalidation + recapture.
  gc::InvalidationRequest inv;
  inv.scope = gc::InvalidationScope::ByWorkload;
  inv.workload.logical_name = "cuda-proof";
  [[maybe_unused]] auto _inv = cache.invalidate(inv);
  auto r2 = cache.lookup(req);
  // allow_capture default true -> recapture leads to a hit with a new generation
  CHECK(r2.hit());
  CHECK(r2.generation != r.generation);
}

GC_TEST_MAIN
