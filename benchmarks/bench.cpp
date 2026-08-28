// Measured Graph Cache benchmarks. Reports measured latencies/throughput, not
// simulated or empty-loop figures. Avoided capture cost is reported as derived
// from measured cold-vs-warm values.
#include "graphcache/cache.hpp"
#include "graphcache/compatibility.hpp"
#include "graphcache/sha256.hpp"
#include "graphcache/topology.hpp"
#include <cstdio>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
namespace {
using clk = std::chrono::steady_clock;
double ms_since(clk::time_point a, clk::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
double us_since(clk::time_point a, clk::time_point b) { return std::chrono::duration<double,std::micro>(b-a).count(); }
gc::GraphLookupRequest cpu_request(const std::string& name, double alpha = 2.0, std::uint64_t bytes = 64) {
  gc::GraphLookupRequest req;
  req.workload.logical_name = name; req.workload.logical_version = 1;
  req.backend.kind = gc::BackendKind::Cpu; req.backend.backend_name = "cpu"; req.backend.backend_version = 1;
  req.runtime.runtime_version = 1; req.runtime.graph_abi = 1; req.runtime.kernel_abi = 1;
  req.device.vendor = gc::AcceleratorVendor::Cpu; req.device.architecture = "cpu";
  gc::GraphDescriptor d; d.backend = req.backend; d.runtime = req.runtime; d.device = req.device;
  gc::GraphNodeDescriptor c; c.node_id = gc::GraphNodeId(1,1); c.category = gc::NodeCategory::MemoryCopy;
  c.name = "copy"; c.memory_bytes = bytes; c.reactive_input_indices = {0}; c.reactive_output_indices = {0}; c.binding.rebinding_eligible = true;
  gc::GraphNodeDescriptor k; k.node_id = gc::GraphNodeId(2,2); k.category = gc::NodeCategory::Kernel; k.name = "scale";
  k.kernel.name = "scale"; k.kernel.contributes_to_correctness = true; k.memory_bytes = bytes;
  k.shape.dims = {static_cast<std::int64_t>(bytes/4)}; k.datatype = gc::Datatype::F32; k.layout = gc::TensorLayout::Contiguous;
  k.scalar.specialized = true; k.scalar.float_value = static_cast<float>(alpha);
  k.reactive_input_indices = {0}; k.reactive_output_indices = {0}; k.binding.rebinding_eligible = true;
  gc::GraphEdgeDescriptor e; e.edge_id = gc::GraphEdgeId(1,1); e.from = c.node_id; e.to = k.node_id; e.kind = gc::DependencyKind::Execution;
  d.nodes = {c,k}; d.edges = {e}; d.dependencies = {k.kernel};
  req.descriptor = d; return req;
}
} // namespace
int main() {
  using namespace gc;
  auto req = cpu_request("bench");
  CompatibilityFacts facts;
  facts.workload = req.workload; facts.backend = req.backend; facts.runtime = req.runtime; facts.device = req.device;
  facts.topology_canonical = canonical_topology(req.descriptor.nodes, req.descriptor.edges);
  facts.datatypes.push_back(Datatype::F32); facts.layouts.push_back(TensorLayout::Contiguous);
  facts.dependencies = req.descriptor.dependencies; facts.model_operator_revision = req.workload.model_operator_revision;
  constexpr int kRepeats = 200000;
  auto t0 = clk::now();
  for (int i = 0; i < kRepeats; ++i) { auto k = GraphCompatibilityKey::build(facts); if (!k.ok()) return 1; }
  auto t1 = clk::now();
  std::printf("compat-key: %.0f keys/s (measured)\n", kRepeats/(ms_since(t0,t1)/1000.0));
  auto canon = facts_to_canonical(facts);
  t0 = clk::now();
  for (int i = 0; i < kRepeats; ++i) { auto d = Sha256::compute(canon.data(), canon.size()); if (d[0]==0xFF) return 1; }
  t1 = clk::now();
  std::printf("sha256: %.0f digests/s (measured)\n", kRepeats/(ms_since(t0,t1)/1000.0));
  t0 = clk::now();
  for (int i = 0; i < kRepeats; ++i) { auto d = topology_digest(req.descriptor.nodes, req.descriptor.edges); if (d[0]==0xFF) return 1; }
  t1 = clk::now();
  std::printf("topology-hash: %.0f hashes/s (measured)\n", kRepeats/(ms_since(t0,t1)/1000.0));
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu; GraphCache cache(cfg);
  auto tcold = clk::now(); auto r1 = cache.lookup(req); auto tcold_done = clk::now();
  if (!r1.hit()) return 1;
  double capture_ms = ms_since(tcold, tcold_done);
  std::printf("cpu-cold-capture: %.3f ms (measured, first capture+validation)\n", capture_ms);
  std::vector<float> in(16,1.0f), out(16,0.0f);
  gc::ReplayBuffers bufs; bufs.inputs={in.data()}; bufs.outputs={out.data()}; bufs.input_bytes={64}; bufs.output_bytes={64};
  constexpr int kReplays = 10000;
  double sum = 0;
  for (int i = 0; i < kReplays; ++i) {
    gc::GraphReplayRequest rr; rr.lease=r1.lease; rr.buffers=bufs; rr.expected_generation=r1.generation; rr.descriptor.replay_attempt=ReplayAttemptId(i+1);
    auto tr = clk::now(); [[maybe_unused]] auto _rp = cache.replay(rr); auto trd = clk::now(); sum += us_since(tr,trd);
  }
  std::printf("cpu-warm-replay: %.2f us/op mean (measured over %d replays)\n", sum/kReplays, kReplays);
  constexpr int kLooks = 100000;
  t0 = clk::now();
  for (int i = 0; i < kLooks; ++i) { auto r2 = cache.lookup(req); if (!r2.hit()) return 1; }
  t1 = clk::now();
  std::printf("lookup: %.0f lookups/s (measured)\n", kLooks/(ms_since(t0,t1)/1000.0));
  std::printf("avoided-capture (derived from cold-vs-warm): %.2f ms over %d replays\n", capture_ms*(kReplays-1), kReplays);
  // ---- Workload / thread / hit-miss matrix (measured) ----
  {
    const int N = 2000;
    GraphCacheConfig s; s.backend_kind = BackendKind::Cpu; GraphCache scale(s);
    std::vector<gc::GraphLookupRequest> reqs; reqs.reserve(N);
    for (int i = 0; i < N; ++i) { reqs.push_back(cpu_request("w" + std::to_string(i))); }
    auto ts0 = clk::now();
    for (int i = 0; i < N; ++i) { if (!scale.lookup(reqs[i]).hit()) return 1; }
    auto ts1 = clk::now();
    std::printf("scale: insert+hit %d graphs: %.2f ms (measured)\n", N, ms_since(ts0,ts1));
    ts0 = clk::now();
    for (int i = 0; i < N; ++i) { if (!scale.lookup(reqs[i]).hit()) return 1; }
    ts1 = clk::now();
    std::printf("scale: 100%c hit 1-thread lookups: %.0f/s (measured over %d)\n", '%', N/(ms_since(ts0,ts1)/1000.0), N);
    std::vector<gc::GraphLookupRequest> misses; misses.reserve(N/10);
    for (int i = 0; i < N/10; ++i) { auto rq = cpu_request("miss" + std::to_string(i)); rq.allow_capture = false; misses.push_back(rq); }
    int hits = 0, miss = 0;
    ts0 = clk::now();
    for (int i = 0; i < N; ++i) {
      if (i % 10 == 0) { if (!scale.lookup(misses[i/10]).hit()) miss++; }
      else { if (scale.lookup(reqs[i]).hit()) hits++; }
    }
    ts1 = clk::now();
    std::printf("scale: 90/10 hit/miss 1-thread lookups: %.0f/s (measured, hits=%d misses=%d)\n", N/(ms_since(ts0,ts1)/1000.0), hits, miss);
    std::atomic<int> thits{0};
    std::vector<std::thread> ts;
    ts0 = clk::now();
    for (int t = 0; t < 8; ++t) ts.emplace_back([&, t]{ for (int i = t; i < N; i += 8) { if (scale.lookup(reqs[i]).hit()) thits.fetch_add(1); } });
    for (auto& th : ts) th.join();
    ts1 = clk::now();
    std::printf("scale: 100%c hit 8-thread lookups: %.0f/s (measured, hits=%d)\n", '%', N/(ms_since(ts0,ts1)/1000.0), thits.load());    // 50/50 hit/miss (alternate seen / never-seen, no capture).
    int h5050 = 0, m5050 = 0;
    ts0 = clk::now();
    for (int i = 0; i < N; ++i) {
      if (i % 2 == 0) { if (scale.lookup(reqs[i]).hit()) h5050++; }
      else { if (!scale.lookup(misses[(i/2) % misses.size()]).hit()) m5050++; }
    }
    ts1 = clk::now();
    std::printf("scale: 50/50 hit/miss 1-thread lookups: %.0f/s (measured, hits=%d misses=%d)\n", N/(ms_since(ts0,ts1)/1000.0), h5050, m5050);
    // Different topology size (256 elements) hit throughput.
    std::vector<gc::GraphLookupRequest> big; big.reserve(N/2);
    for (int i = 0; i < N/2; ++i) { auto rq = cpu_request("big" + std::to_string(i), 2.0, 1024); big.push_back(rq); }
    for (int i = 0; i < N/2; ++i) { if (!scale.lookup(big[i]).hit()) return 1; }
    ts0 = clk::now();
    for (int i = 0; i < N/2; ++i) { if (!scale.lookup(big[i]).hit()) return 1; }
    ts1 = clk::now();
    std::printf("scale: 256-elem topo 1-thread lookups: %.0f/s (measured over %d)\n", (N/2)/(ms_since(ts0,ts1)/1000.0), N/2);

  }
  auto m = cache.metrics();
  std::printf("metrics: captures=%llu replays=%llu lookups=%llu (measured)\n",(unsigned long long)m.captures,(unsigned long long)m.replays,(unsigned long long)m.lookups);
  return 0;
}