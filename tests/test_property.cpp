#include "test_util.hpp"

#include <cstdint>

namespace {
// Deterministic xorshift64 PRNG.
struct Rng {
  std::uint64_t s;
  explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  std::uint64_t next() {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
  }
  std::uint32_t range(std::uint32_t n) { return static_cast<std::uint32_t>(next() % n); }
};
} // namespace

GC_TEST(property_fixed_seed_invariants) {
  constexpr std::uint64_t kSeed = 0xC0FFEE123456789ULL;
  std::printf("gc_test: property seed = 0x%016llx\n", kSeed);
  Rng rng(kSeed);
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  cfg.concurrency_hint = 16;
  gc::GraphCache cache(cfg);

  std::uint64_t ops = 0;
  const std::uint32_t kOps = 3000;
  constexpr int kWl = 5;
  for (std::uint32_t i = 0; i < kOps; ++i) {
    ++ops;
    std::uint32_t name_idx = rng.range(kWl);
    bool rebindable = (rng.range(2) == 0);
    bool alternate_dtype = (rng.range(3) == 0);
    auto req = test_util::make_cpu_request("P" + std::to_string(name_idx), 64, rebindable);
    if (alternate_dtype) req.descriptor.nodes[1].datatype = gc::Datatype::F16;

    auto r = cache.lookup(req);
    if (r.hit()) {
      // Invariant: a hit means the workload was seen before with matching facts.
      CHECK(r.lease != nullptr);
      // Replay and verify reference correctness.
      std::vector<float> in(16, static_cast<float>(rng.range(9))), out(16, 0.0f);
      gc::ReplayBuffers bufs;
      bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
      bufs.input_bytes = {64}; bufs.output_bytes = {64};
      gc::GraphReplayRequest rr;
      rr.lease = r.lease;
      rr.buffers = bufs;
      rr.descriptor.replay_attempt = gc::ReplayAttemptId(i + 1);
      rr.expected_generation = r.generation;
      auto replay = cache.replay(rr);
      if (replay.ok() && !alternate_dtype) {
        for (int k = 0; k < 16; ++k) {
          if (std::fabs(out[k] - 2.0f * in[k]) > 1e-4f) CHECK(out[k] == 2.0f * in[k]);
        }
      }
      r.lease->release();
    } else {
      // Misses are legitimate for an unseen workload or after invalidation.
      CHECK(!r.hit());
    }
    if (i % 500 == 0) {
      gc::InvalidationRequest inv;
      inv.scope = gc::InvalidationScope::ByWorkload;
      inv.workload.logical_name = "P" + std::to_string(rng.range(kWl));
      [[maybe_unused]] auto _inv = cache.invalidate(inv);
    }
  }
  auto m = cache.metrics();
  CHECK_EQ(m.active_leases, 0);
  CHECK_EQ(m.active_replays, 0);
  CHECK_EQ(m.active_captures, 0);
  CHECK(m.captures >= 1);
  std::printf("gc_test: property operations = %llu, captures = %llu, lookups = %llu\n",
              static_cast<unsigned long long>(ops),
              static_cast<unsigned long long>(m.captures),
              static_cast<unsigned long long>(m.lookups));
}

GC_TEST_MAIN
