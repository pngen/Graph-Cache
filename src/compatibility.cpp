#include "graphcache/compatibility.hpp"

#include <algorithm>
#include <unordered_set>

namespace gc {

namespace {

// Compatibility field tags (encode order == decode order).
enum : std::uint16_t {
  K_WL_NAME = 1, K_WL_VER = 2, K_WL_NS = 3, K_WL_REVMODE = 4,
  K_WL_MODELREV = 5, K_WL_POLICYGEN = 6,
  K_BK_KIND = 10, K_BK_NAME = 11, K_BK_VER = 12, K_BK_IMPL = 13,
  K_RT_VER = 20, K_RT_GRAPHABI = 21, K_RT_KERNELABI = 22, K_RT_DRIVER = 23,
  K_DEV_VENDOR = 30, K_DEV_ARCH = 31, K_DEV_CCMAJ = 32, K_DEV_CCMIN = 33,
  K_DEV_IDX = 34, K_DEV_NAME = 35,
  K_CM_MODE = 40, K_CM_STREAM = 41,
  K_TOPO = 50,
  K_INSHAPES_COUNT = 60, K_INSHAPE = 61,
  K_OUTSHAPES_COUNT = 62, K_OUTSHAPE = 63,
  K_DTYPES_COUNT = 64, K_DTYPE = 65,
  K_LAYOUTS_COUNT = 66, K_LAYOUT = 67,
  K_SCALAR_SPEC = 70, K_SCALAR_INT = 71, K_SCALAR_FLOAT = 72,
  K_Q_MODE = 73, K_Q_GROUP = 74, K_Q_SYM = 75, K_Q_SCHEME = 76,
  K_BIND_CLASS = 77, K_BIND_ALIGN = 78, K_BIND_SCHEMA = 79, K_BIND_ELIGIBLE = 80,
  K_REQ_ALIGN = 81, K_MEM_SCHEMA = 82,
  K_DEP_COUNT = 90, K_DEP = 91,
  K_MODELREV = 95, K_POLICYGEN = 96
};

// Shape inner tags.
enum : std::uint16_t { SH_DIMS = 1, SH_DYNAMIC = 2, SH_MAXRANK = 3 };
// Dependency inner tags.
enum : std::uint16_t { DEP_NAME = 1, DEP_HI = 2, DEP_LO = 3, DEP_GEN = 4, DEP_ABI = 5, DEP_DIGEST = 6, DEP_CONTRIB = 7 };

std::vector<std::uint8_t> encode_shape(const ShapeDescriptor& s) {
  CanonicalWriter w;
  if (!s.dims.empty()) {
    std::vector<std::uint64_t> dims;
    dims.reserve(s.dims.size());
    for (auto d : s.dims) dims.push_back(static_cast<std::uint64_t>(d));
    w.put_u64_list(SH_DIMS, dims);
  }
  w.put_bool(SH_DYNAMIC, s.dynamic);
  if (s.max_rank) w.put_u32(SH_MAXRANK, s.max_rank);
  return w.take();
}

bool decode_shape(std::span<const std::uint8_t> bytes, ShapeDescriptor& s) {
  CanonicalReader r(bytes);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  while (r.next(tag, p)) {
    if (tag == SH_DIMS) {
      std::uint32_t n;
      if (!CanonicalReader::decode_u32(p.subspan(0, 4), n)) return false;
      if (n > 4096) return false;
      std::size_t off = 4;
      for (std::uint32_t i = 0; i < n; ++i) {
        if (off + 8 > p.size()) return false;
        std::uint64_t v;
        if (!CanonicalReader::decode_u64(p.subspan(off, 8), v)) return false;
        s.dims.push_back(static_cast<std::int64_t>(v));
        off += 8;
      }
    } else if (tag == SH_DYNAMIC) {
      if (p.empty()) return false;
      s.dynamic = p[0] != 0;
    } else if (tag == SH_MAXRANK) {
      std::uint32_t v;
      if (!CanonicalReader::decode_u32(p, v)) return false;
      s.max_rank = v;
    } else {
      return false;
    }
  }
  return !r.malformed();
}

std::vector<std::uint8_t> encode_dep(const KernelIdentityRef& d) {
  CanonicalWriter w;
  w.put_str(DEP_NAME, d.name);
  w.put_u64(DEP_HI, d.id_hi);
  w.put_u64(DEP_LO, d.id_lo);
  w.put_u64(DEP_GEN, d.generation.value);
  w.put_u32(DEP_ABI, d.abi);
  w.put_str(DEP_DIGEST, d.content_digest);
  w.put_bool(DEP_CONTRIB, d.contributes_to_correctness);
  return w.take();
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

void encode_facts(CanonicalWriter& w, const CompatibilityFacts& f) {
  w.put_str(K_WL_NAME, f.workload.logical_name);
  w.put_u32(K_WL_VER, f.workload.logical_version);
  w.put_str(K_WL_NS, f.workload.namespace_name);
  w.put_u32(K_WL_REVMODE, static_cast<std::uint32_t>(f.workload.revision_mode));
  w.put_str(K_WL_MODELREV, f.workload.model_operator_revision);
  w.put_str(K_WL_POLICYGEN, f.workload.policy_generation);
  w.put_u32(K_BK_KIND, static_cast<std::uint32_t>(f.backend.kind));
  w.put_str(K_BK_NAME, f.backend.backend_name);
  w.put_u32(K_BK_VER, f.backend.backend_version);
  w.put_str(K_BK_IMPL, f.backend.implementation_name);
  w.put_u32(K_RT_VER, f.runtime.runtime_version);
  w.put_u32(K_RT_GRAPHABI, f.runtime.graph_abi);
  w.put_u32(K_RT_KERNELABI, f.runtime.kernel_abi);
  w.put_u32(K_RT_DRIVER, f.runtime.driver_version);
  w.put_u32(K_DEV_VENDOR, static_cast<std::uint32_t>(f.device.vendor));
  w.put_str(K_DEV_ARCH, f.device.architecture);
  w.put_u32(K_DEV_CCMAJ, f.device.compute_major);
  w.put_u32(K_DEV_CCMIN, f.device.compute_minor);
  w.put_u32(K_DEV_IDX, f.device.device_index);
  w.put_str(K_DEV_NAME, f.device.name);
  w.put_u32(K_CM_MODE, static_cast<std::uint32_t>(f.capture_mode));
  w.put_u32(K_CM_STREAM, static_cast<std::uint32_t>(f.stream_semantics));
  w.put_bytes(K_TOPO, f.topology_canonical);
  w.put_u32(K_INSHAPES_COUNT, static_cast<std::uint32_t>(f.input_shapes.size()));
  for (const auto& s : f.input_shapes) {
    auto b = encode_shape(s);
    w.put_bytes(K_INSHAPE, b);
  }
  w.put_u32(K_OUTSHAPES_COUNT, static_cast<std::uint32_t>(f.output_shapes.size()));
  for (const auto& s : f.output_shapes) {
    auto b = encode_shape(s);
    w.put_bytes(K_OUTSHAPE, b);
  }
  w.put_u32(K_DTYPES_COUNT, static_cast<std::uint32_t>(f.datatypes.size()));
  for (auto d : f.datatypes) w.put_u32(K_DTYPE, static_cast<std::uint32_t>(d));
  w.put_u32(K_LAYOUTS_COUNT, static_cast<std::uint32_t>(f.layouts.size()));
  for (auto l : f.layouts) w.put_u32(K_LAYOUT, static_cast<std::uint32_t>(l));
  w.put_bool(K_SCALAR_SPEC, f.scalar.specialized);
  if (f.scalar.specialized) {
    w.put_i64(K_SCALAR_INT, f.scalar.int_value);
    w.put_f64(K_SCALAR_FLOAT, f.scalar.float_value);
  }
  w.put_u32(K_Q_MODE, static_cast<std::uint32_t>(f.quantization.mode));
  if (f.quantization.mode != QuantizationConfig::Mode::None) {
    w.put_u32(K_Q_GROUP, f.quantization.group_size);
    w.put_bool(K_Q_SYM, f.quantization.symmetric);
    w.put_str(K_Q_SCHEME, f.quantization.custom_scheme);
  }
  w.put_u32(K_BIND_CLASS, static_cast<std::uint32_t>(f.binding.binding_class));
  w.put_u32(K_BIND_ALIGN, f.binding.alignment);
  w.put_str(K_BIND_SCHEMA, f.binding.memory_binding_schema);
  w.put_bool(K_BIND_ELIGIBLE, f.binding.rebinding_eligible);
  w.put_u64(K_REQ_ALIGN, f.required_alignment);
  w.put_str(K_MEM_SCHEMA, f.memory_binding_schema);
  w.put_u32(K_DEP_COUNT, static_cast<std::uint32_t>(f.dependencies.size()));
  for (const auto& d : f.dependencies) {
    auto b = encode_dep(d);
    w.put_bytes(K_DEP, b);
  }
  w.put_str(K_MODELREV, f.model_operator_revision);
  w.put_str(K_POLICYGEN, f.policy_generation);
}

bool decode_facts(std::span<const std::uint8_t> canonical, CompatibilityFacts& out) {
  CanonicalReader r(canonical);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  // A strict decode expects the encoder's exact order. Tag mismatch => reject.
  if (!r.next(tag, p)) return false;
  if (tag != K_WL_NAME) return false;
  if (!CanonicalReader::decode_str(p, out.workload.logical_name)) return false;
  if (!r.next(tag, p) || tag != K_WL_VER) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.workload.logical_version = v; }
  if (!r.next(tag, p) || tag != K_WL_NS) return false;
  if (!CanonicalReader::decode_str(p, out.workload.namespace_name)) return false;
  if (!r.next(tag, p) || tag != K_WL_REVMODE) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.workload.revision_mode = static_cast<WorkloadRevisionMode>(v); }
  if (!r.next(tag, p) || tag != K_WL_MODELREV) return false;
  if (!CanonicalReader::decode_str(p, out.workload.model_operator_revision)) return false;
  if (!r.next(tag, p) || tag != K_WL_POLICYGEN) return false;
  if (!CanonicalReader::decode_str(p, out.workload.policy_generation)) return false;
  if (!r.next(tag, p) || tag != K_BK_KIND) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.backend.kind = static_cast<BackendKind>(v); }
  if (!r.next(tag, p) || tag != K_BK_NAME) return false;
  if (!CanonicalReader::decode_str(p, out.backend.backend_name)) return false;
  if (!r.next(tag, p) || tag != K_BK_VER) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.backend.backend_version = v; }
  if (!r.next(tag, p) || tag != K_BK_IMPL) return false;
  if (!CanonicalReader::decode_str(p, out.backend.implementation_name)) return false;
  if (!r.next(tag, p) || tag != K_RT_VER) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.runtime.runtime_version = v; }
  if (!r.next(tag, p) || tag != K_RT_GRAPHABI) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.runtime.graph_abi = v; }
  if (!r.next(tag, p) || tag != K_RT_KERNELABI) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.runtime.kernel_abi = v; }
  if (!r.next(tag, p) || tag != K_RT_DRIVER) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.runtime.driver_version = v; }
  if (!r.next(tag, p) || tag != K_DEV_VENDOR) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.device.vendor = static_cast<AcceleratorVendor>(v); }
  if (!r.next(tag, p) || tag != K_DEV_ARCH) return false;
  if (!CanonicalReader::decode_str(p, out.device.architecture)) return false;
  if (!r.next(tag, p) || tag != K_DEV_CCMAJ) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.device.compute_major = v; }
  if (!r.next(tag, p) || tag != K_DEV_CCMIN) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.device.compute_minor = v; }
  if (!r.next(tag, p) || tag != K_DEV_IDX) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.device.device_index = v; }
  if (!r.next(tag, p) || tag != K_DEV_NAME) return false;
  if (!CanonicalReader::decode_str(p, out.device.name)) return false;
  if (!r.next(tag, p) || tag != K_CM_MODE) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.capture_mode = static_cast<CaptureMode>(v); }
  if (!r.next(tag, p) || tag != K_CM_STREAM) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.stream_semantics = static_cast<StreamSemantics>(v); }
  if (!r.next(tag, p) || tag != K_TOPO) return false;
  out.topology_canonical.assign(p.begin(), p.end());
  if (!r.next(tag, p) || tag != K_INSHAPES_COUNT) return false;
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 4096) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != K_INSHAPE) return false;
      ShapeDescriptor s;
      if (!decode_shape(p, s)) return false;
      out.input_shapes.push_back(std::move(s));
    } }
  if (!r.next(tag, p) || tag != K_OUTSHAPES_COUNT) return false;
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 4096) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != K_OUTSHAPE) return false;
      ShapeDescriptor s;
      if (!decode_shape(p, s)) return false;
      out.output_shapes.push_back(std::move(s));
    } }
  if (!r.next(tag, p) || tag != K_DTYPES_COUNT) return false;
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 4096) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != K_DTYPE) return false;
      std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false;
      out.datatypes.push_back(static_cast<Datatype>(v));
    } }
  if (!r.next(tag, p) || tag != K_LAYOUTS_COUNT) return false;
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 4096) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != K_LAYOUT) return false;
      std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false;
      out.layouts.push_back(static_cast<TensorLayout>(v));
    } }
  if (!r.next(tag, p) || tag != K_SCALAR_SPEC) return false;
  if (p.empty()) return false;
  out.scalar.specialized = p[0] != 0;
  if (out.scalar.specialized) {
    if (!r.next(tag, p) || tag != K_SCALAR_INT) return false;
    if (!CanonicalReader::decode_i64(p, out.scalar.int_value)) return false;
    if (!r.next(tag, p) || tag != K_SCALAR_FLOAT) return false;
    if (!CanonicalReader::decode_f64(p, out.scalar.float_value)) return false;
  }
  if (!r.next(tag, p) || tag != K_Q_MODE) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.quantization.mode = static_cast<QuantizationConfig::Mode>(v); }
  if (out.quantization.mode != QuantizationConfig::Mode::None) {
    if (!r.next(tag, p) || tag != K_Q_GROUP) return false;
    if (!CanonicalReader::decode_u32(p, out.quantization.group_size)) return false;
    if (!r.next(tag, p) || tag != K_Q_SYM) return false;
    if (p.empty()) return false;
    out.quantization.symmetric = p[0] != 0;
    if (!r.next(tag, p) || tag != K_Q_SCHEME) return false;
    if (!CanonicalReader::decode_str(p, out.quantization.custom_scheme)) return false;
  }
  if (!r.next(tag, p) || tag != K_BIND_CLASS) return false;
  { std::uint32_t v; if (!CanonicalReader::decode_u32(p, v)) return false; out.binding.binding_class = static_cast<BindingClass>(v); }
  if (!r.next(tag, p) || tag != K_BIND_ALIGN) return false;
  if (!CanonicalReader::decode_u32(p, out.binding.alignment)) return false;
  if (!r.next(tag, p) || tag != K_BIND_SCHEMA) return false;
  if (!CanonicalReader::decode_str(p, out.binding.memory_binding_schema)) return false;
  if (!r.next(tag, p) || tag != K_BIND_ELIGIBLE) return false;
  if (p.empty()) return false;
  out.binding.rebinding_eligible = p[0] != 0;
  if (!r.next(tag, p) || tag != K_REQ_ALIGN) return false;
  if (!CanonicalReader::decode_u64(p, out.required_alignment)) return false;
  if (!r.next(tag, p) || tag != K_MEM_SCHEMA) return false;
  if (!CanonicalReader::decode_str(p, out.memory_binding_schema)) return false;
  if (!r.next(tag, p) || tag != K_DEP_COUNT) return false;
  { std::uint32_t n; if (!CanonicalReader::decode_u32(p, n) || n > 8192) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
      if (!r.next(tag, p) || tag != K_DEP) return false;
      KernelIdentityRef d;
      if (!decode_dep(p, d)) return false;
      out.dependencies.push_back(std::move(d));
    } }
  if (!r.next(tag, p) || tag != K_MODELREV) return false;
  if (!CanonicalReader::decode_str(p, out.model_operator_revision)) return false;
  if (!r.next(tag, p) || tag != K_POLICYGEN) return false;
  if (!CanonicalReader::decode_str(p, out.policy_generation)) return false;
  return true;
}

} // namespace

