#include "graphcache/cache.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace gc {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kMagic = 0x47434652u;  // 'GCFR'
constexpr std::uint32_t kFormatVersion = 1u;
constexpr std::size_t kChecksumLen = 32u;

std::string id_to_hex(const GraphArtifactId& id) {
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                static_cast<unsigned long long>(id.hi),
                static_cast<unsigned long long>(id.lo));
  return std::string(buf);
}

bool hex_to_id(const std::string& s, GraphArtifactId& id) {
  if (s.size() != 32) return false;
  auto val = [&](char c) -> unsigned {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
    return 16;
  };
  auto parse16 = [&](std::size_t off) -> std::uint64_t {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 16; ++i) {
      unsigned d = val(s[off + i]);
      if (d > 15) return 0;
      v = (v << 4) | d;
    }
    return v;
  };
  id.hi = parse16(0);
  id.lo = parse16(16);
  // verify no invalid char anywhere
  for (char c : s) if (val(c) > 15) return false;
  return true;
}

} // namespace

struct PersistenceStore::Impl {
  fs::path dir;
  std::uint32_t version;
  std::mutex mutex;

  explicit Impl(std::string d, std::uint32_t v)
      : dir(std::move(d)), version(v) {
    fs::create_directories(dir);
  }

  std::vector<std::uint8_t> build_envelope(std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> out;
    out.reserve(4 + 4 + 4 + payload.size() + 4 + kChecksumLen);
    auto push_u32 = [&](std::uint32_t v) {
      out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
      out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
      out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
      out.push_back(static_cast<std::uint8_t>(v & 0xffu));
    };
    push_u32(kMagic);
    push_u32(version);
    push_u32(static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    auto digest = Sha256::compute(payload.data(), payload.size());
    push_u32(static_cast<std::uint32_t>(kChecksumLen));
    out.insert(out.end(), digest.begin(), digest.end());
    return out;
  }

  // Returns false (and sets err) for any malformed envelope.
  bool parse_envelope(const std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& payload,
                      ErrorCode& err) {
    if (data.size() < 16) { err = ErrorCode::PersistenceTruncated; return false; }
    auto rd_u32 = [&](std::size_t off, std::uint32_t& v) -> bool {
      if (off + 4 > data.size()) return false;
      v = (static_cast<std::uint32_t>(data[off]) << 24) |
          (static_cast<std::uint32_t>(data[off + 1]) << 16) |
          (static_cast<std::uint32_t>(data[off + 2]) << 8) |
          static_cast<std::uint32_t>(data[off + 3]);
      return true;
    };
    std::uint32_t magic, ver, plen, clen;
    if (!rd_u32(0, magic)) { err = ErrorCode::PersistenceTruncated; return false; }
    if (magic != kMagic) { err = ErrorCode::PersistenceCorrupt; return false; }
    if (!rd_u32(4, ver)) { err = ErrorCode::PersistenceTruncated; return false; }
    if (ver != version) { err = ErrorCode::PersistenceUnknownVersion; return false; }
    if (!rd_u32(8, plen)) { err = ErrorCode::PersistenceTruncated; return false; }
    if (12u + plen + 4u + kChecksumLen != data.size()) {
      if (12u + plen + 4u + kChecksumLen > data.size()) { err = ErrorCode::PersistenceTruncated; return false; }
      err = ErrorCode::PersistenceTrailingGarbage;
      return false;
    }
    if (!rd_u32(12u + plen, clen)) { err = ErrorCode::PersistenceTruncated; return false; }
    if (clen != kChecksumLen) { err = ErrorCode::PersistenceCorrupt; return false; }
    payload.assign(data.begin() + 12, data.begin() + 12 + plen);
    auto digest = Sha256::compute(payload.data(), payload.size());
    std::size_t csum_off = 16u + plen;
    for (std::size_t i = 0; i < kChecksumLen; ++i) {
      if (data[csum_off + i] != digest[i]) { err = ErrorCode::PersistenceChecksumMismatch; return false; }
    }
    err = ErrorCode::Ok;
    return true;
  }

  fs::path file_for(const GraphArtifactId& id) {
    return dir / ("gc-" + id_to_hex(id) + ".gcf");
  }
  fs::path temp_for(const GraphArtifactId& id) {
    return dir / ("gc-" + id_to_hex(id) + ".tmp");
  }
};

PersistenceStore::PersistenceStore(std::string directory, std::uint32_t format_version)
    : impl_(std::make_unique<Impl>(std::move(directory), format_version)) {}

PersistenceStore::~PersistenceStore() = default;
PersistenceStore::PersistenceStore(PersistenceStore&&) noexcept = default;
PersistenceStore& PersistenceStore::operator=(PersistenceStore&&) noexcept = default;

std::uint32_t PersistenceStore::format_version() const { return impl_->version; }
std::string PersistenceStore::directory() const { return impl_->dir.string(); }

Result<void> PersistenceStore::put(const GraphArtifactId& id,
                                   const std::vector<std::uint8_t>& payload) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto env = impl_->build_envelope(payload);
  fs::path tmp = impl_->temp_for(id);
  fs::path fin = impl_->file_for(id);
  {
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) return Result<void>::failure(Error(ErrorCode::PersistenceIoError, "cannot open temp:" + tmp.string()));
    ofs.write(reinterpret_cast<const char*>(env.data()), static_cast<std::streamsize>(env.size()));
    ofs.flush();
    if (!ofs) return Result<void>::failure(Error(ErrorCode::PersistenceIoError, "cannot write temp:" + tmp.string()));
  }
  std::error_code ec;
  fs::rename(tmp, fin, ec);
  if (ec) {
    fs::remove(tmp);
    return Result<void>::failure(Error(ErrorCode::PersistenceIoError, "rename failed: " + ec.message()));
  }
  return Result<void>::success();
}

Result<std::vector<std::uint8_t>> PersistenceStore::get(const GraphArtifactId& id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  fs::path fin = impl_->file_for(id);
  if (!fs::exists(fin)) return Result<std::vector<std::uint8_t>>::failure(Error(ErrorCode::PersistenceNotFound, "no file"));
  std::ifstream ifs(fin, std::ios::binary);
  if (!ifs) return Result<std::vector<std::uint8_t>>::failure(Error(ErrorCode::PersistenceIoError, "cannot open"));
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  std::vector<std::uint8_t> payload;
  ErrorCode err;
  if (!impl_->parse_envelope(data, payload, err)) {
    return Result<std::vector<std::uint8_t>>::failure(Error(err, "persisted envelope rejected"));
  }
  return Result<std::vector<std::uint8_t>>::success(std::move(payload));
}

Result<void> PersistenceStore::remove(const GraphArtifactId& id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::error_code ec;
  fs::remove(impl_->file_for(id), ec);
  fs::remove(impl_->temp_for(id), ec);
  (void)ec;
  return Result<void>::success();
}

std::vector<GraphArtifactId> PersistenceStore::list() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<GraphArtifactId> ids;
  for (const auto& entry : fs::directory_iterator(impl_->dir)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("gc-", 0) != 0) continue;
    if (name.size() != 3 + 32 + 4) continue;  // "gc-" + 32 hex + ".gcf"
    if (name.substr(3 + 32) != ".gcf") continue;
    GraphArtifactId id;
    if (hex_to_id(name.substr(3, 32), id)) ids.push_back(id);
  }
  return ids;
}

} // namespace gc
