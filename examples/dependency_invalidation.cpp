#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("dep_inval");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  auto req2 = req;
  req2.descriptor.dependencies[0].generation = GraphGeneration(9);
  req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  std::printf("self_generation_ok=%d stale_dep_hit=%d\n", r1.hit()?1:0, r2.hit()?1:0);
  return (!r2.hit()) ? 0 : 1;
}
