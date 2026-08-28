#pragma once
// Shared helpers for Graph Cache tests.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "gc_test.hpp"

#include "graphcache/cache.hpp"
#include "graphcache/compatibility.hpp"
#include "graphcache/cpu_backend.hpp"

namespace test_util {

inline gc::GraphNodeId node_id(std::uint64_t hi, std::uint64_t lo) {
  return gc::GraphNodeId(hi, lo);
}
inline void set_id(gc::GraphNodeId& id, std::uint64_t hi, std::uint64_t lo) {
  id = gc::GraphNodeId(hi, lo);
}

// A simple, valid CPU graph: one input buffer -> MemoryCopy -> kernel 'scale' -> output buffer.
inline gc::GraphDescriptor make_cpu_graph(const std::string& name, std::uint64_t bytes = 64,
                                          bool rebindable = true) {
  gc::GraphDescriptor g;
  g.backend.kind = gc::BackendKind::Cpu;
  g.backend.backend_name = "cpu";
  g.backend.backend_version = 1;
  g.backend.implementation_name = "graphcache-cpu-dag";
  g.runtime.runtime_version = 1;
  g.runtime.graph_abi = 1;
  g.runtime.kernel_abi = 1;
  g.device.vendor = gc::AcceleratorVendor::Cpu;
  g.device.architecture = "cpu";
  g.device.name = "host";
  g.sync.capture_mode = gc::CaptureMode::BackendManaged;
  g.sync.stream_semantics = gc::StreamSemantics::Default;
  g.provenance.graph_name = name;
  g.provenance.capture_toolchain = "gc-test";

  // copies input[0] -> output[0]
  gc::GraphNodeDescriptor copy;
  set_id(copy.node_id, 1, 1);
  copy.category = gc::NodeCategory::MemoryCopy;
  copy.name = "copy";
  copy.memory_bytes = bytes;
  copy.reactive_input_indices = {0};
  copy.reactive_output_indices = {0};
  copy.binding.binding_class = rebindable ? gc::BindingClass::ReplayMutableBinding
                                          : gc::BindingClass::ImmutableBinding;
  copy.binding.rebinding_eligible = rebindable;

  // kernel scale: y = x * 2.0
  gc::GraphNodeDescriptor kernel;
  set_id(kernel.node_id, 2, 2);
  kernel.category = gc::NodeCategory::Kernel;
  kernel.name = "scale";
  kernel.kernel.name = "scale";
  kernel.kernel.id_hi = 0x5000;
  kernel.kernel.id_lo = 1;
  kernel.kernel.generation = gc::GraphGeneration(1);
  kernel.kernel.abi = 1;
  kernel.kernel.content_digest = "kernelscale";
  kernel.kernel.contributes_to_correctness = true;
  kernel.launch.grid_x = 1; kernel.launch.grid_y = 1; kernel.launch.grid_z = 1;
  kernel.launch.block_x = 64; kernel.launch.block_y = 1; kernel.launch.block_z = 1;
  kernel.memory_bytes = bytes;
  kernel.shape.dims = {static_cast<std::int64_t>(bytes / 4)};
  kernel.datatype = gc::Datatype::F32;
  kernel.layout = gc::TensorLayout::Contiguous;
  kernel.scalar.specialized = true;
  kernel.scalar.float_value = 2.0;
  kernel.required_alignment = 128;
  kernel.binding.binding_class = rebindable ? gc::BindingClass::ReplayMutableBinding
                                            : gc::BindingClass::ImmutableBinding;
  kernel.binding.rebinding_eligible = rebindable;
  kernel.binding.memory_binding_schema = "scale:1";
  kernel.reactive_input_indices = {0};
  kernel.reactive_output_indices = {0};

  gc::GraphEdgeDescriptor e1;
  e1.edge_id = gc::GraphEdgeId(1, 1);
  e1.from = copy.node_id;
  e1.to = kernel.node_id;
  e1.kind = gc::DependencyKind::Execution;
  e1.label = "copy->scale";

  g.nodes.push_back(copy);
  g.nodes.push_back(kernel);
  g.edges.push_back(e1);
  g.dependencies.push_back(kernel.kernel);
  return g;
}

// Build a lookup request targeting the CPU graph.
inline gc::GraphLookupRequest make_cpu_request(const std::string& name, std::uint64_t bytes = 64,
                                               bool rebindable = true,
                                               const std::string& ns = "") {
  gc::GraphLookupRequest r;
  r.workload.logical_name = name;
  r.workload.logical_version = 1;
  r.workload.namespace_name = ns;
  r.backend.kind = gc::BackendKind::Cpu;
  r.backend.backend_name = "cpu";
  r.backend.backend_version = 1;
  r.runtime.runtime_version = 1;
  r.runtime.graph_abi = 1;
  r.runtime.kernel_abi = 1;
  r.device.vendor = gc::AcceleratorVendor::Cpu;
  r.device.architecture = "cpu";
  r.device.name = "host";
  r.descriptor = make_cpu_graph(name, bytes, rebindable);
  r.descriptor.sync.capture_mode = gc::CaptureMode::BackendManaged;
  r.policy = gc::GraphCompatibilityPolicy{};
  r.allow_capture = true;
  r.graph_name = name;
  return r;
}

} // namespace test_util
