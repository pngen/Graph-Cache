#include "graphcache/serialization.hpp"

#include <unordered_map>

namespace gc {

namespace {

enum : std::uint16_t {
  GK_ARTIFACT_HI = 1, GK_ARTIFACT_LO = 2, GK_GEN = 3,
  GK_BK_KIND = 10, GK_BK_NAME = 11, GK_BK_VER = 12, GK_BK_IMPL = 13,
  GK_RT_VER = 20, GK_RT_GABI = 21, GK_RT_KABI = 22, GK_RT_DRIVER = 23,
  GK_DEV_VENDOR = 30, GK_DEV_ARCH = 31, GK_DEV_CCMAJ = 32, GK_DEV_CCMIN = 33,
  GK_DEV_IDX = 34, GK_DEV_NAME = 35,
  GK_CAP_MODE = 40, GK_STREAM = 41, GK_EXTSYNC = 42, GK_EVT = 43,
  GK_PROV_TS = 50, GK_PROV_SRC = 51, GK_PROV_DUR = 52, GK_PROV_TOOL = 53, GK_PROV_NAME = 54,
  GK_NODE_COUNT = 60, GK_NODE = 61,
  GK_EDGE_COUNT = 62, GK_EDGE = 63,
  GK_DEP_COUNT = 64, GK_DEP = 65
};

enum : std::uint16_t {
  GN_ID_HI = 1, GN_ID_LO = 2, GN_CAT = 3, GN_NAME = 4,
  GN_K_NAME = 5, GN_K_HI = 6, GN_K_LO = 7, GN_K_GEN = 8, GN_K_ABI = 9, GN_K_DIGEST = 10, GN_K_CONTRIB = 11,
  GN_GRIDX = 12, GN_GRIDY = 13, GN_GRIDZ = 14, GN_BLKX = 15, GN_BLKY = 16, GN_BLKZ = 17, GN_SMEM = 18,
  GN_MEMBYTES = 19, GN_MSET = 20,
  GN_DIMS = 21, GN_DYNAMIC = 22, GN_MAXRANK = 23, GN_DTYPE = 24, GN_LAYOUT = 25,
  GN_SCALAR_SPEC = 26, GN_SCALAR_INT = 27, GN_SCALAR_FLOAT = 28,
  GN_Q_MODE = 29, GN_Q_GROUP = 30, GN_Q_SYM = 31, GN_Q_SCHEME = 32,
  GN_BIND_CLASS = 33, GN_BIND_ALIGN = 34, GN_BIND_SCHEMA = 35, GN_BIND_ELIGIBLE = 36,
  GN_REQ_ALIGN = 37, GN_CHILD_HI = 38, GN_CHILD_LO = 39,
  GN_IN_INDICES = 40, GN_OUT_INDICES = 41, GN_OPAQUE = 42
};

enum : std::uint16_t {
  GE_ID_HI = 1, GE_ID_LO = 2, GE_FROM_HI = 3, GE_FROM_LO = 4, GE_TO_HI = 5, GE_TO_LO = 6,
  GE_KIND = 7, GE_LABEL = 8
};

enum : std::uint16_t {
  DEP_NAME = 1, DEP_HI = 2, DEP_LO = 3, DEP_GEN = 4, DEP_ABI = 5, DEP_DIGEST = 6, DEP_CONTRIB = 7
};

// ---- metadata tags ----
enum : std::uint16_t {
  MK_ARTIFACT_HI = 1, MK_ARTIFACT_LO = 2, MK_GEN = 3, MK_CACHE_GEN = 4,
  MK_WL_NAME = 10, MK_WL_VER = 11, MK_WL_NS = 12, MK_WL_REVMODE = 13, MK_WL_MODELREV = 14, MK_WL_POLICYGEN = 15,
  MK_ARTIFACT_SIZE = 20, MK_CAPTURE_COST = 21, MK_INSTANTIATE_COST = 22, MK_DEP_GEN = 23, MK_INVALIDATED = 24
};

void encode_node(CanonicalWriter& w, const GraphNodeDescriptor& n) {
  w.put_u64(GN_ID_HI, n.node_id.hi);
  w.put_u64(GN_ID_LO, n.node_id.lo);
  w.put_u32(GN_CAT, static_cast<std::uint32_t>(n.category));
  w.put_str(GN_NAME, n.name);
  if (n.category == NodeCategory::Kernel) {
    w.put_str(GN_K_NAME, n.kernel.name);
    w.put_u64(GN_K_HI, n.kernel.id_hi);
    w.put_u64(GN_K_LO, n.kernel.id_lo);
    w.put_u64(GN_K_GEN, n.kernel.generation.value);
    w.put_u32(GN_K_ABI, n.kernel.abi);
    w.put_str(GN_K_DIGEST, n.kernel.content_digest);
    w.put_bool(GN_K_CONTRIB, n.kernel.contributes_to_correctness);
    w.put_u32(GN_GRIDX, n.launch.grid_x);
    w.put_u32(GN_GRIDY, n.launch.grid_y);
    w.put_u32(GN_GRIDZ, n.launch.grid_z);
    w.put_u32(GN_BLKX, n.launch.block_x);
    w.put_u32(GN_BLKY, n.launch.block_y);
    w.put_u32(GN_BLKZ, n.launch.block_z);
    w.put_u32(GN_SMEM, n.launch.shared_memory_bytes);
  }
  w.put_u64(GN_MEMBYTES, n.memory_bytes);
  if (n.category == NodeCategory::MemorySet) w.put_u8(GN_MSET, n.memset_value);
  if (n.datatype != Datatype::None) {
    std::vector<std::uint64_t> dims;
    for (auto d : n.shape.dims) dims.push_back(static_cast<std::uint64_t>(d));
    if (!dims.empty()) w.put_u64_list(GN_DIMS, dims);
    w.put_bool(GN_DYNAMIC, n.shape.dynamic);
    if (n.shape.max_rank) w.put_u32(GN_MAXRANK, n.shape.max_rank);
    w.put_u32(GN_DTYPE, static_cast<std::uint32_t>(n.datatype));
    w.put_u32(GN_LAYOUT, static_cast<std::uint32_t>(n.layout));
  }
  if (n.scalar.specialized) {
    w.put_bool(GN_SCALAR_SPEC, true);
    w.put_i64(GN_SCALAR_INT, n.scalar.int_value);
    w.put_f64(GN_SCALAR_FLOAT, n.scalar.float_value);
  }
  w.put_u32(GN_Q_MODE, static_cast<std::uint32_t>(n.quantization.mode));
  if (n.quantization.mode != QuantizationConfig::Mode::None) {
    w.put_u32(GN_Q_GROUP, n.quantization.group_size);
    w.put_bool(GN_Q_SYM, n.quantization.symmetric);
    w.put_str(GN_Q_SCHEME, n.quantization.custom_scheme);
  }
  w.put_u32(GN_BIND_CLASS, static_cast<std::uint32_t>(n.binding.binding_class));
  w.put_u32(GN_BIND_ALIGN, n.binding.alignment);
  w.put_str(GN_BIND_SCHEMA, n.binding.memory_binding_schema);
  w.put_bool(GN_BIND_ELIGIBLE, n.binding.rebinding_eligible);
  w.put_u64(GN_REQ_ALIGN, n.required_alignment);
  if (n.child_graph_lo != 0) { w.put_u64(GN_CHILD_HI, n.child_graph_hi); w.put_u64(GN_CHILD_LO, n.child_graph_lo); }
  if (!n.reactive_input_indices.empty()) {
    std::vector<std::uint64_t> idx; for (auto i : n.reactive_input_indices) idx.push_back(i);
    w.put_u64_list(GN_IN_INDICES, idx);
  }
  if (!n.reactive_output_indices.empty()) {
    std::vector<std::uint64_t> idx; for (auto i : n.reactive_output_indices) idx.push_back(i);
    w.put_u64_list(GN_OUT_INDICES, idx);
  }
  if (!n.backend_opaque.empty()) w.put_bytes(GN_OPAQUE, n.backend_opaque);
}

void encode_edge(CanonicalWriter& w, const GraphEdgeDescriptor& e) {
  w.put_u64(GE_ID_HI, e.edge_id.hi);
  w.put_u64(GE_ID_LO, e.edge_id.lo);
  w.put_u64(GE_FROM_HI, e.from.hi);
  w.put_u64(GE_FROM_LO, e.from.lo);
  w.put_u64(GE_TO_HI, e.to.hi);
  w.put_u64(GE_TO_LO, e.to.lo);
  w.put_u32(GE_KIND, static_cast<std::uint32_t>(e.kind));
  w.put_str(GE_LABEL, e.label);
}

void encode_dep(CanonicalWriter& w, const KernelIdentityRef& d) {
  w.put_str(DEP_NAME, d.name);
  w.put_u64(DEP_HI, d.id_hi);
  w.put_u64(DEP_LO, d.id_lo);
  w.put_u64(DEP_GEN, d.generation.value);
  w.put_u32(DEP_ABI, d.abi);
  w.put_str(DEP_DIGEST, d.content_digest);
  w.put_bool(DEP_CONTRIB, d.contributes_to_correctness);
}

} // namespace
namespace {

bool decode_u64_list(std::span<const std::uint8_t> p, std::vector<std::uint64_t>& out) {
  std::uint32_t n;
  if (p.size() < 4 || !CanonicalReader::decode_u32(p.first(4), n)) return false;
  if (n > 4096 || p.size() < 4u + 8u * n) return false;
  std::size_t off = 4;
  for (std::uint32_t i = 0; i < n; ++i) {
    std::uint64_t v;
    if (!CanonicalReader::decode_u64(p.subspan(off, 8), v)) return false;
    out.push_back(v);
    off += 8;
  }
  return true;
}

bool decode_node(std::span<const std::uint8_t> bytes, GraphNodeDescriptor& n) {
  CanonicalReader r(bytes);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  bool has_shape_dim = false;
  while (r.next(tag, p)) {
    switch (tag) {
      case GN_ID_HI: if (!CanonicalReader::decode_u64(p, n.node_id.hi)) return false; break;
      case GN_ID_LO: if (!CanonicalReader::decode_u64(p, n.node_id.lo)) return false; break;
      case GN_CAT: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.category = static_cast<NodeCategory>(v); break; }
      case GN_NAME: if (!CanonicalReader::decode_str(p, n.name)) return false; break;
      case GN_K_NAME: if (!CanonicalReader::decode_str(p, n.kernel.name)) return false; break;
      case GN_K_HI: if (!CanonicalReader::decode_u64(p, n.kernel.id_hi)) return false; break;
      case GN_K_LO: if (!CanonicalReader::decode_u64(p, n.kernel.id_lo)) return false; break;
      case GN_K_GEN: if (!CanonicalReader::decode_u64(p, n.kernel.generation.value)) return false; break;
      case GN_K_ABI: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.kernel.abi = v; break; }
      case GN_K_DIGEST: if (!CanonicalReader::decode_str(p, n.kernel.content_digest)) return false; break;
      case GN_K_CONTRIB: if (p.empty()) return false; n.kernel.contributes_to_correctness = p[0] != 0; break;
      case GN_GRIDX: if (!CanonicalReader::decode_u32(p, n.launch.grid_x)) return false; break;
      case GN_GRIDY: if (!CanonicalReader::decode_u32(p, n.launch.grid_y)) return false; break;
      case GN_GRIDZ: if (!CanonicalReader::decode_u32(p, n.launch.grid_z)) return false; break;
      case GN_BLKX: if (!CanonicalReader::decode_u32(p, n.launch.block_x)) return false; break;
      case GN_BLKY: if (!CanonicalReader::decode_u32(p, n.launch.block_y)) return false; break;
      case GN_BLKZ: if (!CanonicalReader::decode_u32(p, n.launch.block_z)) return false; break;
      case GN_SMEM: if (!CanonicalReader::decode_u32(p, n.launch.shared_memory_bytes)) return false; break;
      case GN_MEMBYTES: if (!CanonicalReader::decode_u64(p, n.memory_bytes)) return false; break;
      case GN_MSET: if (p.empty()) return false; n.memset_value = p[0]; break;
      case GN_DIMS: { std::vector<std::uint64_t> v; if (!decode_u64_list(p, v)) return false; for (auto x : v) n.shape.dims.push_back(static_cast<std::int64_t>(x)); has_shape_dim = true; break; }
      case GN_DYNAMIC: if (p.empty()) return false; n.shape.dynamic = p[0] != 0; break;
      case GN_MAXRANK: if (!CanonicalReader::decode_u32(p, n.shape.max_rank)) return false; break;
      case GN_DTYPE: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.datatype = static_cast<Datatype>(v); break; }
      case GN_LAYOUT: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.layout = static_cast<TensorLayout>(v); break; }
      case GN_SCALAR_SPEC: if (!p.empty()) n.scalar.specialized = p[0] != 0; break;
      case GN_SCALAR_INT: if (!CanonicalReader::decode_i64(p, n.scalar.int_value)) return false; break;
      case GN_SCALAR_FLOAT: if (!CanonicalReader::decode_f64(p, n.scalar.float_value)) return false; break;
      case GN_Q_MODE: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.quantization.mode = static_cast<QuantizationConfig::Mode>(v); break; }
      case GN_Q_GROUP: if (!CanonicalReader::decode_u32(p, n.quantization.group_size)) return false; break;
      case GN_Q_SYM: if (p.empty()) return false; n.quantization.symmetric = p[0] != 0; break;
      case GN_Q_SCHEME: if (!CanonicalReader::decode_str(p, n.quantization.custom_scheme)) return false; break;
      case GN_BIND_CLASS: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; n.binding.binding_class = static_cast<BindingClass>(v); break; }
      case GN_BIND_ALIGN: if (!CanonicalReader::decode_u32(p, n.binding.alignment)) return false; break;
      case GN_BIND_SCHEMA: if (!CanonicalReader::decode_str(p, n.binding.memory_binding_schema)) return false; break;
      case GN_BIND_ELIGIBLE: if (p.empty()) return false; n.binding.rebinding_eligible = p[0] != 0; break;
      case GN_REQ_ALIGN: if (!CanonicalReader::decode_u64(p, n.required_alignment)) return false; break;
      case GN_CHILD_HI: if (!CanonicalReader::decode_u64(p, n.child_graph_hi)) return false; break;
      case GN_CHILD_LO: if (!CanonicalReader::decode_u64(p, n.child_graph_lo)) return false; break;
      case GN_IN_INDICES: { std::vector<std::uint64_t> v; if (!decode_u64_list(p, v)) return false; for (auto x : v) n.reactive_input_indices.push_back(static_cast<std::uint32_t>(x)); break; }
      case GN_OUT_INDICES: { std::vector<std::uint64_t> v; if (!decode_u64_list(p, v)) return false; for (auto x : v) n.reactive_output_indices.push_back(static_cast<std::uint32_t>(x)); break; }
      case GN_OPAQUE: n.backend_opaque.assign(p.begin(), p.end()); break;
      default: return false;
    }
  }
  if (r.malformed()) return false;
  (void)has_shape_dim;
  return true;
}

