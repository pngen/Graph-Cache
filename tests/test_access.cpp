#include "test_util.hpp"

#include <filesystem>
#include <process.h>

// Compile/link exercise of the full public API surface.
GC_TEST(public_api_surface) {
  // Identity types.
  gc::GraphId gid(0x1, 0x2);
  gc::GraphArtifactId aid(0x3, 0x4);
  gc::GraphGeneration gen(5);
  gc::CacheGeneration cgen(6);
  gc::CaptureAttemptId cap(7);
  gc::ReplayAttemptId rap(8);
  gc::LoadGeneration lg(9);
  gc::ResidencyGeneration rg(10);
  gc::CoordinatorEpoch epoch(11);
  gc::WorkerId wid(12);
  gc::WorkerBootId wb(13);
  gc::GraphNodeId nid(0x10, 0x11);
  gc::GraphEdgeId eid(0x12, 0x13);
  CHECK(gid.valid() && aid.valid() && gen.valid() && cgen.valid());

  // Result<T> / Result<void> / ErrorCode.
  gc::Result<int> r = gc::Result<int>::success(42);
  CHECK(r.ok() && r.value() == 42);
  gc::Result<void> rv = gc::Result<void>::failure(gc::Error(gc::ErrorCode::InvalidArgument, "x"));
  CHECK(!rv.ok());
  CHECK(rv.error().code == gc::ErrorCode::InvalidArgument);
  const char* ec = gc::to_string(gc::ErrorCode::StaleEpoch);
  CHECK(ec[0] != 0);

  // Descriptors.
  gc::DeviceDescriptor dev;
  dev.vendor = gc::AcceleratorVendor::Nvidia;
  dev.architecture = "blackwell";
  dev.compute_major = 12; dev.compute_minor = 0;
  CHECK(dev.compute_capability() == "12.0");
  gc::RuntimeDescriptor rt;
  rt.graph_abi = 1; rt.kernel_abi = 1;
  gc::ShapeDescriptor shape;
  shape.dims = {16};

  gc::GraphNodeDescriptor node;
  gc::GraphEdgeDescriptor edge;
  gc::GraphDescriptor desc;
  desc.backend.kind = gc::BackendKind::Cpu;
  desc.backend.backend_name = "cpu";

  gc::CompatibilityFacts facts;
  facts.workload.logical_name = "wl";
  facts.backend = desc.backend;
  facts.runtime = rt;
  facts.device = dev;
  facts.topology_canonical = gc::canonical_topology(desc.nodes, desc.edges);
  auto key = gc::GraphCompatibilityKey::build(facts);
  CHECK(key.ok());
  CHECK(key->digest_hex().size() == 64);
  gc::GraphCompatibilityPolicy policy;
  auto decision = gc::decide_compatibility(facts, facts, policy);
  CHECK(decision.compatible() || decision.klass == gc::GraphCompatibilityClass::ExactCompatible);

  // PersistenceStore.
  auto tmp = std::filesystem::temp_directory_path() / ("gc-access-" + std::to_string(::_getpid()));
  std::filesystem::create_directories(tmp);
  gc::PersistenceStore store(tmp.string());
  std::vector<std::uint8_t> blob = {1, 2, 3, 4};
  CHECK(store.put(aid, blob).ok());
  auto got = store.get(aid);
  CHECK(got.ok() && got.value() == blob);
  CHECK(store.remove(aid).ok());
  std::filesystem::remove_all(tmp);

  // Residency / eviction policy.
  gc::ResidencyPolicy rp; gc::EvictionPolicy ep;
  CHECK(rp.max_backend_resident > 0);
  CHECK(ep.lru);

  // Config + cache.
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  gc::GraphLookupRequest req = test_util::make_cpu_request("access", 64, true);
  auto res = cache.lookup(req);
  CHECK(res.hit());

  // Observability.
  gc::Metrics m = cache.metrics();
  gc::Snapshot s = cache.snapshot();
  gc::Explain ex = cache.explain(res);
  std::vector<gc::Event> ev = cache.events();
  CHECK(s.graphs_total >= 1);
  CHECK(!ex.json.empty());
  CHECK(ev.size() >= 1);

  // Reservation type.
  gc::GraphReservation reserve;
  reserve.artifact_id = aid;
  reserve.generation = gen;
  reserve.attempt = cap;
  CHECK(reserve.valid());
}

GC_TEST_MAIN
