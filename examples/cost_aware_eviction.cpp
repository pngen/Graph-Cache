#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  // Eviction policy options; the engine runs deterministic cost-aware scoring.
  cfg.eviction.cost_aware = true;
  cfg.eviction.recapture_cost_aware = true;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("cost_aware");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  r1.lease->release();
  // Pin then confirm eviction is refused while pinned.
  if (!cache.pin(r1.artifact_id).ok()) return 1;
  bool evict_refused = !cache.evict(r1.artifact_id).ok();
  [[maybe_unused]] auto _up = cache.unpin(r1.artifact_id);
  auto r2 = cache.lookup(req);
  std::printf("pinned_eviction_refused=%d reload_hit=%d\n", evict_refused?1:0, r2.hit()?1:0);
  return (evict_refused && r2.hit()) ? 0 : 1;
}