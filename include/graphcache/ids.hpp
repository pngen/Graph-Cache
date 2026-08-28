#pragma once
// Strong, lossless numeric identities used across Graph Cache.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace gc {

// ---- Generation / attempt identity types ----
struct CacheGeneration {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr CacheGeneration() = default;
  constexpr explicit CacheGeneration(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const CacheGeneration&) const = default;
  friend bool operator==(const CacheGeneration&, const CacheGeneration&) = default;
};

struct GraphGeneration {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr GraphGeneration() = default;
  constexpr explicit GraphGeneration(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const GraphGeneration&) const = default;
  friend bool operator==(const GraphGeneration&, const GraphGeneration&) = default;
};

struct CaptureAttemptId {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr CaptureAttemptId() = default;
  constexpr explicit CaptureAttemptId(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const CaptureAttemptId&) const = default;
  friend bool operator==(const CaptureAttemptId&, const CaptureAttemptId&) = default;
};

struct ReplayAttemptId {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr ReplayAttemptId() = default;
  constexpr explicit ReplayAttemptId(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const ReplayAttemptId&) const = default;
  friend bool operator==(const ReplayAttemptId&, const ReplayAttemptId&) = default;
};

struct LoadGeneration {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr LoadGeneration() = default;
  constexpr explicit LoadGeneration(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const LoadGeneration&) const = default;
  friend bool operator==(const LoadGeneration&, const LoadGeneration&) = default;
};

struct ResidencyGeneration {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr ResidencyGeneration() = default;
  constexpr explicit ResidencyGeneration(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const ResidencyGeneration&) const = default;
  friend bool operator==(const ResidencyGeneration&, const ResidencyGeneration&) = default;
};

struct CoordinatorEpoch {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr CoordinatorEpoch() = default;
  constexpr explicit CoordinatorEpoch(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const CoordinatorEpoch&) const = default;
  friend bool operator==(const CoordinatorEpoch&, const CoordinatorEpoch&) = default;
};

struct WorkerId {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr WorkerId() = default;
  constexpr explicit WorkerId(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const WorkerId&) const = default;
  friend bool operator==(const WorkerId&, const WorkerId&) = default;
};

struct WorkerBootId {
  std::uint64_t value{0};
  static constexpr std::uint64_t Invalid = 0;
  constexpr WorkerBootId() = default;
  constexpr explicit WorkerBootId(std::uint64_t v) : value(v) {}
  constexpr bool valid() const { return value != Invalid; }
  constexpr auto operator<=>(const WorkerBootId&) const = default;
  friend bool operator==(const WorkerBootId&, const WorkerBootId&) = default;
};

// ---- Stable object identities ----
// Opaque 128-bit identifiers. The two 64-bit halves preserve a lossless
// identity and a domain discriminator so distinct domains can never collide.
struct GraphId {
  std::uint64_t hi{0};
  std::uint64_t lo{0};
  constexpr GraphId() = default;
  constexpr GraphId(std::uint64_t h, std::uint64_t l) : hi(h), lo(l) {}
  constexpr bool valid() const { return lo != 0; }
  constexpr auto operator<=>(const GraphId&) const = default;
  friend bool operator==(const GraphId&, const GraphId&) = default;
  std::string to_string() const;
};

struct GraphArtifactId {
  std::uint64_t hi{0};
  std::uint64_t lo{0};
  constexpr GraphArtifactId() = default;
  constexpr GraphArtifactId(std::uint64_t h, std::uint64_t l) : hi(h), lo(l) {}
  constexpr bool valid() const { return lo != 0; }
  constexpr auto operator<=>(const GraphArtifactId&) const = default;
  friend bool operator==(const GraphArtifactId&, const GraphArtifactId&) = default;
  std::string to_string() const;
};

struct GraphNodeId {
  std::uint64_t hi{0};
  std::uint64_t lo{0};
  constexpr GraphNodeId() = default;
  constexpr GraphNodeId(std::uint64_t h, std::uint64_t l) : hi(h), lo(l) {}
  constexpr bool valid() const { return lo != 0; }
  constexpr auto operator<=>(const GraphNodeId&) const = default;
  friend bool operator==(const GraphNodeId&, const GraphNodeId&) = default;
};

struct GraphEdgeId {
  std::uint64_t hi{0};
  std::uint64_t lo{0};
  constexpr GraphEdgeId() = default;
  constexpr GraphEdgeId(std::uint64_t h, std::uint64_t l) : hi(h), lo(l) {}
  constexpr bool valid() const { return lo != 0; }
  constexpr auto operator<=>(const GraphEdgeId&) const = default;
  friend bool operator==(const GraphEdgeId&, const GraphEdgeId&) = default;
};

} // namespace gc

// ---- Hashing support ----
namespace std {
template <> struct hash<gc::CacheGeneration> {
  size_t operator()(const gc::CacheGeneration& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::GraphGeneration> {
  size_t operator()(const gc::GraphGeneration& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::CaptureAttemptId> {
  size_t operator()(const gc::CaptureAttemptId& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::ReplayAttemptId> {
  size_t operator()(const gc::ReplayAttemptId& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::LoadGeneration> {
  size_t operator()(const gc::LoadGeneration& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::ResidencyGeneration> {
  size_t operator()(const gc::ResidencyGeneration& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::CoordinatorEpoch> {
  size_t operator()(const gc::CoordinatorEpoch& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::WorkerId> {
  size_t operator()(const gc::WorkerId& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::WorkerBootId> {
  size_t operator()(const gc::WorkerBootId& v) const noexcept {
    return hash<uint64_t>{}(v.value);
  }
};
template <> struct hash<gc::GraphId> {
  size_t operator()(const gc::GraphId& v) const noexcept {
    size_t h = hash<uint64_t>{}(v.lo);
    h ^= hash<uint64_t>{}(v.hi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
template <> struct hash<gc::GraphArtifactId> {
  size_t operator()(const gc::GraphArtifactId& v) const noexcept {
    size_t h = hash<uint64_t>{}(v.lo);
    h ^= hash<uint64_t>{}(v.hi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
template <> struct hash<gc::GraphNodeId> {
  size_t operator()(const gc::GraphNodeId& v) const noexcept {
    size_t h = hash<uint64_t>{}(v.lo);
    h ^= hash<uint64_t>{}(v.hi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
template <> struct hash<gc::GraphEdgeId> {
  size_t operator()(const gc::GraphEdgeId& v) const noexcept {
    size_t h = hash<uint64_t>{}(v.lo);
    h ^= hash<uint64_t>{}(v.hi) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
} // namespace std

namespace gc {
inline std::string GraphId::to_string() const {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%016llx:%016llx",
                static_cast<unsigned long long>(hi),
                static_cast<unsigned long long>(lo));
  return std::string(buf);
}
inline std::string GraphArtifactId::to_string() const {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%016llx:%016llx",
                static_cast<unsigned long long>(hi),
                static_cast<unsigned long long>(lo));
  return std::string(buf);
}
} // namespace gc
