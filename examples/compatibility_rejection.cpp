#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("compat_reject");
  auto r1 = cache.lookup(req);
  if (!r1.hit()) return 1;
  // A request with a different datatype is not a candidate under the same key.
  auto req2 = req; req2.descriptor.nodes[1].datatype = Datatype::F16; req2.allow_capture = false;
  auto r2 = cache.lookup(req2);
  std::printf("orig_hit=%d reject_hit=%d\n", r1.hit()?1:0, r2.hit()?1:0);
  return (!r2.hit()) ? 0 : 1;
}