bool decode_edge(std::span<const std::uint8_t> bytes, GraphEdgeDescriptor& e) {
  CanonicalReader r(bytes);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  while (r.next(tag, p)) {
    switch (tag) {
      case GE_ID_HI: if (!CanonicalReader::decode_u64(p, e.edge_id.hi)) return false; break;
      case GE_ID_LO: if (!CanonicalReader::decode_u64(p, e.edge_id.lo)) return false; break;
      case GE_FROM_HI: if (!CanonicalReader::decode_u64(p, e.from.hi)) return false; break;
      case GE_FROM_LO: if (!CanonicalReader::decode_u64(p, e.from.lo)) return false; break;
      case GE_TO_HI: if (!CanonicalReader::decode_u64(p, e.to.hi)) return false; break;
      case GE_TO_LO: if (!CanonicalReader::decode_u64(p, e.to.lo)) return false; break;
      case GE_KIND: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; e.kind = static_cast<DependencyKind>(v); break; }
      case GE_LABEL: if (!CanonicalReader::decode_str(p, e.label)) return false; break;
      default: return false;
    }
  }
  return !r.malformed();
}

bool decode_dep(std::span<const std::uint8_t> bytes, KernelIdentityRef& d) {
  CanonicalReader r(bytes);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  while (r.next(tag, p)) {
    switch (tag) {
      case DEP_NAME: if (!CanonicalReader::decode_str(p, d.name)) return false; break;
      case DEP_HI: if (!CanonicalReader::decode_u64(p, d.id_hi)) return false; break;
      case DEP_LO: if (!CanonicalReader::decode_u64(p, d.id_lo)) return false; break;
      case DEP_GEN: if (!CanonicalReader::decode_u64(p, d.generation.value)) return false; break;
      case DEP_ABI: { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; d.abi = v; break; }
      case DEP_DIGEST: if (!CanonicalReader::decode_str(p, d.content_digest)) return false; break;
      case DEP_CONTRIB: if (p.empty()) return false; d.contributes_to_correctness = p[0] != 0; break;
      default: return false;
    }
  }
  return !r.malformed();
}

} // namespace