Result<GraphCompatibilityKey> GraphCompatibilityKey::build(const CompatibilityFacts& facts) {
  CanonicalWriter w;
  encode_facts(w, facts);
  GraphCompatibilityKey key;
  key.facts_ = facts;
  key.canonical_ = w.take();
  key.digest_ = Sha256::compute(key.canonical_.data(), key.canonical_.size());
  return Result<GraphCompatibilityKey>::success(std::move(key));
}

Result<GraphCompatibilityKey> GraphCompatibilityKey::from_canonical(std::span<const std::uint8_t> canonical) {
  CompatibilityFacts facts;
  if (!decode_facts(canonical, facts)) {
    return Result<GraphCompatibilityKey>::failure(
        Error(ErrorCode::PersistenceCorrupt, "canonical compatibility metadata is malformed or truncated"));
  }
  CanonicalWriter w;
  encode_facts(w, facts);
  auto reencoded = w.take();
  if (reencoded != std::vector<std::uint8_t>(canonical.begin(), canonical.end())) {
    return Result<GraphCompatibilityKey>::failure(
        Error(ErrorCode::PersistenceCorrupt, "canonical compatibility metadata does not round-trip"));
  }
  GraphCompatibilityKey key;
  key.facts_ = std::move(facts);
  key.canonical_ = std::move(reencoded);
  key.digest_ = Sha256::compute(key.canonical_.data(), key.canonical_.size());
  return Result<GraphCompatibilityKey>::success(std::move(key));
}

