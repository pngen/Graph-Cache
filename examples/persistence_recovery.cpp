#include "common.hpp"
#include <filesystem>
int main() {
  using namespace gc;
  auto tmp = ex_util::temp_dir("persist");
  {
    GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu; cfg.enable_persistence = true; cfg.persistence_dir = tmp.string();
    GraphCache cache(cfg);
    auto r = cache.lookup(ex_util::cpu_request("persist_ex"));
    if (!r.hit()) return 1;
    [[maybe_unused]] auto _p = cache.persist_all();
  }
  {
    GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu; cfg.enable_persistence = true; cfg.persistence_dir = tmp.string();
    GraphCache cache(cfg);
    [[maybe_unused]] auto _rc = cache.recover();
    auto r = cache.lookup(ex_util::cpu_request("persist_ex"));
    std::printf("recovery hit=%d\n", r.hit()?1:0);
    int ok = r.hit() ? ex_util::replay_and_check(cache, r, std::vector<float>(16, 1.0f), 2.0) : 1;
    std::filesystem::remove_all(tmp);
    return ok;
  }
}