#include "graphcache/cpu_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace gc {

namespace {

// Deterministic CPU kernel names understood by the CPU backend.
enum class CpuKernel : std::uint32_t {
  Identity = 1,
  Scale = 2,
  AddScalar = 3,
  Linear = 4,
  Square = 5,
  Relu = 6
};

struct CpuNode {
  GraphNodeId id;
  NodeCategory category;
  std::string kernel_name;
  CpuKernel kernel{CpuKernel::Identity};
  std::uint32_t src_index{0};  // index into ReplayBuffers.inputs
  std::uint32_t dst_index{0};  // index into ReplayBuffers.outputs
  std::uint64_t bytes{0};
  std::uint8_t memset_value{0};
  ScalarSpecialization scalar;
  Datatype datatype{Datatype::F32};
};

struct CpuGraph {
  std::vector<CpuNode> nodes;
  std::vector<GraphEdgeDescriptor> edges;
  std::vector<GraphNodeId> order;  // deterministic topological order
  std::uint64_t total_bytes{0};
  std::uint64_t edge_count{0};
};

CpuKernel classify_kernel(const GraphNodeDescriptor& n) {
  const std::string& name = n.kernel.name;
  if (name == "identity") return CpuKernel::Identity;
  if (name == "scale") return CpuKernel::Scale;
  if (name == "addscalar") return CpuKernel::AddScalar;
  if (name == "linear") return CpuKernel::Linear;
  if (name == "square") return CpuKernel::Square;
  if (name == "relu") return CpuKernel::Relu;
  return CpuKernel::Identity;
}

std::uint64_t element_count(const CpuNode& n) {
  // The CPU synthetic executor always operates on 4-byte (float) lanes; the
  // datatype participates in compatibility identity, not in the deterministic
  // CPU execution footprint. This keeps the lane count == bytes/4 so the
  // executor never overruns a buffer sized to n.bytes.
  return n.bytes / sizeof(float);
}

} // namespace

BackendIdentity CpuBackend::identity() const {
  BackendIdentity id;
  id.kind = BackendKind::Cpu;
  id.backend_name = "cpu";
  id.backend_version = 1;
  id.implementation_name = "graphcache-cpu-dag";
  return id;
}

Result<std::shared_ptr<void>> CpuBackend::capture(const GraphDescriptor& desc,
                                                  CaptureAttemptId /*attempt*/) {
  auto check = desc.validate_topology();
  if (!check.ok()) {
    return Result<std::shared_ptr<void>>::failure(check.error());
  }
  auto graph = std::make_shared<CpuGraph>();
  std::unordered_map<GraphNodeId, std::uint32_t> index;
  index.reserve(desc.nodes.size());
  graph->nodes.reserve(desc.nodes.size());
  std::unordered_set<GraphNodeId> seen;
  std::uint32_t idx = 0;
  for (const auto& n : desc.nodes) {
    if (!seen.insert(n.node_id).second) continue;
    CpuNode cn;
    cn.id = n.node_id;
    cn.category = n.category;
    cn.kernel_name = n.kernel.name;
    cn.kernel = classify_kernel(n);
    if (!n.reactive_input_indices.empty()) cn.src_index = n.reactive_input_indices.front();
    if (!n.reactive_output_indices.empty()) cn.dst_index = n.reactive_output_indices.front();
    cn.bytes = n.memory_bytes;
    cn.memset_value = n.memset_value;
    cn.scalar = n.scalar;
    cn.datatype = n.datatype;
    graph->nodes.push_back(cn);
    index[n.node_id] = idx++;
    graph->total_bytes += n.memory_bytes;
  }
  graph->edges = desc.edges;
  graph->edge_count = graph->edges.size();

  // Deterministic topological order (Kahn with deterministic tie-break by node id).
  std::unordered_map<GraphNodeId, std::size_t> indeg;
  std::unordered_map<GraphNodeId, std::vector<GraphNodeId>> adj;
  for (const auto& n : desc.nodes) indeg[n.node_id] = 0;
  for (const auto& e : desc.edges) {
    indeg[e.to]++;
    adj[e.from].push_back(e.to);
  }
  auto node_less = [](const GraphNodeId& a, const GraphNodeId& b) {
    if (a.hi != b.hi) return a.hi < b.hi;
    return a.lo < b.lo;
  };
  std::deque<GraphNodeId> ready;
  for (const auto& [id, d] : indeg) if (d == 0) ready.push_back(id);
  std::vector<GraphNodeId> order;
  order.reserve(desc.nodes.size());
  while (!ready.empty()) {
    std::sort(ready.begin(), ready.end(), node_less);
    GraphNodeId cur = ready.front();
    ready.pop_front();
    order.push_back(cur);
    for (auto& nxt : adj[cur]) if (--indeg[nxt] == 0) ready.push_back(nxt);
  }
  graph->order = std::move(order);
  return Result<std::shared_ptr<void>>::success(std::static_pointer_cast<void>(graph));
}

Result<void> CpuBackend::load(const GraphDescriptor& desc, std::shared_ptr<void>& /*exec*/) {
  // CPU graphs are always host-resident; nothing to instantiate.
  (void)desc;
  return Result<void>::success();
}

Result<void> CpuBackend::validate(const GraphDescriptor& desc, const std::shared_ptr<void>& exec) {
  auto g = std::static_pointer_cast<CpuGraph>(exec);
  if (!g) return Result<void>::failure(Error(ErrorCode::InvalidArgument, "null cpu graph handle"));
  auto check = desc.validate_topology();
  if (!check.ok()) return check;
  return Result<void>::success();
}

