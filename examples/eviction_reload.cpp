#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("evict_reload");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  r1.lease->release();
  if (!cache.evict(r1.artifact_id).ok()) return 1;
  auto r2 = cache.lookup(req);
  std::printf("evict+reload hit=%d\n", r2.hit()?1:0);
  return r2.hit() ? 0 : 1;
}