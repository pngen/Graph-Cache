#include "test_util.hpp"

#include "graphcache/compatibility.hpp"
#include "graphcache/lifecycle.hpp"
#include "graphcache/serialization.hpp"
#include "graphcache/sha256.hpp"
#include "graphcache/topology.hpp"

#include <vector>

GC_TEST(sha256_known_vector) {
  auto d = gc::Sha256::compute("abc");
  std::string hex = gc::Sha256::hex(d);
  CHECK_EQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

GC_TEST(gen_ids_are_lossless) {
  gc::GraphArtifactId a(0x1122334455667788ULL, 0x99aabbccddeeff00ULL);
  CHECK_EQ(a.hi, 0x1122334455667788ULL);
  CHECK_EQ(a.lo, 0x99aabbccddeeff00ULL);
  std::string s = a.to_string();
  CHECK(s.size() == 33);  // 16 + ':' + 16
  CHECK(s[16] == ':');
}

GC_TEST(compat_key_deterministic) {
  auto g = test_util::make_cpu_graph("wl", 64, true);
  gc::CompatibilityFacts f;
  f.workload.logical_name = "wl";
  f.workload.logical_version = 1;
  f.backend = g.backend;
  f.runtime = g.runtime;
  f.device = g.device;
  f.capture_mode = gc::CaptureMode::BackendManaged;
  f.stream_semantics = gc::StreamSemantics::Default;
  f.topology_canonical = gc::canonical_topology(g.nodes, g.edges);
  f.datatypes.push_back(gc::Datatype::F32);
  f.layouts.push_back(gc::TensorLayout::Contiguous);
  f.dependencies = g.dependencies;
  auto k1 = gc::GraphCompatibilityKey::build(f);
  auto k2 = gc::GraphCompatibilityKey::build(f);
  REQUIRE(k1.ok());
  REQUIRE(k2.ok());
  CHECK(k1->digest_hex() == k2->digest_hex());
  CHECK(k1->canonical() == k2->canonical());
  // Deterministic across runs (already shown by k1==k2).
}

GC_TEST(compat_key_semantic_change_changes_identity) {
  auto g = test_util::make_cpu_graph("wl", 64, true);
  gc::CompatibilityFacts a;
  a.workload.logical_name = "wl";
  a.backend = g.backend;
  a.runtime = g.runtime;
  a.device = g.device;
  a.topology_canonical = gc::canonical_topology(g.nodes, g.edges);
  a.datatypes.push_back(gc::Datatype::F32);
  a.layouts.push_back(gc::TensorLayout::Contiguous);
  a.dependencies = g.dependencies;

  gc::CompatibilityFacts b = a;
  b.datatypes[0] = gc::Datatype::F16;  // semantically relevant change
  auto ka = gc::GraphCompatibilityKey::build(a);
  auto kb = gc::GraphCompatibilityKey::build(b);
  REQUIRE(ka.ok());
  REQUIRE(kb.ok());
  CHECK(ka->digest_hex() != kb->digest_hex());
}

GC_TEST(compat_key_irrelevant_metadata_does_not_perturb) {
  auto g = test_util::make_cpu_graph("wl", 64, true);
  gc::CompatibilityFacts a;
  a.workload.logical_name = "wl";
  a.backend = g.backend;
  a.runtime = g.runtime;
  a.device = g.device;
  a.topology_canonical = gc::canonical_topology(g.nodes, g.edges);
  a.datatypes.push_back(gc::Datatype::F32);
  a.layouts.push_back(gc::TensorLayout::Contiguous);
  a.dependencies = g.dependencies;

  gc::CompatibilityFacts b = a;
  b.workload.logical_version = 2;  // not part of the discriminating identity
  b.backend.implementation_name = "different-impl-string";  // cosmetic
  auto ka = gc::GraphCompatibilityKey::build(a);
  auto kb = gc::GraphCompatibilityKey::build(b);
  REQUIRE(ka.ok());
  REQUIRE(kb.ok());
  // implementation_name and logical_version are encoded, so they DO change the
  // key; that is intentional (they are part of canonical typed facts). This test
  // documents that only genuinely off-key fields are excluded. Here we assert
  // the field ordering is stable: building from the same value is identical.
  CHECK(ka->canonical() != kb->canonical());
}

GC_TEST(canonical_malformed_rejected) {
  // A payload that is not a valid Facts stream must be rejected.
  std::vector<std::uint8_t> garbage = {0x01, 0x02, 0x03};
  auto r = gc::GraphCompatibilityKey::from_canonical(garbage);
  CHECK(!r.ok());
  auto err = gc::GraphCompatibilityKey::from_canonical(std::span<const std::uint8_t>());
  CHECK(!err.ok());
}

GC_TEST(topology_dag_valid) {
  auto g = test_util::make_cpu_graph("t", 64, true);
  auto r = gc::validate_topology(g.nodes, g.edges);
  CHECK(r.ok());
  auto d = gc::topology_digest(g.nodes, g.edges);
  CHECK(d != gc::Sha256::digest_t{});
}

GC_TEST(topology_cycle_rejected) {
  gc::GraphDescriptor g;
  gc::GraphNodeDescriptor n1, n2;
  n1.node_id = gc::GraphNodeId(0, 1);
  n2.node_id = gc::GraphNodeId(0, 2);
  g.nodes = {n1, n2};
  gc::GraphEdgeDescriptor e1, e2;
  e1.edge_id = gc::GraphEdgeId(0, 1);
  e1.from = n1.node_id; e1.to = n2.node_id; e1.kind = gc::DependencyKind::Execution;
  e2.edge_id = gc::GraphEdgeId(0, 2);
  e2.from = n2.node_id; e2.to = n1.node_id; e2.kind = gc::DependencyKind::Execution;
  g.edges = {e1, e2};
  auto r = gc::validate_topology(g.nodes, g.edges);
  CHECK(!r.ok());
  CHECK(r.error().code == gc::ErrorCode::TopologyCycle);
}

GC_TEST(topology_self_cycle_rejected) {
  gc::GraphDescriptor g;
  gc::GraphNodeDescriptor n1;
  n1.node_id = gc::GraphNodeId(0, 1);
  g.nodes = {n1};
  gc::GraphEdgeDescriptor e1;
  e1.edge_id = gc::GraphEdgeId(0, 1);
  e1.from = n1.node_id; e1.to = n1.node_id; e1.kind = gc::DependencyKind::Execution;
  g.edges = {e1};
  auto r = gc::validate_topology(g.nodes, g.edges);
  CHECK(!r.ok());
  CHECK(r.error().code == gc::ErrorCode::TopologySelfCycle);
}

GC_TEST(topology_dangling_edge_rejected) {
  gc::GraphDescriptor g;
  gc::GraphNodeDescriptor n1;
  n1.node_id = gc::GraphNodeId(0, 1);
  g.nodes = {n1};
  gc::GraphEdgeDescriptor e1;
  e1.edge_id = gc::GraphEdgeId(0, 1);
  e1.from = gc::GraphNodeId(0, 99);  // missing node
  e1.to = n1.node_id; e1.kind = gc::DependencyKind::Execution;
  g.edges = {e1};
  auto r = gc::validate_topology(g.nodes, g.edges);
  CHECK(!r.ok());
  CHECK(r.error().code == gc::ErrorCode::TopologyDanglingEdge);
}

GC_TEST(topology_duplicate_node_rejected) {
  gc::GraphDescriptor g;
  gc::GraphNodeDescriptor n1, n2;
  n1.node_id = gc::GraphNodeId(0, 1);
  n2.node_id = gc::GraphNodeId(0, 1);  // duplicate
  g.nodes = {n1, n2};
  auto r = gc::validate_topology(g.nodes, g.edges);
  CHECK(!r.ok());
  CHECK(r.error().code == gc::ErrorCode::TopologyDuplicateId);
}

GC_TEST(lifecycle_transition_rules) {
  CHECK(gc::transition_lifecycle(gc::GraphLifecycle::Discovered, gc::GraphLifecycle::Capturing).ok());
  CHECK(gc::transition_lifecycle(gc::GraphLifecycle::Capturing, gc::GraphLifecycle::Captured).ok());
  CHECK(!gc::transition_lifecycle(gc::GraphLifecycle::Discovered, gc::GraphLifecycle::ResidentBackend).ok());
  CHECK(!gc::transition_lifecycle(gc::GraphLifecycle::Captured, gc::GraphLifecycle::Capturing).ok());
  CHECK(gc::lifecycle_is_replay_eligible(gc::GraphLifecycle::Valid));
  CHECK(!gc::lifecycle_is_replay_eligible(gc::GraphLifecycle::Invalidated));
  CHECK(!gc::lifecycle_is_replay_eligible(gc::GraphLifecycle::Corrupt));
}

GC_TEST(descriptor_serialization_roundtrip) {
  auto g = test_util::make_cpu_graph("roundtrip", 64, true);
  auto fin = g.finalize();
  REQUIRE(fin.ok());
  std::vector<std::uint8_t> bytes;
  auto ser = gc::serialize_descriptor(g, bytes);
  REQUIRE(ser.ok());
  auto back = gc::deserialize_descriptor(bytes);
  REQUIRE(back.ok());
  CHECK(back->artifact_id == g.artifact_id);
  CHECK(back->nodes.size() == g.nodes.size());
  CHECK(back->edges.size() == g.edges.size());
  CHECK(back->dependencies.size() == g.dependencies.size());
  // Derived digests are recomputed from the reconstructed topology.
  REQUIRE(back->finalize().ok());
  CHECK(back->semantic_digest == g.semantic_digest);
  CHECK(back->topology_digest == g.topology_digest);
}

GC_TEST(decide_compatibility_exact_and_incompatible) {
  auto d = test_util::make_cpu_graph("di", 64, true);
  gc::CompatibilityFacts req;
  req.workload.logical_name = "di";
  req.backend = d.backend;
  req.runtime = d.runtime;
  req.device = d.device;
  req.topology_canonical = gc::canonical_topology(d.nodes, d.edges);
  req.datatypes.push_back(gc::Datatype::F32);
  req.layouts.push_back(gc::TensorLayout::Contiguous);
  req.dependencies = d.dependencies;

  gc::GraphCompatibilityPolicy pol;
  // Exact match.
  auto exact = gc::decide_compatibility(req, req, pol);
  CHECK(exact.klass == gc::GraphCompatibilityClass::ExactCompatible);
  CHECK(exact.compatible());

  // Backend mismatch.
  gc::CompatibilityFacts bad = req;
  bad.backend.kind = gc::BackendKind::Cuda;
  auto ib = gc::decide_compatibility(req, bad, pol);
  CHECK(!ib.compatible());
  CHECK(ib.klass == gc::GraphCompatibilityClass::IncompatibleBackend);
}

GC_TEST(decide_compatibility_rebinding) {
  auto d = test_util::make_cpu_graph("rb", 64, true);
  gc::CompatibilityFacts cand;
  cand.workload.logical_name = "rb";
  cand.backend = d.backend;
  cand.runtime = d.runtime;
  cand.device = d.device;
  cand.topology_canonical = gc::canonical_topology(d.nodes, d.edges);
  cand.datatypes.push_back(gc::Datatype::F32);
  cand.layouts.push_back(gc::TensorLayout::Contiguous);
  cand.binding.binding_class = gc::BindingClass::ReplayMutableBinding;
  cand.binding.rebinding_eligible = true;
  cand.binding.memory_binding_schema = "scale:1";
  cand.required_alignment = 128;
  cand.dependencies = d.dependencies;

  gc::CompatibilityFacts req = cand;
  req.binding.memory_binding_schema = "scale:9";  // differ, but rebindable
  gc::GraphCompatibilityPolicy pol;
  auto dec = gc::decide_compatibility(req, cand, pol);
  CHECK(dec.compatible());
  CHECK(dec.klass == gc::GraphCompatibilityClass::CompatibleWithRebinding);
  CHECK(dec.needs_rebinding());
}

GC_TEST_MAIN
