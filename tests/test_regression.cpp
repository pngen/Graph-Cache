// Regression scenarios for eviction/unload/invalidation residency, strong
// revocation, recovery, persisted invalidation, replay buffer bounds, pin
// accounting and concurrent invalidation. Pass a test name to run only it.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <process.h>
#include <string>
#include <thread>
#include <vector>

#ifdef GC_HAS_CUDA
#include <cuda_runtime.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kBytes = 64;

struct TempDir {
  fs::path path;
  TempDir() {
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("gc-regress-" + std::to_string(stamp) + "-" + std::to_string(::_getpid()));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

gc::GraphCacheConfig cpu_cfg() {
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  return cfg;
}

gc::GraphCacheConfig persistent_cfg(const fs::path& dir) {
  auto cfg = cpu_cfg();
  cfg.enable_persistence = true;
  cfg.persistence_dir = dir.string();
  return cfg;
}

// Graphs holding a live executable (host- or backend-resident).
std::uint64_t resident_graphs(const gc::GraphCache& cache) {
  auto s = cache.snapshot();
  return s.graphs_by_residency[static_cast<int>(gc::ResidencyTier::HostResident)] +
         s.graphs_by_residency[static_cast<int>(gc::ResidencyTier::BackendResident)];
}

gc::InvalidationRequest by_artifact(const gc::GraphArtifactId& id, bool strong = false) {
  gc::InvalidationRequest inv;
  inv.scope = gc::InvalidationScope::ByArtifactId;
  inv.artifact_id = id;
  inv.strong = strong;
  return inv;
}

// Replays over 256-byte allocations while declaring the given buffer sizes, so
// a backend that ignores the declared sizes is detected without overrunning
// the test's own memory.
gc::Result<gc::GraphReplayResult> replay_declared(gc::GraphCache& cache, const gc::GraphLookupResult& r,
                                                  std::size_t declared_in, std::size_t declared_out) {
  std::vector<float> in(64, 1.0f), out(64, 0.0f);
  gc::ReplayBuffers bufs;
  bufs.inputs = {in.data()};
  bufs.outputs = {out.data()};
  bufs.input_bytes = {declared_in};
  bufs.output_bytes = {declared_out};
  gc::GraphReplayRequest rr;
  rr.lease = r.lease;
  rr.buffers = bufs;
  rr.descriptor.replay_attempt = gc::ReplayAttemptId(1);
  rr.expected_generation = r.generation;
  return cache.replay(rr);
}

}  // namespace

// Eviction releases the executable; the next lookup reloads the same artifact.
GC_TEST(evict_releases_executable) {
  gc::GraphCache cache(cpu_cfg());
  auto req = test_util::make_cpu_request("reg_evict", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  r1.lease->release();
  CHECK_EQ(resident_graphs(cache), 1u);
  REQUIRE(cache.evict(r1.artifact_id).ok());
  CHECK_EQ(resident_graphs(cache), 0u);
  auto r2 = cache.lookup(req);
  REQUIRE(r2.hit());
  CHECK(r2.artifact_id == r1.artifact_id);
  CHECK_EQ(cache.metrics().recaptures, 1u);
  r2.lease->release();
}

// unload() releases the executable too.
GC_TEST(unload_releases_executable) {
  gc::GraphCache cache(cpu_cfg());
  auto req = test_util::make_cpu_request("reg_unload", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  CHECK(!cache.unload(r1.artifact_id).ok());   // leased
  r1.lease->release();
  REQUIRE(cache.unload(r1.artifact_id).ok());
  CHECK_EQ(resident_graphs(cache), 0u);
}

// Invalidation retires the executable of an unleased graph; a leased graph
// keeps it for the draining lease.
GC_TEST(invalidation_releases_executable) {
  gc::GraphCache cache(cpu_cfg());
  auto r1 = cache.lookup(test_util::make_cpu_request("reg_inval_a", kBytes, true));
  REQUIRE(r1.hit());
  r1.lease->release();
  auto res = cache.invalidate(by_artifact(r1.artifact_id));
  CHECK_EQ(res.invalidated, 1u);
  CHECK_EQ(resident_graphs(cache), 0u);

  auto r2 = cache.lookup(test_util::make_cpu_request("reg_inval_b", kBytes, true));
  REQUIRE(r2.hit());
  auto res2 = cache.invalidate(by_artifact(r2.artifact_id));
  CHECK_EQ(res2.still_leasing, 1u);
  CHECK_EQ(resident_graphs(cache), 1u);
  CHECK(replay_declared(cache, r2, kBytes, kBytes).ok());   // drain semantics
  r2.lease->release();
}

// Strong invalidation revokes leases that were already granted.
GC_TEST(strong_invalidation_revokes_leases) {
  gc::GraphCache cache(cpu_cfg());
  auto r = cache.lookup(test_util::make_cpu_request("reg_strong", kBytes, true));
  REQUIRE(r.hit());
  REQUIRE(replay_declared(cache, r, kBytes, kBytes).ok());
  auto res = cache.invalidate(by_artifact(r.artifact_id, /*strong=*/true));
  CHECK_EQ(res.still_leasing, 1u);
  auto rep = replay_declared(cache, r, kBytes, kBytes);
  CHECK(!rep.ok());
  if (!rep.ok()) CHECK(rep.error().code == gc::ErrorCode::Invalidated);
  r.lease->release();
}

// recover() on a live cache never replaces (or duplicates) a live entry, so an
// invalidation afterwards still reaches the graph lookups would serve.
GC_TEST(recover_keeps_live_entry) {
  TempDir tmp;
  gc::GraphCache cache(persistent_cfg(tmp.path));
  auto req = test_util::make_cpu_request("reg_recover_live", kBytes, true);
  auto r1 = cache.lookup(req);
  REQUIRE(r1.hit());
  r1.lease->release();
  REQUIRE(cache.recover().ok());
  CHECK_EQ(cache.graph_count(), 1u);
  auto res = cache.invalidate(by_artifact(r1.artifact_id));
  CHECK_EQ(res.invalidated, 1u);
  req.allow_capture = false;
  auto r2 = cache.lookup(req);
  CHECK(!r2.hit());
}

// An invalidation is persisted: a restarted cache does not serve the graph.
GC_TEST(invalidation_survives_restart) {
  TempDir tmp;
  auto req = test_util::make_cpu_request("reg_restart_inval", kBytes, true);
  {
    gc::GraphCache cache(persistent_cfg(tmp.path));
    auto r = cache.lookup(req);
    REQUIRE(r.hit());
    r.lease->release();
    auto res = cache.invalidate(by_artifact(r.artifact_id));
    CHECK_EQ(res.invalidated, 1u);
  }
  gc::GraphCache cache(persistent_cfg(tmp.path));
  REQUIRE(cache.recover().ok());
  req.allow_capture = false;
  auto r = cache.lookup(req);
  CHECK(!r.hit());
}

// Replay never moves more bytes than the caller's declared buffers hold, also
// for a dynamic-shape hit whose request is smaller than the captured graph.
GC_TEST(replay_respects_buffer_sizes) {
  gc::GraphCache cache(cpu_cfg());
  auto big = test_util::make_cpu_request("reg_bounds", kBytes, true);
  big.descriptor.nodes[1].shape.dynamic = true;
  big.descriptor.nodes[1].shape.max_rank = 1;
  auto r1 = cache.lookup(big);
  REQUIRE(r1.hit());
  CHECK(!replay_declared(cache, r1, 16, 16).ok());
  CHECK(replay_declared(cache, r1, kBytes, kBytes).ok());
  r1.lease->release();

  auto small = test_util::make_cpu_request("reg_bounds", 16, true);
  small.descriptor.nodes[1].shape.dynamic = true;
  small.descriptor.nodes[1].shape.max_rank = 1;
  small.policy.require_exact_topology = false;
  small.allow_capture = false;
  auto r2 = cache.lookup(small);
  REQUIRE(r2.hit());
  CHECK(r2.outcome == gc::LookupOutcome::CompatibleDynamicShapeHit);
  CHECK(!replay_declared(cache, r2, 16, 16).ok());
  r2.lease->release();
}

// Concurrent unpins of a single pin: exactly one succeeds and the count never
// wraps.
GC_TEST(unpin_concurrent_no_underflow) {
  gc::GraphCache cache(cpu_cfg());
  auto r = cache.lookup(test_util::make_cpu_request("reg_unpin", kBytes, true));
  REQUIRE(r.hit());
  r.lease->release();
  const auto id = r.artifact_id;
  constexpr int kThreads = 4;
  constexpr int kTrials = 2000;
  int bad_trials = 0;
  for (int trial = 0; trial < kTrials; ++trial) {
    REQUIRE(cache.pin(id).ok());
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> successes{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
      ts.emplace_back([&] {
        ready.fetch_add(1);
        while (!go.load()) {}
        if (cache.unpin(id).ok()) successes.fetch_add(1);
      });
    }
    while (ready.load() < kThreads) {}
    go = true;
    for (auto& th : ts) th.join();
    if (successes.load() != 1) {
      ++bad_trials;   // the count wrapped; stop here
      break;
    }
  }
  CHECK_EQ(bad_trials, 0);
  CHECK(!cache.unpin(id).ok());   // nothing left pinned
}

// Concurrent invalidations, captures and lookups across workloads. Every
// iteration uses a fresh workload, so the invalidated-key set keeps growing
// (and rehashing) while other threads invalidate and capture.
GC_TEST(concurrent_invalidation_and_capture) {
  gc::GraphCache cache(cpu_cfg());
  constexpr int kThreads = 8;
  constexpr int kIters = 400;
  std::atomic<bool> stop{false};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&, t] {
      for (int i = 0; i < kIters; ++i) {
        const std::string name = "reg_conc_" + std::to_string(t) + "_" + std::to_string(i);
        auto r = cache.lookup(test_util::make_cpu_request(name, kBytes, true));
        if (r.hit()) r.lease->release();
        gc::InvalidationRequest inv;
        inv.scope = gc::InvalidationScope::ByWorkload;
        inv.workload.logical_name = name;
        (void)cache.invalidate(inv);
      }
    });
  }
  std::thread all([&] {
    while (!stop.load()) (void)cache.invalidate_all();
  });
  for (auto& th : ts) th.join();
  stop = true;
  all.join();
  auto m = cache.metrics();
  CHECK_EQ(m.active_leases, 0u);
  CHECK_EQ(m.active_captures, 0u);
}