Result<void> serialize_descriptor(const GraphDescriptor& g, std::vector<std::uint8_t>& out) {
  CanonicalWriter w;
  w.put_u64(GK_ARTIFACT_HI, g.artifact_id.hi);
  w.put_u64(GK_ARTIFACT_LO, g.artifact_id.lo);
  w.put_u64(GK_GEN, g.generation.value);
  w.put_u32(GK_BK_KIND, static_cast<std::uint32_t>(g.backend.kind));
  w.put_str(GK_BK_NAME, g.backend.backend_name);
  w.put_u32(GK_BK_VER, g.backend.backend_version);
  w.put_str(GK_BK_IMPL, g.backend.implementation_name);
  w.put_u32(GK_RT_VER, g.runtime.runtime_version);
  w.put_u32(GK_RT_GABI, g.runtime.graph_abi);
  w.put_u32(GK_RT_KABI, g.runtime.kernel_abi);
  w.put_u32(GK_RT_DRIVER, g.runtime.driver_version);
  w.put_u32(GK_DEV_VENDOR, static_cast<std::uint32_t>(g.device.vendor));
  w.put_str(GK_DEV_ARCH, g.device.architecture);
  w.put_u32(GK_DEV_CCMAJ, g.device.compute_major);
  w.put_u32(GK_DEV_CCMIN, g.device.compute_minor);
  w.put_u32(GK_DEV_IDX, g.device.device_index);
  w.put_str(GK_DEV_NAME, g.device.name);
  w.put_u32(GK_CAP_MODE, static_cast<std::uint32_t>(g.sync.capture_mode));
  w.put_u32(GK_STREAM, static_cast<std::uint32_t>(g.sync.stream_semantics));
  w.put_bool(GK_EXTSYNC, g.sync.requires_external_sync);
  w.put_u32(GK_EVT, g.sync.event_dependency_count);
  w.put_u64(GK_PROV_TS, g.provenance.capture_timestamp_ms);
  w.put_str(GK_PROV_SRC, g.provenance.capture_source);
  w.put_u64(GK_PROV_DUR, g.provenance.capture_duration_us);
  w.put_str(GK_PROV_TOOL, g.provenance.capture_toolchain);
  w.put_str(GK_PROV_NAME, g.provenance.graph_name);
  w.put_u32(GK_NODE_COUNT, static_cast<std::uint32_t>(g.nodes.size()));
  for (const auto& n : g.nodes) {
    CanonicalWriter nw;
    encode_node(nw, n);
    w.put_bytes(GK_NODE, nw.bytes());
  }
  w.put_u32(GK_EDGE_COUNT, static_cast<std::uint32_t>(g.edges.size()));
  for (const auto& e : g.edges) {
    CanonicalWriter ew;
    encode_edge(ew, e);
    w.put_bytes(GK_EDGE, ew.bytes());
  }
  w.put_u32(GK_DEP_COUNT, static_cast<std::uint32_t>(g.dependencies.size()));
  for (const auto& d : g.dependencies) {
    CanonicalWriter dw;
    encode_dep(dw, d);
    w.put_bytes(GK_DEP, dw.bytes());
  }
  out = w.take();
  return Result<void>::success();
}