CompatibilityFacts facts_from_graph(const GraphDescriptor& graph) {
  CompatibilityFacts f;
  f.backend = graph.backend;
  f.runtime = graph.runtime;
  f.device = graph.device;
  f.capture_mode = graph.sync.capture_mode;
  f.stream_semantics = graph.sync.stream_semantics;
  f.topology_canonical = canonical_topology(graph.nodes, graph.edges);
  // Derive per-tensor specialization from the first kernel/memory node.
  for (const auto& n : graph.nodes) {
    if (n.datatype != Datatype::None) {
      f.datatypes.push_back(n.datatype);
      f.layouts.push_back(n.layout);
      if (!n.shape.dims.empty() || n.shape.dynamic || n.shape.max_rank) f.input_shapes.push_back(n.shape);
      if (n.scalar.specialized) f.scalar = n.scalar;
      f.required_alignment = std::max(f.required_alignment, n.required_alignment);
      f.memory_binding_schema = n.binding.memory_binding_schema;
      f.binding = n.binding;
    }
  }
  f.quantization = QuantizationConfig{};
  for (const auto& n : graph.nodes) {
    if (n.quantization.mode != QuantizationConfig::Mode::None) f.quantization = n.quantization;
  }
  f.dependencies = graph.dependencies;
  // workload identity / model revision are merged by the cache engine at capture time.
  return f;
}