#ifdef GC_HAS_CUDA
namespace {
gc::GraphLookupRequest cuda_request(const std::string& name, std::uint64_t bytes) {
  gc::GraphLookupRequest r;
  r.workload.logical_name = name;
  r.workload.logical_version = 1;
  r.backend.kind = gc::BackendKind::Cuda;
  r.backend.backend_name = "cuda-graph";
  r.backend.backend_version = 1;
  r.runtime.runtime_version = 1;
  r.runtime.graph_abi = 1;
  r.runtime.kernel_abi = 1;
  r.device.vendor = gc::AcceleratorVendor::Nvidia;
  r.device.architecture = "blackwell";
  r.device.compute_major = 12;
  r.device.compute_minor = 0;
  gc::GraphNodeDescriptor k;
  k.node_id = gc::GraphNodeId(0x1, 0x1);
  k.category = gc::NodeCategory::Kernel;
  k.name = "scale";
  k.kernel.name = "scale";
  k.kernel.id_hi = 0x5000; k.kernel.id_lo = 1;
  k.kernel.generation = gc::GraphGeneration(1);
  k.kernel.abi = 1;
  k.kernel.content_digest = "k-scale";
  k.kernel.contributes_to_correctness = true;
  k.memory_bytes = bytes;
  k.shape.dims = {static_cast<std::int64_t>(bytes / 4)};
  k.datatype = gc::Datatype::F32;
  k.layout = gc::TensorLayout::Contiguous;
  k.scalar.specialized = true;
  k.scalar.float_value = 3.0f;
  k.reactive_input_indices = {0};
  k.reactive_output_indices = {0};
  r.descriptor.nodes = {k};
  r.descriptor.backend = r.backend;
  r.descriptor.runtime = r.runtime;
  r.descriptor.device = r.device;
  r.descriptor.sync.capture_mode = gc::CaptureMode::StreamCapture;
  r.descriptor.sync.stream_semantics = gc::StreamSemantics::StreamOrdered;
  r.descriptor.provenance.graph_name = name;
  r.descriptor.dependencies = {k.kernel};
  r.allow_capture = true;
  r.graph_name = name;
  return r;
}

std::size_t cuda_free_bytes() {
  std::size_t free_b = 0, total_b = 0;
  (void)cudaMemGetInfo(&free_b, &total_b);
  return free_b;
}
}  // namespace

