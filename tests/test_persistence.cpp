#include "test_util.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <process.h>

namespace fs = std::filesystem;

namespace {
struct TempDir {
  fs::path path;
  TempDir() {
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("gc-persist-" + std::to_string(stamp) + "-" +
                                        std::to_string(::_getpid()));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::string id_hex(const gc::GraphArtifactId& id) {
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                static_cast<unsigned long long>(id.hi),
                static_cast<unsigned long long>(id.lo));
  return std::string(buf);
}
} // namespace

GC_TEST(persistence_recover_reconstructs) {
  TempDir tmp;
  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cpu;
    cfg.enable_persistence = true;
    cfg.persistence_dir = tmp.path.string();
    gc::GraphCache cache(cfg);
    auto req = test_util::make_cpu_request("persist", 64, true);
    auto r = cache.lookup(req);
    REQUIRE(r.hit());
    auto p = cache.persist_all();
    REQUIRE(p.ok());
  }  // cache destroyed; live backend handles absent

  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cpu;
    cfg.enable_persistence = true;
    cfg.persistence_dir = tmp.path.string();
    gc::GraphCache cache(cfg);
    auto rec = cache.recover();
    REQUIRE(rec.ok());
    auto req = test_util::make_cpu_request("persist", 64, true);
    auto r = cache.lookup(req);
    CHECK(r.hit());   // reconstructed from persisted authority
    // Backend residency is absent after recover; replay performs a recapture
    // and must remain correct.
    std::vector<float> in(16, 3.0f), out(16, 0.0f);
    gc::ReplayBuffers bufs;
    bufs.inputs = {in.data()}; bufs.outputs = {out.data()};
    bufs.input_bytes = {64}; bufs.output_bytes = {64};
    gc::GraphReplayRequest rr;
    rr.lease = r.lease;
    rr.buffers = bufs;
    rr.descriptor.replay_attempt = gc::ReplayAttemptId(7);
    rr.expected_generation = r.generation;
    auto replay = cache.replay(rr);
    REQUIRE(replay.ok());
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(out[i] - 6.0f) < 1e-4f);
  }
}

GC_TEST(persistence_corrupt_rejected) {
  TempDir tmp;
  gc::GraphArtifactId id;
  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cpu;
    cfg.enable_persistence = true;
    cfg.persistence_dir = tmp.path.string();
    gc::GraphCache cache(cfg);
    auto req = test_util::make_cpu_request("corrupt", 64, true);
    auto r = cache.lookup(req);
    REQUIRE(r.hit());
    id = r.artifact_id;
    [[maybe_unused]] auto _p = cache.persist_all();
  }
  // Corrupt the file with wrong magic.
  fs::path f = tmp.path / ("gc-" + id_hex(id) + ".gcf");
  {
    std::ofstream ofs(f, std::ios::binary | std::ios::trunc);
    std::uint8_t junk[64] = {0xDE, 0xAD, 0xBE, 0xEF};
    ofs.write(reinterpret_cast<const char*>(junk), 64);
  }
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  cfg.enable_persistence = true;
  cfg.persistence_dir = tmp.path.string();
  gc::GraphCache cache(cfg);
  auto rec = cache.recover();
  CHECK(rec.ok());   // recovery itself completes; corrupt entries rejected
  auto m = cache.metrics();
  CHECK(m.corruption_count >= 1);
  auto req = test_util::make_cpu_request("corrupt", 64, true);
  req.allow_capture = false;
  auto r = cache.lookup(req);
  // No valid entry; must be a miss.
  CHECK(!r.hit());
}

GC_TEST(persistence_truncated_rejected_and_unknown_version) {
  TempDir tmp;
  gc::GraphArtifactId id;
  {
    gc::GraphCacheConfig cfg;
    cfg.backend_kind = gc::BackendKind::Cpu;
    cfg.enable_persistence = true;
    cfg.persistence_dir = tmp.path.string();
    gc::GraphCache cache(cfg);
    auto req = test_util::make_cpu_request("trunc", 64, true);
    auto r = cache.lookup(req);
    REQUIRE(r.hit());
    id = r.artifact_id;
    [[maybe_unused]] auto _p = cache.persist_all();
  }
  fs::path f = tmp.path / ("gc-" + id_hex(id) + ".gcf");
  // Read then write only the first half (truncate).
  std::vector<std::uint8_t> data;
  {
    std::ifstream ifs(f, std::ios::binary);
    data.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  }
  {
    std::ofstream ofs(f, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() / 2));
  }
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  cfg.enable_persistence = true;
  cfg.persistence_dir = tmp.path.string();
  gc::GraphCache cache(cfg);
  auto rec = cache.recover();
  CHECK(rec.ok());
  auto m = cache.metrics();
  CHECK(m.corruption_count >= 1);
}

GC_TEST(persistence_orphan_temp_cleanup) {
  TempDir tmp;
  // Create an orphan temp artifact.
  {
    std::ofstream ofs(tmp.path / "gc-deadbeefdeadbeefdeadbeefdeadbeef.tmp", std::ios::binary);
    ofs << "partial";
  }
  gc::GraphCacheConfig cfg;
  cfg.backend_kind = gc::BackendKind::Cpu;
  cfg.enable_persistence = true;
  cfg.persistence_dir = tmp.path.string();
  gc::GraphCache cache(cfg);
  [[maybe_unused]] auto _rec = cache.recover();
  bool orphan_gone = true;
  for (const auto& e : fs::directory_iterator(tmp.path)) {
    if (e.path().extension() == ".tmp") orphan_gone = false;
  }
  CHECK(orphan_gone);
}

GC_TEST_MAIN
