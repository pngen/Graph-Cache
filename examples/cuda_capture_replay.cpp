#include "../tests/test_util.hpp"
#include <cstdio>
#include <vector>
#include <cmath>
#if defined(GC_HAS_CUDA)
int main() {
  using namespace gc;
  if (!cuda_available()) { std::printf("CUDA unavailable\n"); return 2; }
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cuda;
  GraphCache cache(cfg);
  // Build a CUDA descriptor with two kernels (scale then addscalar).
  GraphLookupRequest req;
  req.workload.logical_name = "cuda_ex"; req.workload.logical_version = 1;
  req.backend.kind = BackendKind::Cuda; req.backend.backend_name = "cuda-graph"; req.backend.backend_version = 1;
  req.runtime.runtime_version = 1; req.runtime.graph_abi = 1; req.runtime.kernel_abi = 1; req.runtime.driver_version = 13040;
  req.device.vendor = AcceleratorVendor::Nvidia; req.device.architecture = "blackwell"; req.device.compute_major = 12; req.device.compute_minor = 0;
  const std::uint64_t bytes = 1024 * 4; const std::size_t n = bytes/4;
  auto mk = [&](std::uint64_t id, const char* kname, float a) {
    GraphNodeDescriptor k; k.node_id = GraphNodeId(id,id); k.category = NodeCategory::Kernel; k.name = kname;
    k.kernel.name = kname; k.kernel.contributes_to_correctness = true; k.memory_bytes = bytes;
    k.shape.dims = {static_cast<std::int64_t>(n)}; k.datatype = Datatype::F32; k.layout = TensorLayout::Contiguous;
    k.scalar.specialized = true; k.scalar.float_value = a; k.binding.rebinding_eligible = true;
    k.reactive_input_indices = {0}; k.reactive_output_indices = {0}; return k;
  };
  auto k1 = mk(1, "scale", 3.0f);
  auto k2 = mk(2, "addscalar", 2.0f);
  GraphEdgeDescriptor e; e.edge_id = GraphEdgeId(1,1); e.from = k1.node_id; e.to = k2.node_id; e.kind = DependencyKind::Execution;
  req.descriptor.nodes = {k1, k2}; req.descriptor.edges = {e}; req.descriptor.backend = req.backend;
  req.descriptor.runtime = req.runtime; req.descriptor.device = req.device; req.descriptor.dependencies = {k1.kernel, k2.kernel};
  auto r = cache.lookup(req);
  if (!r.hit()) return 1;
  std::vector<float> in(n), out(n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(i % 5) - 1.0f;
  int ok = 0;
  for (int rep = 0; rep < 3; ++rep) {
    std::fill(out.begin(), out.end(), 0.0f);
    ReplayBuffers bufs; bufs.inputs = {in.data()}; bufs.outputs = {out.data()}; bufs.input_bytes = {bytes}; bufs.output_bytes = {bytes};
    GraphReplayRequest rr; rr.lease = r.lease; rr.buffers = bufs; rr.expected_generation = r.generation;
    rr.descriptor.replay_attempt = ReplayAttemptId(rep+1);
    auto rep2 = cache.replay(rr);
    if (!rep2.ok()) { ok = 1; break; }
    for (std::size_t i = 0; i < n; ++i) {
      float expected = 3.0f * in[i] + 2.0f;
      if (std::fabs(out[i] - expected) > 1e-2f) { ok = 1; break; }
    }
    if (ok) break;
  }
  std::printf("cuda example: device=%s cc=%u.%u elements=%zu kernels=2 replays=3 ok=%d\n", cuda_device_name().c_str(), cuda_compute_major(), cuda_compute_minor(), n, ok);
  return ok;
}
#else
int main() { std::printf("CUDA not enabled\n"); return 2; }
#endif