const char* to_string(GraphCompatibilityClass c) noexcept {
  switch (c) {
    case GraphCompatibilityClass::ExactCompatible: return "ExactCompatible";
    case GraphCompatibilityClass::CompatibleWithRebinding: return "CompatibleWithRebinding";
    case GraphCompatibilityClass::CompatibleWithDynamicShapeConstraint: return "CompatibleWithDynamicShapeConstraint";
    case GraphCompatibilityClass::CompatibleWithRuntimeValidation: return "CompatibleWithRuntimeValidation";
    case GraphCompatibilityClass::IncompatibleBackend: return "IncompatibleBackend";
    case GraphCompatibilityClass::IncompatibleArchitecture: return "IncompatibleArchitecture";
    case GraphCompatibilityClass::IncompatibleRuntime: return "IncompatibleRuntime";
    case GraphCompatibilityClass::IncompatibleDriverGeneration: return "IncompatibleDriverGeneration";
    case GraphCompatibilityClass::IncompatibleGraphABI: return "IncompatibleGraphABI";
    case GraphCompatibilityClass::IncompatibleKernelABI: return "IncompatibleKernelABI";
    case GraphCompatibilityClass::IncompatibleTopology: return "IncompatibleTopology";
    case GraphCompatibilityClass::IncompatibleDependency: return "IncompatibleDependency";
    case GraphCompatibilityClass::IncompatibleDatatype: return "IncompatibleDatatype";
    case GraphCompatibilityClass::IncompatibleLayout: return "IncompatibleLayout";
    case GraphCompatibilityClass::IncompatibleShape: return "IncompatibleShape";
    case GraphCompatibilityClass::IncompatibleAlignment: return "IncompatibleAlignment";
    case GraphCompatibilityClass::IncompatibleCaptureMode: return "IncompatibleCaptureMode";
    case GraphCompatibilityClass::IncompatibleStreamSemantics: return "IncompatibleStreamSemantics";
    case GraphCompatibilityClass::IncompatibleMemoryBinding: return "IncompatibleMemoryBinding";
    case GraphCompatibilityClass::IncompatibleSpecialization: return "IncompatibleSpecialization";
    case GraphCompatibilityClass::IncompatibleQuantization: return "IncompatibleQuantization";
    case GraphCompatibilityClass::IncompatibleModelRevision: return "IncompatibleModelRevision";
    case GraphCompatibilityClass::IncompatibleWorkload: return "IncompatibleWorkload";
    case GraphCompatibilityClass::InvalidGraph: return "InvalidGraph";
    case GraphCompatibilityClass::StaleGraph: return "StaleGraph";
    case GraphCompatibilityClass::CorruptGraph: return "CorruptGraph";
    case GraphCompatibilityClass::PolicyRejected: return "PolicyRejected";
    case GraphCompatibilityClass::NotACandidate: return "NotACandidate";
  }
  return "NotACandidate";
}
const char* to_string(CompatibilityReasonCode c) noexcept {
  switch (c) {
    case CompatibilityReasonCode::None: return "None";
    case CompatibilityReasonCode::ExactMatch: return "ExactMatch";
    case CompatibilityReasonCode::RebindingRequiredLegal: return "RebindingRequiredLegal";
    case CompatibilityReasonCode::DynamicShapeAccepted: return "DynamicShapeAccepted";
    case CompatibilityReasonCode::BackendMismatch: return "BackendMismatch";
    case CompatibilityReasonCode::ArchitectureMismatch: return "ArchitectureMismatch";
    case CompatibilityReasonCode::RuntimeMismatch: return "RuntimeMismatch";
    case CompatibilityReasonCode::DriverGenerationMismatch: return "DriverGenerationMismatch";
    case CompatibilityReasonCode::GraphABIMismatch: return "GraphABIMismatch";
    case CompatibilityReasonCode::KernelABIMismatch: return "KernelABIMismatch";
    case CompatibilityReasonCode::TopologyMismatch: return "TopologyMismatch";
    case CompatibilityReasonCode::DependencyMismatch: return "DependencyMismatch";
    case CompatibilityReasonCode::DatatypeMismatch: return "DatatypeMismatch";
    case CompatibilityReasonCode::LayoutMismatch: return "LayoutMismatch";
    case CompatibilityReasonCode::ShapeMismatch: return "ShapeMismatch";
    case CompatibilityReasonCode::AlignmentMismatch: return "AlignmentMismatch";
    case CompatibilityReasonCode::CaptureModeMismatch: return "CaptureModeMismatch";
    case CompatibilityReasonCode::StreamSemanticsMismatch: return "StreamSemanticsMismatch";
    case CompatibilityReasonCode::MemoryBindingMismatch: return "MemoryBindingMismatch";
    case CompatibilityReasonCode::SpecializationMismatch: return "SpecializationMismatch";
    case CompatibilityReasonCode::QuantizationMismatch: return "QuantizationMismatch";
    case CompatibilityReasonCode::ModelRevisionMismatch: return "ModelRevisionMismatch";
    case CompatibilityReasonCode::WorkloadIdentityMismatch: return "WorkloadIdentityMismatch";
    case CompatibilityReasonCode::PolicyRejectedReason: return "PolicyRejectedReason";
    case CompatibilityReasonCode::GraphInvalid: return "GraphInvalid";
    case CompatibilityReasonCode::GraphStale: return "GraphStale";
    case CompatibilityReasonCode::GraphCorrupt: return "GraphCorrupt";
    case CompatibilityReasonCode::RebindingNotEligible: return "RebindingNotEligible";
  }
  return "None";
}

