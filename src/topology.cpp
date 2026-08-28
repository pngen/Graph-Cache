#include "graphcache/topology.hpp"

#include <deque>
#include <optional>
#include <unordered_map>

namespace gc {

namespace {

bool node_less(const GraphNodeId& a, const GraphNodeId& b) {
  if (a.hi != b.hi) return a.hi < b.hi;
  return a.lo < b.lo;
}
bool edge_less(const GraphEdgeId& a, const GraphEdgeId& b) {
  if (a.hi != b.hi) return a.hi < b.hi;
  return a.lo < b.lo;
}

// Field tags for node records.
enum : std::uint16_t {
  N_ID_HI = 1, N_ID_LO = 2, N_CATEGORY = 3, N_NAME = 4,
  N_K_START = 5, N_K_HI = 6, N_K_LO = 7, N_K_ABI = 8, N_K_GEN = 9,
  N_K_DIGEST = 10, N_K_CONTRIB = 11, N_K_NAME = 12,
  N_GRIDX = 13, N_GRIDY = 14, N_GRIDZ = 15, N_BLKX = 16, N_BLKY = 17,
  N_BLKZ = 18, N_SMEM = 19,
  N_MEMBYTES = 20, N_MSET = 21,
  N_DIMS = 22, N_DYNAMIC = 23, N_MAXRANK = 24,
  N_DTYPE = 25, N_LAYOUT = 26,
  N_SCALAR_SPEC = 27, N_SCALAR_INT = 28, N_SCALAR_FLOAT = 29,
  N_Q_MODE = 30, N_Q_GROUP = 31, N_Q_SYM = 32, N_Q_SCHEME = 33,
  N_BIND_CLASS = 34, N_BIND_ALIGN = 35, N_BIND_SCHEMA = 36, N_BIND_ELIGIBLE = 37,
  N_REQ_ALIGN = 38,
  N_CHILD_HI = 39, N_CHILD_LO = 40,
  N_IN_INDICES = 41, N_OUT_INDICES = 42,
  N_OPAQUE = 43,
  // Edge tags.
  E_ID_HI = 101, E_ID_LO = 102, E_FROM_HI = 103, E_FROM_LO = 104,
  E_TO_HI = 105, E_TO_LO = 106, E_KIND = 107, E_LABEL = 108
};

void encode_node(CanonicalWriter& w, const GraphNodeDescriptor& n) {
  w.put_u64(N_ID_HI, n.node_id.hi);
  w.put_u64(N_ID_LO, n.node_id.lo);
  w.put_u32(N_CATEGORY, static_cast<std::uint32_t>(n.category));
  w.put_str(N_NAME, n.name);
  if (n.category == NodeCategory::Kernel) {
    w.put_str(N_K_NAME, n.kernel.name);
    w.put_u64(N_K_HI, n.kernel.id_hi);
    w.put_u64(N_K_LO, n.kernel.id_lo);
    w.put_u64(N_K_GEN, n.kernel.generation.value);
    w.put_u32(N_K_ABI, n.kernel.abi);
    w.put_str(N_K_DIGEST, n.kernel.content_digest);
    w.put_bool(N_K_CONTRIB, n.kernel.contributes_to_correctness);
  }
  if (n.category == NodeCategory::Kernel) {
    w.put_u32(N_GRIDX, n.launch.grid_x);
    w.put_u32(N_GRIDY, n.launch.grid_y);
    w.put_u32(N_GRIDZ, n.launch.grid_z);
    w.put_u32(N_BLKX, n.launch.block_x);
    w.put_u32(N_BLKY, n.launch.block_y);
    w.put_u32(N_BLKZ, n.launch.block_z);
    w.put_u32(N_SMEM, n.launch.shared_memory_bytes);
  }
  w.put_u64(N_MEMBYTES, n.memory_bytes);
  if (n.category == NodeCategory::MemorySet) w.put_u8(N_MSET, n.memset_value);
  if (n.datatype != Datatype::None) {
    std::vector<std::uint64_t> dims;
    dims.reserve(n.shape.dims.size());
    for (auto d : n.shape.dims) dims.push_back(static_cast<std::uint64_t>(d));
    if (!dims.empty()) w.put_u64_list(N_DIMS, dims);
    w.put_bool(N_DYNAMIC, n.shape.dynamic);
    if (n.shape.max_rank) w.put_u32(N_MAXRANK, n.shape.max_rank);
    w.put_u32(N_DTYPE, static_cast<std::uint32_t>(n.datatype));
    w.put_u32(N_LAYOUT, static_cast<std::uint32_t>(n.layout));
  }
  if (n.scalar.specialized) {
    w.put_bool(N_SCALAR_SPEC, true);
    w.put_i64(N_SCALAR_INT, n.scalar.int_value);
    w.put_f64(N_SCALAR_FLOAT, n.scalar.float_value);
  }
  w.put_u32(N_Q_MODE, static_cast<std::uint32_t>(n.quantization.mode));
  if (n.quantization.mode != QuantizationConfig::Mode::None) {
    w.put_u32(N_Q_GROUP, n.quantization.group_size);
    w.put_bool(N_Q_SYM, n.quantization.symmetric);
    w.put_str(N_Q_SCHEME, n.quantization.custom_scheme);
  }
  w.put_u32(N_BIND_CLASS, static_cast<std::uint32_t>(n.binding.binding_class));
  w.put_u32(N_BIND_ALIGN, n.binding.alignment);
  w.put_str(N_BIND_SCHEMA, n.binding.memory_binding_schema);
  w.put_bool(N_BIND_ELIGIBLE, n.binding.rebinding_eligible);
  w.put_u64(N_REQ_ALIGN, n.required_alignment);
  if (n.child_graph_lo != 0) {
    w.put_u64(N_CHILD_HI, n.child_graph_hi);
    w.put_u64(N_CHILD_LO, n.child_graph_lo);
  }
  if (!n.reactive_input_indices.empty()) {
    std::vector<std::uint64_t> idx;
    for (auto i : n.reactive_input_indices) idx.push_back(i);
    w.put_u64_list(N_IN_INDICES, idx);
  }
  if (!n.reactive_output_indices.empty()) {
    std::vector<std::uint64_t> idx;
    for (auto i : n.reactive_output_indices) idx.push_back(i);
    w.put_u64_list(N_OUT_INDICES, idx);
  }
  if (!n.backend_opaque.empty()) {
    w.put_bytes(N_OPAQUE, n.backend_opaque);
  }
}

void encode_edge(CanonicalWriter& w, const GraphEdgeDescriptor& e) {
  w.put_u64(E_ID_HI, e.edge_id.hi);
  w.put_u64(E_ID_LO, e.edge_id.lo);
  w.put_u64(E_FROM_HI, e.from.hi);
  w.put_u64(E_FROM_LO, e.from.lo);
  w.put_u64(E_TO_HI, e.to.hi);
  w.put_u64(E_TO_LO, e.to.lo);
  w.put_u32(E_KIND, static_cast<std::uint32_t>(e.kind));
  w.put_str(E_LABEL, e.label);
}

} // namespace

Result<void> validate_topology(std::span<const GraphNodeDescriptor> nodes,
                               std::span<const GraphEdgeDescriptor> edges) {
  std::unordered_set<GraphNodeId> node_ids;
  node_ids.reserve(nodes.size());
  for (const auto& n : nodes) {
    if (!node_ids.insert(n.node_id).second) {
      return Result<void>::failure(Error(ErrorCode::TopologyDuplicateId,
                                        "duplicate GraphNodeId in topology"));
    }
  }
  // Edge endpoint validity, self-cycle, duplicate/contradictory edges.
  std::unordered_set<GraphEdgeId> edge_ids;
  struct PairKey {
    std::uint64_t from_hi, from_lo, to_hi, to_lo;
    bool operator==(const PairKey& o) const {
      return from_hi == o.from_hi && from_lo == o.from_lo && to_hi == o.to_hi && to_lo == o.to_lo;
    }
  };
  struct PairHash {
    std::size_t operator()(const PairKey& k) const {
      std::size_t h = std::hash<std::uint64_t>{}(k.from_hi) ^ (std::hash<std::uint64_t>{}(k.from_lo) << 1) ^
                      (std::hash<std::uint64_t>{}(k.to_hi) << 2) ^ (std::hash<std::uint64_t>{}(k.to_lo) << 3);
      return h;
    }
  };
  std::unordered_map<PairKey, DependencyKind, PairHash> pair_kind;
  for (const auto& e : edges) {
    if (e.from.hi == e.to.hi && e.from.lo == e.to.lo) {
      return Result<void>::failure(Error(ErrorCode::TopologySelfCycle,
                                        "edge with self-dependency"));
    }
    if (node_ids.find(e.from) == node_ids.end() || node_ids.find(e.to) == node_ids.end()) {
      return Result<void>::failure(Error(ErrorCode::TopologyDanglingEdge,
                                        "edge endpoint does not reference a node"));
    }
    if (!edge_ids.insert(e.edge_id).second) {
      return Result<void>::failure(Error(ErrorCode::TopologyDuplicateId,
                                        "duplicate GraphEdgeId in topology"));
    }
    PairKey key{e.from.hi, e.from.lo, e.to.hi, e.to.lo};
    auto it = pair_kind.find(key);
    if (it != pair_kind.end()) {
      if (it->second == e.kind) {
        return Result<void>::failure(Error(ErrorCode::TopologyDuplicateEdge,
                                          "duplicate edge between same endpoints"));
      }
      return Result<void>::failure(Error(ErrorCode::Contradictory,
                                        "contradictory edges of different kinds between same endpoints"));
    }
    pair_kind[key] = e.kind;
  }
  // Acyclic check (Kahn).
  std::unordered_map<GraphNodeId, std::size_t> in_degree;
  for (const auto& n : nodes) in_degree[n.node_id] = 0;
  std::unordered_map<GraphNodeId, std::vector<GraphNodeId>> adj;
  for (const auto& e : edges) {
    in_degree[e.to]++;
    adj[e.from].push_back(e.to);
  }
  std::deque<GraphNodeId> ready;
  for (const auto& [id, deg] : in_degree) {
    if (deg == 0) ready.push_back(id);
  }
  std::size_t visited = 0;
  while (!ready.empty()) {
    GraphNodeId cur = ready.front();
    ready.pop_front();
    ++visited;
    for (const auto& nxt : adj[cur]) {
      if (--in_degree[nxt] == 0) ready.push_back(nxt);
    }
  }
  if (visited != nodes.size()) {
    return Result<void>::failure(Error(ErrorCode::TopologyCycle,
                                      "topology contains a cycle"));
  }
  return Result<void>::success();
}

std::vector<std::uint8_t> canonical_topology(std::span<const GraphNodeDescriptor> nodes,
                                             std::span<const GraphEdgeDescriptor> edges) {
  std::vector<GraphNodeDescriptor> sn(nodes.begin(), nodes.end());
  std::vector<GraphEdgeDescriptor> se(edges.begin(), edges.end());
  std::sort(sn.begin(), sn.end(),
            [](const GraphNodeDescriptor& a, const GraphNodeDescriptor& b) {
              return node_less(a.node_id, b.node_id);
            });
  std::sort(se.begin(), se.end(),
            [](const GraphEdgeDescriptor& a, const GraphEdgeDescriptor& b) {
              return edge_less(a.edge_id, b.edge_id);
            });
  CanonicalWriter w;
  w.put_u32(1, static_cast<std::uint32_t>(sn.size()));
  for (const auto& n : sn) encode_node(w, n);
  w.put_u32(2, static_cast<std::uint32_t>(se.size()));
  for (const auto& e : se) encode_edge(w, e);
  return w.take();
}

GraphDescriptor canonicalize_topology(const GraphDescriptor& g) {
  GraphDescriptor out = g;
  std::sort(out.nodes.begin(), out.nodes.end(),
            [](const GraphNodeDescriptor& a, const GraphNodeDescriptor& b) {
              return node_less(a.node_id, b.node_id);
            });
  std::sort(out.edges.begin(), out.edges.end(),
            [](const GraphEdgeDescriptor& a, const GraphEdgeDescriptor& b) {
              return edge_less(a.edge_id, b.edge_id);
            });
  return out;
}

std::vector<std::uint8_t> canonical_semantic(const GraphDescriptor& g) {
  CanonicalWriter w;
  auto topo = canonical_topology(g.nodes, g.edges);
  w.put_bytes(201, topo);
  w.put_u32(202, static_cast<std::uint32_t>(g.backend.kind));
  w.put_str(203, g.backend.backend_name);
  w.put_u32(204, g.backend.backend_version);
  w.put_u32(205, g.runtime.runtime_version);
  w.put_u32(206, g.runtime.graph_abi);
  w.put_u32(207, g.runtime.kernel_abi);
  w.put_u32(208, g.runtime.driver_version);
  w.put_u32(209, static_cast<std::uint32_t>(g.device.vendor));
  w.put_str(210, g.device.architecture);
  w.put_u32(211, g.device.compute_major);
  w.put_u32(212, g.device.compute_minor);
  w.put_u32(213, static_cast<std::uint32_t>(g.sync.capture_mode));
  w.put_u32(214, static_cast<std::uint32_t>(g.sync.stream_semantics));
  w.put_bool(215, g.sync.requires_external_sync);
  w.put_u32(216, g.sync.event_dependency_count);
  w.put_u32(217, static_cast<std::uint32_t>(g.dependencies.size()));
  for (const auto& d : g.dependencies) {
    CanonicalWriter dw;
    dw.put_str(1, d.name);
    dw.put_u64(2, d.id_hi);
    dw.put_u64(3, d.id_lo);
    dw.put_u64(4, d.generation.value);
    dw.put_u32(5, d.abi);
    dw.put_str(6, d.content_digest);
    dw.put_bool(7, d.contributes_to_correctness);
    w.put_bytes(218, dw.bytes());
  }
  w.put_str(219, g.provenance.graph_name.empty() ? "anonymous" : g.provenance.graph_name);
  w.put_str(220, g.provenance.capture_toolchain);
  // Note: artifact_id is intentionally excluded from semantic canonical content
  // because it is DERIVED from this digest; including it would be circular.
  return w.take();
}

Sha256::digest_t topology_digest(std::span<const GraphNodeDescriptor> nodes,
                                 std::span<const GraphEdgeDescriptor> edges) {
  auto b = canonical_topology(nodes, edges);
  return Sha256::compute(b.data(), b.size());
}

Result<void> GraphDescriptor::validate_topology() const {
  return gc::validate_topology(nodes, edges);
}

Result<void> GraphDescriptor::finalize() {
  const auto check = validate_topology();
  if (!check.ok()) return check;
  GraphDescriptor canon = canonicalize_topology(*this);
  nodes = std::move(canon.nodes);
  edges = std::move(canon.edges);
  topology_canonical = canonical_topology(nodes, edges);
  topology_digest = Sha256::compute(topology_canonical.data(), topology_canonical.size());
  semantic_canonical = canonical_semantic(*this);
  semantic_digest = Sha256::compute(semantic_canonical.data(), semantic_canonical.size());
  if (!artifact_id.valid()) {
    // Derive a lossless artifact identity from the semantic digest high bits.
    artifact_id = GraphArtifactId(
        (static_cast<std::uint64_t>(semantic_digest[0]) << 56) |
        (static_cast<std::uint64_t>(semantic_digest[1]) << 48) |
        (static_cast<std::uint64_t>(semantic_digest[2]) << 40) |
        (static_cast<std::uint64_t>(semantic_digest[3]) << 32) |
        (static_cast<std::uint64_t>(semantic_digest[4]) << 24) |
        (static_cast<std::uint64_t>(semantic_digest[5]) << 16) |
        (static_cast<std::uint64_t>(semantic_digest[6]) << 8) |
        static_cast<std::uint64_t>(semantic_digest[7]),
        (static_cast<std::uint64_t>(semantic_digest[8]) << 56) |
        (static_cast<std::uint64_t>(semantic_digest[9]) << 48) |
        (static_cast<std::uint64_t>(semantic_digest[10]) << 40) |
        (static_cast<std::uint64_t>(semantic_digest[11]) << 32) |
        (static_cast<std::uint64_t>(semantic_digest[12]) << 24) |
        (static_cast<std::uint64_t>(semantic_digest[13]) << 16) |
        (static_cast<std::uint64_t>(semantic_digest[14]) << 8) |
        static_cast<std::uint64_t>(semantic_digest[15]));
  }
  return Result<void>::success();
}

} // namespace gc