Result<GraphDescriptor> deserialize_descriptor(std::span<const std::uint8_t> bytes) {
  CanonicalReader r(bytes);
  GraphDescriptor g;
  std::span<const std::uint8_t> p;
  std::uint16_t tag;
  auto need = [&](std::uint16_t expect) {
    if (!r.next(tag, p) || tag != expect) return false;
    return true;
  };
  if (!r.next(tag, p) || tag != GK_ARTIFACT_HI) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "descriptor: artifact_hi"));
  if (!CanonicalReader::decode_u64(p, g.artifact_id.hi)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "artifact_hi decode"));
  if (!need(GK_ARTIFACT_LO) || !CanonicalReader::decode_u64(p, g.artifact_id.lo)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "artifact_lo"));
  if (!need(GK_GEN) || !CanonicalReader::decode_u64(p, g.generation.value)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "generation"));
  if (!need(GK_BK_KIND)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_kind"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_kind decode")); g.backend.kind = static_cast<BackendKind>(v); }
  if (!need(GK_BK_NAME) || !CanonicalReader::decode_str(p, g.backend.backend_name)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_name"));
  if (!need(GK_BK_VER)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_ver"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_ver decode")); g.backend.backend_version = v; }
  if (!need(GK_BK_IMPL) || !CanonicalReader::decode_str(p, g.backend.implementation_name)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "bk_impl"));
  if (!need(GK_RT_VER)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "rt_ver"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "rt_ver decode")); g.runtime.runtime_version = v; }
  if (!need(GK_RT_GABI)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "rt_gabi"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "gabi decode")); g.runtime.graph_abi = v; }
  if (!need(GK_RT_KABI)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "rt_kabi"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "kabi decode")); g.runtime.kernel_abi = v; }
  if (!need(GK_RT_DRIVER)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "rt_driver"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "driver decode")); g.runtime.driver_version = v; }
  if (!need(GK_DEV_VENDOR)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_vendor"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_vendor decode")); g.device.vendor = static_cast<AcceleratorVendor>(v); }
  if (!need(GK_DEV_ARCH) || !CanonicalReader::decode_str(p, g.device.architecture)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_arch"));
  if (!need(GK_DEV_CCMAJ)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cc_major"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cc_major decode")); g.device.compute_major = v; }
  if (!need(GK_DEV_CCMIN)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cc_minor"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cc_minor decode")); g.device.compute_minor = v; }
  if (!need(GK_DEV_IDX)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_idx"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_idx decode")); g.device.device_index = v; }
  if (!need(GK_DEV_NAME) || !CanonicalReader::decode_str(p, g.device.name)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dev_name"));
  if (!need(GK_CAP_MODE)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cap_mode"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "cap_mode decode")); g.sync.capture_mode = static_cast<CaptureMode>(v); }
  if (!need(GK_STREAM)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "stream"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "stream decode")); g.sync.stream_semantics = static_cast<StreamSemantics>(v); }
  if (!need(GK_EXTSYNC)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "extsync"));
  if (!p.empty()) g.sync.requires_external_sync = p[0] != 0;
  if (!need(GK_EVT)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "evt"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "evt decode")); g.sync.event_dependency_count = v; }
  if (!need(GK_PROV_TS)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_ts"));
  if (!CanonicalReader::decode_u64(p, g.provenance.capture_timestamp_ms)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_ts decode"));
  if (!need(GK_PROV_SRC) || !CanonicalReader::decode_str(p, g.provenance.capture_source)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_src"));
  if (!need(GK_PROV_DUR) || !CanonicalReader::decode_u64(p, g.provenance.capture_duration_us)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_dur"));
  if (!need(GK_PROV_TOOL) || !CanonicalReader::decode_str(p, g.provenance.capture_toolchain)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_tool"));
  if (!need(GK_PROV_NAME) || !CanonicalReader::decode_str(p, g.provenance.graph_name)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "prov_name"));
  if (!need(GK_NODE_COUNT)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "node_count"));
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 8192) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "node_count decode"));
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != GK_NODE) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "node"));
      GraphNodeDescriptor nd;
      if (!decode_node(p, nd)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "node decode"));
      g.nodes.push_back(std::move(nd));
    } }
  if (!need(GK_EDGE_COUNT)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "edge_count"));
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 16384) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "edge_count decode"));
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != GK_EDGE) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "edge"));
      GraphEdgeDescriptor ed;
      if (!decode_edge(p, ed)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "edge decode"));
      g.edges.push_back(std::move(ed));
    } }
  if (!need(GK_DEP_COUNT)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dep_count"));
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 8192) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dep_count decode"));
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != GK_DEP) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dep"));
      KernelIdentityRef d;
      if (!decode_dep(p, d)) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceCorrupt, "dep decode"));
      g.dependencies.push_back(std::move(d));
    } }
  if (!r.at_end()) return Result<GraphDescriptor>::failure(Error(ErrorCode::PersistenceTrailingGarbage, "trailing bytes after descriptor"));
  return Result<GraphDescriptor>::success(std::move(g));
}

