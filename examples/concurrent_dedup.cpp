#include "common.hpp"
#include <thread>
#include <atomic>
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  std::atomic<int> hits{0}; std::atomic<int> misses{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t) ts.emplace_back([&]{ auto r = cache.lookup(ex_util::cpu_request("dedup")); if (r.hit()) hits++; else misses++; });
  for (auto& th : ts) th.join();
  auto m = cache.metrics();
  std::printf("hits=%d misses=%d captures=%llu (single-flight)\n", hits.load(), misses.load(), (unsigned long long)m.captures);
  return (hits.load() == 8 && misses.load() == 0 && m.captures == 1) ? 0 : 1;
}
