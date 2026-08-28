#include "common.hpp"
int main() {
  using namespace gc;
  GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu;
  GraphCache cache(cfg);
  auto req = ex_util::cpu_request("shape_spec");
  req.descriptor.nodes[1].shape.dims = {64};      // shape-specialized (256 bytes)
  req.descriptor.nodes[1].memory_bytes = 256;
  auto r = cache.lookup(req);
  if (!r.hit()) return 1;
  std::vector<float> in(64, 1.0f);
  int ok = ex_util::replay_and_check(cache, r, in, 2.0);
  std::printf("shape-specialized replay ok=%d\n", ok);
  return ok;
}
