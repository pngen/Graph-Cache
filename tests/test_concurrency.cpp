#include "test_util.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

GC_TEST(concurrency_single_flight_contention) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  cfg.concurrency_hint = 16;
  gc::GraphCache cache(cfg);

  constexpr int kThreads = 16;
  constexpr int kIters = 200;
  std::atomic<int> hits{0};
  std::atomic<int> misses{0};
  std::atomic<std::uint64_t> replays{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kIters; ++i) {
        auto req = test_util::make_cpu_request("contend", 64, true);
        auto r = cache.lookup(req);
        if (r.hit()) {
          hits.fetch_add(1);
          std::vector<float> in(16, 1.0f), out(16, 0.0f);
          gc::ReplayBuffers bufs;
          bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
          bufs.input_bytes = {64}; bufs.output_bytes = {64};
          gc::GraphReplayRequest rr;
          rr.lease = r.lease;
          rr.buffers = bufs;
          rr.descriptor.replay_attempt = gc::ReplayAttemptId(static_cast<std::uint64_t>(i + 1));
          rr.expected_generation = r.generation;
          if (cache.replay(rr).ok()) replays.fetch_add(1);
          r.lease->release();
        } else {
          misses.fetch_add(1);
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(hits.load(), kThreads * kIters);
  CHECK_EQ(misses.load(), 0);
  CHECK_EQ(replays.load(), static_cast<std::uint64_t>(kThreads * kIters));
  auto m = cache.metrics();
  CHECK_EQ(m.captures, 1);          // one capture for all concurrent lookups
  CHECK_EQ(m.active_leases, 0);     // all released
  CHECK_EQ(m.active_replays, 0);
}

GC_TEST(concurrency_mixed_operations) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);

  constexpr int kThreads = 8;
  constexpr int kIters = 150;
  std::atomic<bool> stop{false};
  std::vector<std::thread> threads;

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kIters; ++i) {
        std::string name = "wl" + std::to_string((t + i) % 4);  // 4 distinct workloads
        auto req = test_util::make_cpu_request(name, 64, true);
        auto r = cache.lookup(req);
        if (r.hit()) {
          std::vector<float> in(16, static_cast<float>(i % 7)), out(16, 0.0f);
          gc::ReplayBuffers bufs;
          bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
          bufs.input_bytes = {64}; bufs.output_bytes = {64};
          gc::GraphReplayRequest rr;
          rr.lease = r.lease;
          rr.buffers = bufs;
          rr.descriptor.replay_attempt = gc::ReplayAttemptId(static_cast<std::uint64_t>(i + 1));
          rr.expected_generation = r.generation;
          [[maybe_unused]] auto _rep = cache.replay(rr);
          r.lease->release();
        }
        if (i % 37 == 0) {
          // Sporadic snapshot/stats/explain.
          (void)cache.metrics();
          (void)cache.snapshot();
          (void)cache.explain(r);
        }
        if (i % 53 == 0 && !stop.load()) {
          gc::InvalidationRequest inv;
          inv.scope = gc::InvalidationScope::ByWorkload;
          inv.workload.logical_name = name;
          [[maybe_unused]] auto _inv = cache.invalidate(inv);
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  auto m = cache.metrics();
  CHECK(m.invalidations >= 0);
  CHECK_EQ(m.active_leases, 0);
  CHECK_EQ(m.active_replays, 0);
  CHECK_EQ(m.active_captures, 0);
}

GC_TEST(concurrency_lease_release_storm_no_underflow) {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  gc::GraphCache cache(cfg);
  // Acquire many leases in parallel and release each twice (idempotency).
  std::vector<std::shared_ptr<gc::GraphLease>> leases;
  std::mutex mtx;
  constexpr int kThreads = 8;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 50; ++i) {
        auto req = test_util::make_cpu_request("storm", 64, true);
        auto r = cache.lookup(req);
        if (r.hit()) {
          {
            std::lock_guard<std::mutex> lk(mtx);
            leases.push_back(r.lease);
          }
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  // Release all, twice each.
  for (auto& l : leases) { l->release(); l->release(); }
  auto m = cache.metrics();
  CHECK_EQ(m.active_leases, 0);
}

GC_TEST_MAIN
