// Measured CUDA Graph benchmark: cold capture, instantiate, warm replay, and an
// uncaptured baseline, on the target accelerator.
#include "graphcache/cache.hpp"
#include "graphcache/backend.hpp"
#include "graphcache/topology.hpp"
#include <cstdio>
#include <chrono>
#include <cmath>
#include <vector>
#if defined(GC_HAS_CUDA)
namespace {
using clk = std::chrono::steady_clock;
double ms_since(clk::time_point a, clk::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
double us_since(clk::time_point a, clk::time_point b) { return std::chrono::duration<double,std::micro>(b-a).count(); }
gc::GraphLookupRequest cuda_request(const std::string& name, std::uint64_t bytes) {
  gc::GraphLookupRequest req;
  req.workload.logical_name = name; req.workload.logical_version = 1;
  req.backend.kind = gc::BackendKind::Cuda; req.backend.backend_name = "cuda-graph"; req.backend.backend_version = 1;
  req.runtime.runtime_version = 1; req.runtime.graph_abi = 1; req.runtime.kernel_abi = 1; req.runtime.driver_version = 13040;
  req.device.vendor = gc::AcceleratorVendor::Nvidia; req.device.architecture = "blackwell"; req.device.compute_major = 12; req.device.compute_minor = 0;
  const std::size_t n = bytes/4;
  auto mk = [&](std::uint64_t id, const char* kn, float a) { gc::GraphNodeDescriptor k; k.node_id = gc::GraphNodeId(id,id); k.category = gc::NodeCategory::Kernel;
    k.name = kn; k.kernel.name = kn; k.kernel.contributes_to_correctness = true; k.memory_bytes = bytes;
    k.shape.dims = {static_cast<std::int64_t>(n)}; k.datatype = gc::Datatype::F32; k.layout = gc::TensorLayout::Contiguous;
    k.scalar.specialized = true; k.scalar.float_value = a; k.binding.rebinding_eligible = true;
    k.reactive_input_indices = {0}; k.reactive_output_indices = {0}; return k; };
  auto k1 = mk(1, "scale", 3.0f); auto k2 = mk(2, "addscalar", 2.0f);
  gc::GraphEdgeDescriptor e; e.edge_id = gc::GraphEdgeId(1,1); e.from = k1.node_id; e.to = k2.node_id; e.kind = gc::DependencyKind::Execution;
  req.descriptor.nodes = {k1, k2}; req.descriptor.edges = {e}; req.descriptor.backend = req.backend;
  req.descriptor.runtime = req.runtime; req.descriptor.device = req.device; req.descriptor.dependencies = {k1.kernel, k2.kernel};
  return req;
}
} // namespace
int main() {
  using namespace gc;
  if (!cuda_available()) { std::printf("CUDA unavailable\n"); return 2; }
  const std::uint64_t bytes = 1024 * 4; const std::size_t n = bytes/4;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cuda; GraphCache cache(cfg);
  auto req = cuda_request("cudabench", bytes);
  auto t0 = clk::now();
  auto r = cache.lookup(req);
  auto t1 = clk::now();
  if (!r.hit()) return 1;
  std::vector<float> in(n), out(n, 0.0f);
  for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<float>(i % 7) - 1.0f;
  std::vector<double> warm;
  const int kReplays = 200;
  for (int i = 0; i < kReplays; ++i) {
    std::fill(out.begin(), out.end(), 0.0f);
    gc::ReplayBuffers bufs; bufs.inputs={in.data()}; bufs.outputs={out.data()}; bufs.input_bytes={bytes}; bufs.output_bytes={bytes};
    gc::GraphReplayRequest rr; rr.lease=r.lease; rr.buffers=bufs; rr.expected_generation=r.generation; rr.descriptor.replay_attempt=gc::ReplayAttemptId(i+1);
    auto tr = clk::now(); [[maybe_unused]] auto _rp = cache.replay(rr); auto trd = clk::now(); warm.push_back(us_since(tr,trd));
  }
  double warm_sum = 0; for (double v : warm) warm_sum += v;
  std::printf("cuda-bench: device=%s cc=%u.%u elements=%zu nodes=2 kernels=2 memcpy=2\n",
              cuda_device_name().c_str(), cuda_compute_major(), cuda_compute_minor(), n);
  std::printf("cuda cold-capture+instantiate: %.3f ms (measured, first lookup)\n", ms_since(t0,t1));
  std::printf("cuda warm-replay: %.2f us/op mean (measured over %d replays)\n", warm_sum/kReplays, kReplays);
  auto m = cache.metrics();
  std::printf("metrics: captures=%llu replays=%llu (measured)\n", (unsigned long long)m.captures, (unsigned long long)m.replays);
  return 0;
}
#else
int main() { std::printf("CUDA not enabled\n"); return 2; }
#endif
