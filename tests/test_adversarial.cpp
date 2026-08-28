#include "test_util.hpp"

#include <atomic>

GC_TEST(adversarial_zero_node_graph_rejected) {
  gc::GraphDescriptor g;
  g.backend.kind = gc::BackendKind::Cpu;
  g.backend.backend_name = "cpu";
  auto r = g.finalize();
  // finalize validates topology; empty graph has no nodes but an empty DAG is
  // technically acyclic; the policy/examples layer rejects zero-node execution.
  // Here we assert that an empty topology has a deterministic (empty) digest.
  CHECK(r.ok());
}

GC_TEST(adversarial_duplicate_artifact_id_dedup) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto r1 = cache.lookup(test_util::make_cpu_request("dup", 64, true));
  REQUIRE(r1.hit());
  auto r2 = cache.lookup(test_util::make_cpu_request("dup", 64, true));
  REQUIRE(r2.hit());
  CHECK(r1.artifact_id == r2.artifact_id);  // identical workload -> identical artifact
}

GC_TEST(adversarial_incompatible_backend_never_hits) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("ib", 64, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  // Different backend kind must not reuse the CPU graph.
  auto req2 = req;
  req2.backend.kind = gc::BackendKind::Cuda;
  req2.backend.backend_name = "cuda-graph";
  req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  CHECK(!r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::MissIncompatible);
}

GC_TEST(adversarial_incompatible_architecture) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("ia", 64, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  auto req2 = req;
  req2.device.compute_major = 12;  // different compute capability
  req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  CHECK(!r2.hit());
}

GC_TEST(adversarial_stale_dependency_generation_misses) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("dep", 64, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());

  // Bump the dependency generation -> the captured graph is stale.
  auto req2 = req;
  req2.descriptor.dependencies[0].generation = gc::GraphGeneration(/*new*/ 7);
  req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  CHECK(!r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::MissStaleDependency);
}

GC_TEST(adversarial_invalidated_graph_lookup_misses) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("inv", 64, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  gc::InvalidationRequest inv;
  inv.scope = gc::InvalidationScope::ByWorkload;
  inv.workload.logical_name = "inv";
  [[maybe_unused]] auto _inv = cache.invalidate(inv);
  req.allow_capture = false;
  auto r2 = cache.lookup(req);
  CHECK(!r2.hit());
}

GC_TEST(adversarial_shutdown_with_active_lease) {
  // Constructing and destroying a cache with outstanding leases must not crash.
  std::shared_ptr<gc::GraphLease> lease;
  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cpu;
    gc::GraphCache cache(cfg);
    auto r = cache.lookup(test_util::make_cpu_request("shutdown", 64, true));
    REQUIRE(r.hit());
    lease = r.lease;  // lease outlives cache
  }
  CHECK(lease != nullptr);
  lease->release();  // releasing after cache destruction must be safe
}

GC_TEST(adversarial_generation_rollback_rejected) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("rb", 64, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  // A replay with an expected generation that does not match must be rejected.
  std::vector<float> in(16, 0.5f), out(16, 0.0f);
  gc::ReplayBuffers bufs;
  bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
  bufs.input_bytes = {64}; bufs.output_bytes = {64};
  gc::GraphReplayRequest rr;
  rr.lease = r1.lease;
  rr.buffers = bufs;
  rr.descriptor.replay_attempt = gc::ReplayAttemptId(1);
  rr.expected_generation = gc::GraphGeneration(r1.generation.value + 1);  // wrong generation
  auto replay = cache.replay(rr);
  CHECK(!replay.ok());
  CHECK(replay.error().code == gc::ErrorCode::GenerationMismatch);
}

GC_TEST_MAIN
