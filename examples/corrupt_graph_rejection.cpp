#include "common.hpp"
#include <filesystem>
#include <fstream>
int main() {
  using namespace gc;
  auto tmp = ex_util::temp_dir("corrupt");
  GraphArtifactId id;
  {
    GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu; cfg.enable_persistence = true; cfg.persistence_dir = tmp.string();
    GraphCache cache(cfg);
    auto r = cache.lookup(ex_util::cpu_request("corrupt_ex"));
    if (!r.hit()) return 1;
    id = r.artifact_id; [[maybe_unused]] auto _p = cache.persist_all();
  }
  // Corrupt the file.
  char hex[40]; std::snprintf(hex, sizeof(hex), "%016llx%016llx", (unsigned long long)id.hi, (unsigned long long)id.lo);
  auto f = tmp / ("gc-" + std::string(hex) + ".gcf");
  { std::ofstream o(f, std::ios::binary|std::ios::trunc); std::uint8_t junk[64] = {0xDE,0xAD,0xBE,0xEF}; o.write((const char*)junk, 64); }
  {
    GraphCacheConfig cfg; cfg.backend_kind = BackendKind::Cpu; cfg.enable_persistence = true; cfg.persistence_dir = tmp.string();
    GraphCache cache(cfg);
    [[maybe_unused]] auto _rc = cache.recover();
    auto m = cache.metrics();
    std::printf("corruption_count=%llu\n", (unsigned long long)m.corruption_count);
    std::filesystem::remove_all(tmp);
    return m.corruption_count >= 1 ? 0 : 1;
  }
}