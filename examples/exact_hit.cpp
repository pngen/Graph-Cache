#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("exact_hit");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  auto r2 = cache.lookup(req);
  std::printf("hit1=%d hit2=%d same_artifact=%d gen=%llu\n", r1.hit()?1:0, r2.hit()?1:0, r1.artifact_id==r2.artifact_id?1:0, (unsigned long long)r1.generation.value);
  return r2.hit() && r1.artifact_id==r2.artifact_id ? 0 : 1;
}