Result<void> serialize_metadata(const PersistedMetadata& m, std::vector<std::uint8_t>& out) {
  CanonicalWriter w;
  w.put_u64(MK_ARTIFACT_HI, m.artifact_id.hi);
  w.put_u64(MK_ARTIFACT_LO, m.artifact_id.lo);
  w.put_u64(MK_GEN, m.generation.value);
  w.put_u64(MK_CACHE_GEN, m.cache_generation.value);
  w.put_str(MK_WL_NAME, m.workload.logical_name);
  w.put_u32(MK_WL_VER, m.workload.logical_version);
  w.put_str(MK_WL_NS, m.workload.namespace_name);
  w.put_u32(MK_WL_REVMODE, static_cast<std::uint32_t>(m.workload.revision_mode));
  w.put_str(MK_WL_MODELREV, m.workload.model_operator_revision);
  w.put_str(MK_WL_POLICYGEN, m.workload.policy_generation);
  w.put_u64(MK_ARTIFACT_SIZE, m.artifact_size);
  w.put_u64(MK_CAPTURE_COST, m.capture_cost_us);
  w.put_u64(MK_INSTANTIATE_COST, m.instantiate_cost_us);
  w.put_u64(MK_DEP_GEN, m.dependency_generation);
  w.put_bool(MK_INVALIDATED, m.invalidated);
  out = w.take();
  return Result<void>::success();
}