// Eviction frees the CUDA graph and its device buffers, and so does destroying
// a cache that still holds a captured graph.
GC_TEST(cuda_eviction_frees_device_memory) {
  REQUIRE(gc::cuda_available());
  (void)cudaFree(nullptr);
  constexpr std::uint64_t kBig = 64ull << 20;            // 2 device buffers of 64 MiB per graph
  constexpr std::size_t kSlack = 32u << 20;
  const std::size_t free0 = cuda_free_bytes();
  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cuda;
    gc::GraphCache cache(cfg);
    auto r = cache.lookup(cuda_request("reg_cuda_evict", kBig));
    REQUIRE(r.hit());
    r.lease->release();
    CHECK(cuda_free_bytes() + 2 * kBig - kSlack <= free0);   // allocated
    REQUIRE(cache.evict(r.artifact_id).ok());
    CHECK(cuda_free_bytes() + kSlack >= free0);               // released by eviction
    auto r2 = cache.lookup(cuda_request("reg_cuda_drop", kBig));
    REQUIRE(r2.hit());
    r2.lease->release();
  }
  CHECK(cuda_free_bytes() + kSlack >= free0);                 // released with the cache
}
#endif

int main(int argc, char** argv) {
  int ran = 0;
  for (auto& t : ::gc_test::registry()) {
    if (argc > 1 && t.name != argv[1]) continue;
    ::gc_test::current() = t.name;
    t.fn();
    ++ran;
  }
  std::printf("gc_test: %d tests, %d checks, %d failures\n", ran, ::gc_test::total_assertions(),
              ::gc_test::failures());
  if (ran == 0) return 2;
  return ::gc_test::failures() == 0 ? 0 : 1;
}
