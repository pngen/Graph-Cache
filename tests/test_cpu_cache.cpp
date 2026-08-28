#include "test_util.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kFloats = 16;
constexpr std::uint64_t kBytes = kFloats * sizeof(float);

std::vector<float> reference_output(const std::vector<float>& in) {
  std::vector<float> out(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) out[i] = 2.0f * in[i];
  return out;
}
gc::Sha256::digest_t digest_of(const float* p, std::size_t n) {
  return gc::Sha256::compute(p, n * sizeof(float));
}
} // namespace

GC_TEST(cpu_capture_then_exact_hit_and_replay) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);

  auto req = test_util::make_cpu_request("cov", kBytes, true);

  // First lookup: miss -> capture -> hit.
  auto r1 = cache.lookup(req);
  CHECK(r1.hit());
  CHECK(r1.capture_started);
  CHECK(r1.lease != nullptr);
  CHECK(r1.outcome == gc::LookupOutcome::ExactHostHit || r1.outcome == gc::LookupOutcome::ExactBackendResidentHit);

  // Replay and verify correctness against an independent reference.
  std::vector<float> in(kFloats);
  for (std::size_t i = 0; i < kFloats; ++i) in[i] = static_cast<float>(i) * 0.25f - 1.5f;
  std::vector<float> out(kFloats, 0.0f);
  auto expect = reference_output(in);

  gc::ReplayBuffers bufs;
  bufs.inputs = {in.data()};
  bufs.outputs = {out.data()};
  bufs.input_bytes = {kBytes};
  bufs.output_bytes = {kBytes};

  gc::GraphReplayRequest rr;
  rr.lease = r1.lease;
  rr.buffers = bufs;
  rr.descriptor.replay_attempt = gc::ReplayAttemptId(1);
  rr.expected_generation = r1.generation;

  auto replay = cache.replay(rr);
  REQUIRE(replay.ok());
  CHECK(replay->ok);
  CHECK(replay->replayed_nodes >= 1);
  // Output must equal reference.
  for (std::size_t i = 0; i < kFloats; ++i) {
    if (std::fabs(out[i] - expect[i]) > 1e-5f) {
      CHECK(out[i] == expect[i]);
    }
  }
}

GC_TEST(cpu_exact_second_hit) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("hit2", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  auto r2 = cache.lookup(req);
  REQUIRE(r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::ExactHostHit);
  CHECK(r1.artifact_id == r2.artifact_id);
  CHECK(r1.generation == r2.generation);
  auto m = cache.metrics();
  CHECK(m.captures == 1);
  CHECK(m.lookups >= 2);
}

GC_TEST(cpu_incompatible_request_is_miss) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("incompat", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());

  // A request with a different datatype must be a miss (incompatible).
  auto req2 = req;
  req2.descriptor.nodes[1].datatype = gc::Datatype::F16;
  // Rebuild facts via request (the nodes drive datatype).
  req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  CHECK(!r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::MissIncompatible);
}

GC_TEST(cpu_single_flight_concurrent_miss) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);

  constexpr int kThreads = 8;
  std::atomic<int> hits{0};
  std::atomic<int> misses{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      auto req = test_util::make_cpu_request("sflight", kBytes, true);
      auto r = cache.lookup(req);
      if (r.hit()) hits.fetch_add(1); else misses.fetch_add(1);
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(hits.load(), kThreads);
  CHECK_EQ(misses.load(), 0);
  auto m = cache.metrics();
  CHECK_EQ(m.captures, 1);   // exactly one capture for N concurrent misses
  CHECK_EQ(m.deduplicated_captures, 0);  // only one capture happened; no second capture
}

GC_TEST(cpu_invalidation_blocks_new_leases_but_drains_existing) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("inval", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());

  gc::InvalidationRequest inv;
  inv.scope = gc::InvalidationScope::ByWorkload;
  inv.workload.logical_name = "inval";
  auto res = cache.invalidate(inv);
  CHECK(res.invalidated >= 1);

  // New lease must not be granted after invalidation.
  req.allow_capture = false;   // only detect whether the invalidated graph is still usable
  auto r2 = cache.lookup(req);
  CHECK(!r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::MissInvalidated || r2.outcome == gc::LookupOutcome::MissCaptureRequired);

  // Existing lease remains replayable (drain semantics).
  std::vector<float> in(kFloats, 1.0f), out(kFloats, 0.0f);
  gc::ReplayBuffers bufs;
  bufs.inputs = {in.data()};
  bufs.outputs = {out.data()};
  bufs.input_bytes = {kBytes};
  bufs.output_bytes = {kBytes};
  gc::GraphReplayRequest rr;
  rr.lease = r1.lease;
  rr.buffers = bufs;
  rr.descriptor.replay_attempt = gc::ReplayAttemptId(2);
  rr.expected_generation = r1.generation;
  auto replay = cache.replay(rr);
  REQUIRE(replay.ok());
  for (std::size_t i = 0; i < kFloats; ++i) CHECK(std::fabs(out[i] - 2.0f) < 1e-5f);
}

GC_TEST(cpu_lease_release_idempotent) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("lease", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  auto lease = r1.lease;
  auto m0 = cache.metrics();
  CHECK(m0.active_leases >= 1);
  lease->release();
  lease->release();  // duplicate release must not underflow
  auto m1 = cache.metrics();
  CHECK(m1.active_leases <= m0.active_leases);
}

GC_TEST(cpu_evict_and_reload) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("evict", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  // release then evict
  r1.lease->release();
  auto ev = cache.evict(r1.artifact_id);
  CHECK(ev.ok());
  // reload (a fresh lookup recaptures if not present)
  auto r2 = cache.lookup(req);
  CHECK(r2.hit());
}

GC_TEST(cpu_pin_prevents_eviction) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("pin", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  auto pk = cache.pin(r1.artifact_id);
  CHECK(pk.ok());
  r1.lease->release();
  auto ev = cache.evict(r1.artifact_id);
  CHECK(!ev.ok());  // pinned cannot be evicted
  auto up = cache.unpin(r1.artifact_id);
  CHECK(up.ok());
}

GC_TEST(cpu_explain_and_stats) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  auto req = test_util::make_cpu_request("explain", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  auto ex = cache.explain(r1);
  CHECK(!ex.text.empty());
  CHECK(!ex.json.empty());
  auto snap = cache.snapshot();
  CHECK(snap.graphs_total >= 1);
}

GC_TEST_MAIN