Result<PersistedMetadata> deserialize_metadata(std::span<const std::uint8_t> bytes) {
  CanonicalReader r(bytes);
  PersistedMetadata m;
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  auto need = [&](std::uint16_t expect) {
    if (!r.next(tag, p) || tag != expect) return false;
    return true;
  };
  if (!need(MK_ARTIFACT_HI) || !CanonicalReader::decode_u64(p, m.artifact_id.hi)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md artifact_hi"));
  if (!need(MK_ARTIFACT_LO) || !CanonicalReader::decode_u64(p, m.artifact_id.lo)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md artifact_lo"));
  if (!need(MK_GEN) || !CanonicalReader::decode_u64(p, m.generation.value)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md gen"));
  if (!need(MK_CACHE_GEN) || !CanonicalReader::decode_u64(p, m.cache_generation.value)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md cache_gen"));
  if (!need(MK_WL_NAME) || !CanonicalReader::decode_str(p, m.workload.logical_name)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md wl_name"));
  if (!need(MK_WL_VER)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md wl_ver"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md wl_ver decode")); m.workload.logical_version = v; }
  if (!need(MK_WL_NS) || !CanonicalReader::decode_str(p, m.workload.namespace_name)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md wl_ns"));
  if (!need(MK_WL_REVMODE)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md revmode"));
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md revmode decode")); m.workload.revision_mode = static_cast<WorkloadRevisionMode>(v); }
  if (!need(MK_WL_MODELREV) || !CanonicalReader::decode_str(p, m.workload.model_operator_revision)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md modelrev"));
  if (!need(MK_WL_POLICYGEN) || !CanonicalReader::decode_str(p, m.workload.policy_generation)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md policygen"));
  if (!need(MK_ARTIFACT_SIZE) || !CanonicalReader::decode_u64(p, m.artifact_size)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md size"));
  if (!need(MK_CAPTURE_COST) || !CanonicalReader::decode_u64(p, m.capture_cost_us)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md cap_cost"));
  if (!need(MK_INSTANTIATE_COST) || !CanonicalReader::decode_u64(p, m.instantiate_cost_us)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md inst_cost"));
  if (!need(MK_DEP_GEN) || !CanonicalReader::decode_u64(p, m.dependency_generation)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md dep_gen"));
  if (!need(MK_INVALIDATED)) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceCorrupt, "md invalidated"));
  if (!p.empty()) m.invalidated = p[0] != 0;
  if (!r.at_end()) return Result<PersistedMetadata>::failure(Error(ErrorCode::PersistenceTrailingGarbage, "trailing bytes after metadata"));
  return Result<PersistedMetadata>::success(std::move(m));
}

} // namespace gc
