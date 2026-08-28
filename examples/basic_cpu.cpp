// Example: basic CPU graph capture and replay.
#include "graphcache/cache.hpp"
#include "graphcache/topology.hpp"
#include <cstdio>
#include <vector>

int main() {
  using namespace gc;
  GraphCacheConfig cfg;
  cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);

  GraphLookupRequest req;
  req.workload.logical_name = "example"; req.workload.logical_version = 1;
  req.backend.kind = BackendKind::Cpu; req.backend.backend_name = "cpu";
  req.runtime.runtime_version = 1; req.runtime.graph_abi = 1; req.runtime.kernel_abi = 1;
  req.device.vendor = AcceleratorVendor::Cpu; req.device.architecture = "cpu";

  GraphDescriptor d;
  d.backend = req.backend; d.runtime = req.runtime; d.device = req.device;
  GraphNodeDescriptor c; c.node_id = GraphNodeId(1,1); c.category = NodeCategory::MemoryCopy; c.name = "copy"; c.memory_bytes = 64;
  c.reactive_input_indices = {0}; c.reactive_output_indices = {0}; c.binding.rebinding_eligible = true;
  GraphNodeDescriptor k; k.node_id = GraphNodeId(2,2); k.category = NodeCategory::Kernel; k.name = "scale";
  k.kernel.name = "scale"; k.kernel.contributes_to_correctness = true; k.memory_bytes = 64;
  k.shape.dims = {16}; k.datatype = Datatype::F32; k.scalar.specialized = true; k.scalar.float_value = 2.0f;
  k.reactive_input_indices = {0}; k.reactive_output_indices = {0}; k.binding.rebinding_eligible = true;
  GraphEdgeDescriptor e; e.edge_id = GraphEdgeId(1,1); e.from = c.node_id; e.to = k.node_id; e.kind = DependencyKind::Execution;
  d.nodes = {c, k}; d.edges = {e};
  req.descriptor = d;

  auto r = cache.lookup(req);
  if (!r.hit()) { std::printf("capture failed\n"); return 1; }
  std::printf("captured artifact=%s generation=%llu\n", r.artifact_id.to_string().c_str(), (unsigned long long)r.generation.value);

  std::vector<float> in(16, 1.0f), out(16, 0.0f);
  ReplayBuffers bufs; bufs.inputs = {in.data()}; bufs.outputs = {out.data()}; bufs.input_bytes = {64}; bufs.output_bytes = {64};
  GraphReplayRequest rr; rr.lease = r.lease; rr.buffers = bufs; rr.expected_generation = r.generation;
  rr.descriptor.replay_attempt = ReplayAttemptId(1);
  auto rep = cache.replay(rr);
  std::printf("replay ok=%d out[0]=%f (expected 2.0)\n", rep.ok()?1:0, out[0]);
  // A second lookup is an exact hit.
  auto r2 = cache.lookup(req);
  std::printf("second hit=%d\n", r2.hit()?1:0);
  return (rep.ok() && r2.hit()) ? 0 : 1;
}
