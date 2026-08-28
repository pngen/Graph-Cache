#pragma once
// Deterministic canonical typed encoding. Used for GraphCompatibilityKey,
// graph topology encoding, and persistence. All integers are encoded
// big-endian so the canonical byte stream is endian-independent.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gc {

// Well-defined tag registry for canonical fields.
enum class CanonicalTag : std::uint16_t {
  U8 = 1,
  U16 = 2,
  U32 = 3,
  U64 = 4,
  I64 = 5,
  F64 = 6,
  Bytes = 7,
  Str = 8,
  Bool = 9,
  U64List = 10,
  StrList = 11
};

namespace detail {
inline void put_be_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
  b.push_back(static_cast<std::uint8_t>(v & 0xffu));
}
inline void put_be_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 3; i >= 0; --i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
}
inline void put_be_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
}
} // namespace detail

class CanonicalWriter {
 public:
  void put_u8(std::uint16_t tag, std::uint8_t v) {
    begin(tag, 1);
    buf_.push_back(v);
  }
  void put_u16(std::uint16_t tag, std::uint16_t v) {
    begin(tag, 2);
    detail::put_be_u16(buf_, v);
  }
  void put_u32(std::uint16_t tag, std::uint32_t v) {
    begin(tag, 4);
    detail::put_be_u32(buf_, v);
  }
  void put_u64(std::uint16_t tag, std::uint64_t v) {
    begin(tag, 8);
    detail::put_be_u64(buf_, v);
  }
  void put_i64(std::uint16_t tag, std::int64_t v) {
    begin(tag, 8);
    detail::put_be_u64(buf_, static_cast<std::uint64_t>(v));
  }
  void put_f64(std::uint16_t tag, double v) {
    begin(tag, 8);
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    detail::put_be_u64(buf_, bits);
  }
  void put_bool(std::uint16_t tag, bool v) {
    begin(tag, 1);
    buf_.push_back(v ? 1u : 0u);
  }
  void put_bytes(std::uint16_t tag, std::span<const std::uint8_t> data) {
    begin(tag, data.size());
    buf_.insert(buf_.end(), data.begin(), data.end());
  }
  void put_str(std::uint16_t tag, std::string_view s) {
    begin(tag, s.size());
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void put_u64_list(std::uint16_t tag, std::span<const std::uint64_t> values) {
    begin(tag, values.size() * 8u + 4u);
    detail::put_be_u32(buf_, static_cast<std::uint32_t>(values.size()));
    for (std::uint64_t v : values) detail::put_be_u64(buf_, v);
  }
  void put_str_list(std::uint16_t tag, const std::vector<std::string>& values) {
    std::size_t total = 4u;
    for (const auto& s : values) total += s.size() + 4u;
    begin(tag, total);
    detail::put_be_u32(buf_, static_cast<std::uint32_t>(values.size()));
    for (const auto& s : values) {
      detail::put_be_u32(buf_, static_cast<std::uint32_t>(s.size()));
      buf_.insert(buf_.end(), s.begin(), s.end());
    }
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return buf_; }
  [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(buf_); }

 private:
  void begin(std::uint16_t tag, std::size_t len) {
    detail::put_be_u16(buf_, tag);
    detail::put_be_u32(buf_, static_cast<std::uint32_t>(len));
  }
  std::vector<std::uint8_t> buf_;
};

// Bounds-checked reader over a canonical stream.
class CanonicalReader {
 public:
  explicit CanonicalReader(std::span<const std::uint8_t> data) : data_(data) {}

  // Returns false when the stream is exhausted. Overflows are signalled through
  // a malformed marker set by consume() when it cannot safely advance.
  bool next(std::uint16_t& tag, std::span<const std::uint8_t>& payload) {
    if (off_ == data_.size()) return false;
    if (data_.size() - off_ < 6u) {
      malformed_ = true;
      return false;
    }
    tag = static_cast<std::uint16_t>((data_[off_] << 8) | data_[off_ + 1]);
    std::uint32_t len = (static_cast<std::uint32_t>(data_[off_ + 2]) << 24) |
                        (static_cast<std::uint32_t>(data_[off_ + 3]) << 16) |
                        (static_cast<std::uint32_t>(data_[off_ + 4]) << 8) |
                        (static_cast<std::uint32_t>(data_[off_ + 5]));
    off_ += 6;
    if (static_cast<std::size_t>(len) > data_.size() - off_) {
      malformed_ = true;
      return false;
    }
    payload = data_.subspan(off_, len);
    off_ += len;
    return true;
  }

  [[nodiscard]] bool malformed() const noexcept { return malformed_; }
  [[nodiscard]] bool at_end() const noexcept { return off_ == data_.size(); }

  // Typed payload decoders (big-endian). Return false on truncation.
  static bool decode_u16(std::span<const std::uint8_t> p, std::uint16_t& v) {
    if (p.size() != 2) return false;
    v = static_cast<std::uint16_t>((p[0] << 8) | p[1]);
    return true;
  }
  static bool decode_u32(std::span<const std::uint8_t> p, std::uint32_t& v) {
    if (p.size() != 4) return false;
    v = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    return true;
  }
  static bool decode_u64(std::span<const std::uint8_t> p, std::uint64_t& v) {
    if (p.size() != 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return true;
  }
  static bool decode_i64(std::span<const std::uint8_t> p, std::int64_t& v) {
    std::uint64_t u;
    if (!decode_u64(p, u)) return false;
    v = static_cast<std::int64_t>(u);
    return true;
  }
  static bool decode_f64(std::span<const std::uint8_t> p, double& v) {
    std::uint64_t bits;
    if (!decode_u64(p, bits)) return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
  }
  static bool decode_str(std::span<const std::uint8_t> p, std::string& v) {
    v.assign(reinterpret_cast<const char*>(p.data()), p.size());
    return true;
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t off_{0};
  bool malformed_{false};
};

// Canonical container framing: [magic u32][payload u32][payload].
// Used for checksummed persistence envelopes.
namespace canon_container {
inline constexpr std::uint32_t Magic = 0x47524348u; // 'GRCH'
inline void write(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> payload) {
  detail::put_be_u32(out, Magic);
  detail::put_be_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}
} // namespace canon_container

} // namespace gc
