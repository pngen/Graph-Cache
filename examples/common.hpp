#pragma once
#include "graphcache/cache.hpp"
#include "graphcache/compatibility.hpp"
#include "graphcache/topology.hpp"
#include "graphcache/serialization.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace ex_util {

inline gc::GraphLookupRequest cpu_request(const std::string& name, double alpha = 2.0,
                                          std::uint64_t bytes = 64, std::uint64_t depgen = 1,
                                          bool rebindable = true) {
  gc::GraphLookupRequest req;
  req.workload.logical_name = name; req.workload.logical_version = 1;
  req.backend.kind = gc::BackendKind::Cpu; req.backend.backend_name = "cpu"; req.backend.backend_version = 1;
  req.runtime.runtime_version = 1; req.runtime.graph_abi = 1; req.runtime.kernel_abi = 1;
  req.device.vendor = gc::AcceleratorVendor::Cpu; req.device.architecture = "cpu";
  gc::GraphDescriptor d;
  d.backend = req.backend; d.runtime = req.runtime; d.device = req.device;
  gc::GraphNodeDescriptor c; c.node_id = gc::GraphNodeId(1,1); c.category = gc::NodeCategory::MemoryCopy;
  c.name = "copy"; c.memory_bytes = bytes; c.reactive_input_indices = {0}; c.reactive_output_indices = {0};
  c.binding.binding_class = gc::BindingClass::ReplayMutableBinding; c.binding.rebinding_eligible = rebindable;
  gc::GraphNodeDescriptor k; k.node_id = gc::GraphNodeId(2,2); k.category = gc::NodeCategory::Kernel; k.name = "scale";
  k.kernel.name = "scale"; k.kernel.id_hi = 0x5000; k.kernel.id_lo = 1; k.kernel.generation = gc::GraphGeneration(depgen);
  k.kernel.abi = 1; k.kernel.content_digest = "k-scale"; k.kernel.contributes_to_correctness = true;
  k.memory_bytes = bytes; k.shape.dims = {static_cast<std::int64_t>(bytes/4)}; k.datatype = gc::Datatype::F32;
  k.layout = gc::TensorLayout::Contiguous; k.scalar.specialized = true; k.scalar.float_value = static_cast<float>(alpha);
  k.binding.binding_class = gc::BindingClass::ReplayMutableBinding; k.binding.rebinding_eligible = rebindable;
  k.reactive_input_indices = {0}; k.reactive_output_indices = {0};
  gc::GraphEdgeDescriptor e; e.edge_id = gc::GraphEdgeId(1,1); e.from = c.node_id; e.to = k.node_id; e.kind = gc::DependencyKind::Execution;
  d.nodes = {c, k}; d.edges = {e}; d.dependencies = {k.kernel};
  req.descriptor = d;
  return req;
}

inline std::vector<float> reference(const std::vector<float>& in, double alpha) {
  std::vector<float> out(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) out[i] = static_cast<float>(alpha) * in[i];
  return out;
}

inline int verify_output(const std::vector<float>& out, const std::vector<float>& expect, double tol = 1e-4) {
  for (std::size_t i = 0; i < out.size() && i < expect.size(); ++i) {
    if (std::fabs(out[i] - expect[i]) > tol) { std::printf("mismatch[%zu]=%f expected=%f\n", i, out[i], expect[i]); return 1; }
  }
  return 0;
}

inline int replay_and_check(gc::GraphCache& cache, gc::GraphLookupResult& r, const std::vector<float>& in, double alpha) {
  std::vector<float> out(in.size(), 0.0f);
  gc::ReplayBuffers bufs; bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
  bufs.input_bytes = {in.size()*sizeof(float)}; bufs.output_bytes = {out.size()*sizeof(float)};
  gc::GraphReplayRequest rr; rr.lease = r.lease; rr.buffers = bufs; rr.expected_generation = r.generation;
  rr.descriptor.replay_attempt = gc::ReplayAttemptId(1);
  auto rep = cache.replay(rr);
  if (!rep.ok()) { std::printf("replay failed\n"); return 1; }
  return verify_output(out, reference(in, alpha));
}

inline std::filesystem::path temp_dir(const std::string& tag) {
  auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  auto p = std::filesystem::temp_directory_path() / ("gc-ex-" + tag + "-" + std::to_string(stamp));
  std::filesystem::create_directories(p);
  return p;
}

} // namespace ex_util