namespace {

// Execute one elementwise kernel over [in, in+n) -> [out, out+n).
void run_cpu_kernel(CpuKernel op, const float* in, float* out, std::uint64_t n,
                    const ScalarSpecialization& sc) {
  float a = static_cast<float>(sc.float_value);
  float b = static_cast<float>(sc.int_value);
  for (std::uint64_t i = 0; i < n; ++i) {
    float x = in[i];
    float y = 0.0f;
    switch (op) {
      case CpuKernel::Identity: y = x; break;
      case CpuKernel::Scale: y = x * a; break;
      case CpuKernel::AddScalar: y = x + a; break;
      case CpuKernel::Linear: y = x * b + a; break;
      case CpuKernel::Square: y = x * x; break;
      case CpuKernel::Relu: y = x > 0.0f ? x : 0.0f; break;
    }
    out[i] = y;
  }
}

} // namespace

Result<GraphReplayResult> CpuBackend::replay(const GraphDescriptor& desc,
                                             const std::shared_ptr<void>& exec,
                                             const ReplayBuffers& buffers,
                                             const ReplayDescriptor& rd) {
  auto g = std::static_pointer_cast<CpuGraph>(exec);
  if (!g) return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "null cpu graph handle"));
  GraphReplayResult r;
  r.replay_attempt = rd.replay_attempt.value;
  (void)desc;
  Sha256 h;
  for (const auto& nid : g->order) {
    const CpuNode* node = nullptr;
    for (const auto& n : g->nodes) if (n.id == nid) { node = &n; break; }
    if (!node) {
      return Result<GraphReplayResult>::failure(Error(ErrorCode::TopologyInvalid, "unknown node in replay order"));
    }
    switch (node->category) {
      case NodeCategory::MemoryCopy: {
        if (node->src_index >= buffers.inputs.size() || node->dst_index >= buffers.outputs.size())
          return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "memory copy buffer out of range"));
        const auto* src = static_cast<const std::uint8_t*>(buffers.inputs[node->src_index]);
        auto* dst = static_cast<std::uint8_t*>(buffers.outputs[node->dst_index]);
        std::memcpy(dst, src, static_cast<std::size_t>(node->bytes));
        r.replayed_nodes++;
        break;
      }
      case NodeCategory::MemorySet: {
        if (node->dst_index >= buffers.outputs.size())
          return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "memset buffer out of range"));
        auto* dst = static_cast<std::uint8_t*>(buffers.outputs[node->dst_index]);
        std::memset(dst, node->memset_value, static_cast<std::size_t>(node->bytes));
        r.replayed_nodes++;
        break;
      }
      case NodeCategory::Kernel: {
        if (node->src_index >= buffers.inputs.size() || node->dst_index >= buffers.outputs.size())
          return Result<GraphReplayResult>::failure(Error(ErrorCode::InvalidArgument, "kernel buffer out of range"));
        auto n = element_count(*node);
        auto* src = static_cast<const float*>(buffers.inputs[node->src_index]);
        auto* dst = static_cast<float*>(buffers.outputs[node->dst_index]);
        run_cpu_kernel(node->kernel, src, dst, n, node->scalar);
        h.update(dst, static_cast<std::size_t>(node->bytes));
        r.replayed_nodes++;
        break;
      }
      case NodeCategory::HostOperation:
      case NodeCategory::Synchronization:
      case NodeCategory::EventPrimitive:
      case NodeCategory::ChildGraph:
      case NodeCategory::BackendOpaque: {
        // CPU backend treats these as no-ops for deterministic CPU execution.
        break;
      }
    }
  }
  r.output_digest = h.final();
  r.output_digest_hex = Sha256::hex(r.output_digest);
  r.ok = true;
  return Result<GraphReplayResult>::success(std::move(r));
}

Result<void> CpuBackend::rebind(const GraphDescriptor& desc, const std::shared_ptr<void>& exec,
                                const RebindSpec& /*spec*/) {
  // CPU graph bindings are resolved at replay time via ReplayBuffers, so the
  // backend accepts a legal rebind as a no-op. The engine decides legality.
  auto g = std::static_pointer_cast<CpuGraph>(exec);
  if (!g) return Result<void>::failure(Error(ErrorCode::InvalidArgument, "null cpu graph handle"));
  (void)desc;
  return Result<void>::success();
}

Result<bool> CpuBackend::can_rebind(const GraphDescriptor& desc) const {
  for (const auto& n : desc.nodes) {
    if (n.binding.binding_class == BindingClass::RecaptureRequiredBinding ||
        n.binding.binding_class == BindingClass::ImmutableBinding) {
      return Result<bool>::success(false);
    }
  }
  return Result<bool>::success(true);
}

Result<void> CpuBackend::unload(const GraphDescriptor& desc, std::shared_ptr<void>& exec) {
  (void)desc;
  exec.reset();
  return Result<void>::success();
}

std::uint64_t CpuBackend::backend_resident_bytes(const GraphDescriptor& desc,
                                                 const std::shared_ptr<void>& exec) const {
  (void)exec;
  std::uint64_t total = 0;
  for (const auto& n : desc.nodes) total += n.memory_bytes;
  return total;
}

} // namespace gc
