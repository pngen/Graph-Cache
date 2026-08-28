#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("lease_inval");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  InvalidationRequest inv; inv.scope = InvalidationScope::ByWorkload; inv.workload.logical_name = "lease_inval";
  [[maybe_unused]] auto _iv = cache.invalidate(inv);
  req.allow_capture = false;
  auto r2 = cache.lookup(req);
  std::printf("before=%d after_invalidation_hit=%d\n", r1.hit()?1:0, r2.hit()?1:0);
  return (!r2.hit()) ? 0 : 1;
}