namespace {
void add_reason(GraphCompatibilityDecision& d, CompatibilityReasonCode c, std::string field, std::string detail) {
  d.reasons.push_back(CompatibilityReason{c, std::move(field), std::move(detail)});
}
} // namespace

GraphCompatibilityDecision decide_compatibility(const CompatibilityFacts& req,
                                                const CompatibilityFacts& cand,
                                                const GraphCompatibilityPolicy& policy) {
  GraphCompatibilityDecision d;
  // Workload identity.
  if (req.workload.logical_name != cand.workload.logical_name) {
    d.klass = GraphCompatibilityClass::IncompatibleWorkload;
    add_reason(d, CompatibilityReasonCode::WorkloadIdentityMismatch, "workload.logical_name",
               "logical workload identity differs");
    return d;
  }
  if (!policy.namespace_filter.empty() && req.workload.namespace_name != policy.namespace_filter) {
    d.klass = GraphCompatibilityClass::PolicyRejected;
    add_reason(d, CompatibilityReasonCode::PolicyRejectedReason, "namespace",
               "request namespace does not match the policy filter");
    return d;
  }
  bool hard_fail = false;
  GraphCompatibilityClass fail_class = GraphCompatibilityClass::NotACandidate;

  auto fail = [&](GraphCompatibilityClass c, CompatibilityReasonCode r, std::string field, std::string detail) {
    if (!hard_fail) { hard_fail = true; fail_class = c; }
    add_reason(d, r, std::move(field), std::move(detail));
  };

  // Hard equality domains.
  if (req.backend.kind != cand.backend.kind || req.backend.backend_name != cand.backend.backend_name ||
      (policy.require_exact_backend && req.backend.backend_version != cand.backend.backend_version)) {
    fail(GraphCompatibilityClass::IncompatibleBackend, CompatibilityReasonCode::BackendMismatch, "backend",
         "backend identity differs");
  }
  if (req.device.vendor != cand.device.vendor) {
    fail(GraphCompatibilityClass::IncompatibleArchitecture, CompatibilityReasonCode::ArchitectureMismatch,
         "device.vendor", "accelerator vendor differs");
  }
  if (req.device.architecture != cand.device.architecture ||
      (policy.require_exact_architecture &&
       (req.device.compute_major != cand.device.compute_major ||
        req.device.compute_minor != cand.device.compute_minor))) {
    fail(GraphCompatibilityClass::IncompatibleArchitecture, CompatibilityReasonCode::ArchitectureMismatch,
         "device.architecture", "architecture / compute capability differs");
  }
  if (policy.require_exact_runtime && req.runtime.runtime_version != cand.runtime.runtime_version) {
    fail(GraphCompatibilityClass::IncompatibleRuntime, CompatibilityReasonCode::RuntimeMismatch,
         "runtime.runtime_version", "runtime version differs");
  }
  if (req.runtime.driver_version != cand.runtime.driver_version) {
    fail(GraphCompatibilityClass::IncompatibleDriverGeneration, CompatibilityReasonCode::DriverGenerationMismatch,
         "runtime.driver_version", "driver generation differs");
  }
  if (policy.require_exact_abis && req.runtime.graph_abi != cand.runtime.graph_abi) {
    fail(GraphCompatibilityClass::IncompatibleGraphABI, CompatibilityReasonCode::GraphABIMismatch,
         "runtime.graph_abi", "graph ABI differs");
  }
  if (policy.require_exact_abis && req.runtime.kernel_abi != cand.runtime.kernel_abi) {
    fail(GraphCompatibilityClass::IncompatibleKernelABI, CompatibilityReasonCode::KernelABIMismatch,
         "runtime.kernel_abi", "kernel ABI differs");
  }
  if (req.capture_mode != cand.capture_mode) {
    fail(GraphCompatibilityClass::IncompatibleCaptureMode, CompatibilityReasonCode::CaptureModeMismatch,
         "sync.capture_mode", "capture mode differs");
  }
  if (req.stream_semantics != cand.stream_semantics) {
    fail(GraphCompatibilityClass::IncompatibleStreamSemantics, CompatibilityReasonCode::StreamSemanticsMismatch,
         "sync.stream_semantics", "stream semantics differ");
  }
  if (policy.require_exact_topology && req.topology_canonical != cand.topology_canonical) {
    fail(GraphCompatibilityClass::IncompatibleTopology, CompatibilityReasonCode::TopologyMismatch,
         "topology", "graph topology identity differs");
  }
  if (policy.require_exact_dependencies) {
    std::unordered_set<std::string> req_correct;
    for (const auto& dep : req.dependencies) {
      if (dep.contributes_to_correctness) {
        req_correct.insert(dep.name + "|" + std::to_string(dep.generation.value) + "|" + dep.content_digest);
      }
    }
    for (const auto& dep : cand.dependencies) {
      if (dep.contributes_to_correctness) {
        auto key = dep.name + "|" + std::to_string(dep.generation.value) + "|" + dep.content_digest;
        if (req_correct.find(key) == req_correct.end()) {
          fail(GraphCompatibilityClass::IncompatibleDependency, CompatibilityReasonCode::DependencyMismatch,
               "dependency:" + dep.name, "correctness-contributing dependency differs");
          break;
        }
      }
    }
  }
  if (req.datatypes != cand.datatypes) {
    fail(GraphCompatibilityClass::IncompatibleDatatype, CompatibilityReasonCode::DatatypeMismatch,
         "datatype", "datatype specialization differs");
  }
  if (req.layouts != cand.layouts) {
    fail(GraphCompatibilityClass::IncompatibleLayout, CompatibilityReasonCode::LayoutMismatch,
         "layout", "tensor layout differs");
  }
  if (req.scalar.specialized != cand.scalar.specialized ||
      (req.scalar.specialized && (req.scalar.int_value != cand.scalar.int_value ||
                                  req.scalar.float_value != cand.scalar.float_value))) {
    fail(GraphCompatibilityClass::IncompatibleSpecialization, CompatibilityReasonCode::SpecializationMismatch,
         "scalar_specialization", "scalar specialization differs");
  }
  if (req.quantization.mode != cand.quantization.mode ||
      req.quantization.group_size != cand.quantization.group_size ||
      req.quantization.symmetric != cand.quantization.symmetric ||
      req.quantization.custom_scheme != cand.quantization.custom_scheme) {
    fail(GraphCompatibilityClass::IncompatibleQuantization, CompatibilityReasonCode::QuantizationMismatch,
         "quantization", "quantization configuration differs");
  }

  // Soft/rebindable domain: alignment + binding.
  bool used_rebinding = false;
  if (req.required_alignment != cand.required_alignment ||
      req.binding.memory_binding_schema != cand.binding.memory_binding_schema ||
      req.binding.binding_class != cand.binding.binding_class ||
      req.binding.alignment != cand.binding.alignment) {
    if (policy.allow_rebinding && cand.binding.rebinding_eligible &&
        cand.binding.binding_class != BindingClass::RecaptureRequiredBinding &&
        cand.binding.binding_class != BindingClass::ImmutableBinding) {
      used_rebinding = true;
      add_reason(d, CompatibilityReasonCode::RebindingRequiredLegal, "binding",
                 "binding differs but is legally reboundable");
    } else if (req.required_alignment != cand.required_alignment) {
      fail(GraphCompatibilityClass::IncompatibleAlignment, CompatibilityReasonCode::AlignmentMismatch,
           "alignment", "required alignment differs and is not reboundable");
    } else {
      fail(GraphCompatibilityClass::IncompatibleMemoryBinding, CompatibilityReasonCode::MemoryBindingMismatch,
           "binding", "memory binding differs and is not legally reboundable");
    }
  }

  // Shape domain: dynamic constraint may accept.
  bool used_dynamic = false;
  bool shape_ok = true;
  if (req.input_shapes != cand.input_shapes || req.output_shapes != cand.output_shapes) {
    bool accepted_dynamic = false;
    if (policy.allow_dynamic_shape && cand.input_shapes.size() == req.input_shapes.size()) {
      bool all_ok = true;
      for (std::size_t i = 0; i < req.input_shapes.size(); ++i) {
        const auto& r = req.input_shapes[i];
        const auto& c = cand.input_shapes[i];
        if (r.dims == c.dims) continue;
        if (c.dynamic && c.max_rank >= r.dims.size()) continue;  // dynamic constraint satisfied
        all_ok = false;
        break;
      }
      if (all_ok && !req.input_shapes.empty()) {
        accepted_dynamic = true;
      }
    }
    if (!accepted_dynamic) {
      shape_ok = false;
      fail(GraphCompatibilityClass::IncompatibleShape, CompatibilityReasonCode::ShapeMismatch,
           "shape", "shape specialization differs");
    } else {
      used_dynamic = true;
      add_reason(d, CompatibilityReasonCode::DynamicShapeAccepted, "shape",
                 "shape differs but satisfies the candidate dynamic-shape constraint");
    }
  }

  // Model/operator revision.
  if (!req.model_operator_revision.empty() && !cand.model_operator_revision.empty() &&
      req.model_operator_revision != cand.model_operator_revision && !policy.allow_model_revision_lax) {
    fail(GraphCompatibilityClass::IncompatibleModelRevision, CompatibilityReasonCode::ModelRevisionMismatch,
         "model_revision", "model/operator revision differs");
  }

  if (hard_fail) {
    d.klass = fail_class;
  } else if (used_rebinding) {
    // If dynamic shape + rebinding both used, rebinding takes precedence.
    d.klass = GraphCompatibilityClass::CompatibleWithRebinding;
    if (used_dynamic) {
      add_reason(d, CompatibilityReasonCode::DynamicShapeAccepted, "shape", "dynamic shape accepted for replay");
    }
  } else if (used_dynamic) {
    d.klass = GraphCompatibilityClass::CompatibleWithDynamicShapeConstraint;
  } else {
    d.klass = GraphCompatibilityClass::ExactCompatible;
    add_reason(d, CompatibilityReasonCode::ExactMatch, "*", "every compatibility field matches exactly");
  }
  return d;
}


std::vector<std::uint8_t> facts_to_canonical(const CompatibilityFacts& facts) {
  CanonicalWriter w;
  encode_facts(w, facts);
  return w.take();
}

Result<CompatibilityFacts> facts_from_canonical(std::span<const std::uint8_t> canonical) {
  CompatibilityFacts facts;
  if (!decode_facts(canonical, facts)) {
    return Result<CompatibilityFacts>::failure(
        Error(ErrorCode::PersistenceCorrupt, "cannot decode canonical compatibility facts"));
  }
  return Result<CompatibilityFacts>::success(std::move(facts));
}

} // namespace gc