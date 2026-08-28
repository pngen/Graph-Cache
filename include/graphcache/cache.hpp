#pragma once
// Graph Cache engine: lookup, capture, validation, lease/replay, invalidation,
// eviction, residency, generation authority, indexing, observability.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/backend.hpp"
#include "graphcache/clock.hpp"
#include "graphcache/compatibility.hpp"
#include "graphcache/ids.hpp"
#include "graphcache/lifecycle.hpp"
#include "graphcache/result.hpp"
#include "graphcache/topology.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gc {

enum class ResidencyTier : int {
  MetadataOnly = 1,
  PersistentStorage = 2,
  HostResident = 3,
  BackendResident = 4
};

[[nodiscard]] const char* to_string(ResidencyTier) noexcept;

// ---- Policies ----
struct ResidencyPolicy {
  std::uint32_t max_backend_resident{64};
  std::uint64_t backend_budget_bytes{static_cast<std::uint64_t>(1) << 40};
  std::uint64_t host_budget_bytes{static_cast<std::uint64_t>(1) << 40};
  bool demote_on_evict{true};
};

struct EvictionPolicy {
  bool lru{true};
  bool cost_aware{true};
  bool recapture_cost_aware{true};
  std::uint32_t tie_break_seed{0x9E3779B9u};
};

struct GraphCacheConfig {
  BackendKind backend_kind{BackendKind::Cpu};
  std::string namespace_name{""};
  GraphCompatibilityPolicy compatibility_policy;
  bool allow_capture{true};
  bool enable_persistence{false};
  std::string persistence_dir;
  ResidencyPolicy residency;
  EvictionPolicy eviction;
  std::size_t event_log_capacity{4096};
  std::uint64_t max_metadata_entries{100000};
  std::uint32_t concurrency_hint{8};
  Clock* clock{nullptr};  // null => system clock
};

// ---- Requests / results ----
struct GraphLookupRequest {
  WorkloadIdentity workload;
  BackendIdentity backend;
  RuntimeDescriptor runtime;
  DeviceDescriptor device;
  GraphDescriptor descriptor;   // requested topology + specialization
  GraphCompatibilityPolicy policy;
  bool allow_capture{true};
  std::string graph_name;
};

enum class LookupOutcome : int {
  ExactBackendResidentHit = 1,
  ExactHostHit = 2,
  CompatibleHitRebinding = 3,
  CompatibleDynamicShapeHit = 4,
  MissCaptureRequired = 5,
  MissIncompatible = 6,
  MissInvalidated = 7,
  MissStaleDependency = 8,
  MissCorruptPersistence = 9,
  MissResidencyPressure = 10,
  MissPolicyRejected = 11
};

[[nodiscard]] const char* to_string(LookupOutcome) noexcept;

class GraphLease {
 public:
  GraphLease() = default;
  GraphLease(const GraphLease&) = delete;
  GraphLease& operator=(const GraphLease&) = delete;
  GraphLease(GraphLease&& o) noexcept;
  GraphLease& operator=(GraphLease&& o) noexcept;
  ~GraphLease();

  // Release is idempotent; duplicate release cannot underflow.
  void release() noexcept;
  [[nodiscard]] bool valid() const noexcept { return entry_ != nullptr; }
  [[nodiscard]] const GraphArtifactId& artifact_id() const noexcept { return artifact_id_; }
  [[nodiscard]] const GraphGeneration& generation() const noexcept { return generation_; }
  [[nodiscard]] const std::shared_ptr<void>& executable() const noexcept { return executable_; }
  [[nodiscard]] const GraphDescriptor& descriptor() const noexcept { return *descriptor_; }
  [[nodiscard]] bool needs_rebinding() const noexcept { return needs_rebinding_; }
  void set_needs_rebinding(bool v) noexcept { needs_rebinding_ = v; }

  // Entry is an internal lease token; forward-declared publicly so the
  // implementation translation unit may define it. It is incomplete to users.
  struct Entry;

 private:
  friend class GraphCache;
  GraphLease(std::shared_ptr<Entry> entry, GraphArtifactId id, GraphGeneration gen,
             std::shared_ptr<void> exec, std::shared_ptr<const GraphDescriptor> desc, bool rebind);
  std::shared_ptr<Entry> entry_;
  GraphArtifactId artifact_id_;
  GraphGeneration generation_;
  std::shared_ptr<void> executable_;
  std::shared_ptr<const GraphDescriptor> descriptor_;
  bool needs_rebinding_{false};
};

struct GraphLookupResult {
  LookupOutcome outcome{LookupOutcome::MissCaptureRequired};
  GraphArtifactId artifact_id;
  GraphGeneration generation;
  GraphCompatibilityDecision decision;
  std::shared_ptr<GraphLease> lease;      // set when hit
  std::vector<CompatibilityReason> reasons;  // structured explanation
  std::string reason_text;                 // text explanation
  bool capture_started{false};             // a single-flight capture was joined/started
  bool waited_on_capture{false};           // this lookup waited on an existing capture

  [[nodiscard]] bool hit() const noexcept { return lease != nullptr; }
};

struct GraphReplayRequest {
  GraphGeneration expected_generation;
  std::shared_ptr<GraphLease> lease;
  ReplayBuffers buffers;
  ReplayDescriptor descriptor;
};

// A reservation records that a capture attempt owns creation of one graph
// generation. It is single-flight-scoped and never grants replay eligibility.
struct GraphReservation {
  GraphArtifactId artifact_id;
  GraphGeneration generation;
  CaptureAttemptId attempt;
  [[nodiscard]] bool valid() const noexcept { return attempt.valid(); }
};

// ---- Invalidation ----
enum class InvalidationScope : int {
  ByGraphId = 1,
  ByArtifactId = 2,
  ByCompatibilityKey = 3,
  ByWorkload = 4,
  ByTopology = 5,
  ByBackend = 6,
  ByDependencyGeneration = 7,
  ByModelRevision = 8,
  ByNamespace = 9,
  ByAll = 10
};

struct InvalidationRequest {
  InvalidationScope scope{InvalidationScope::ByArtifactId};
  GraphId graph_id;
  GraphArtifactId artifact_id;
  GraphGeneration dependency_generation;
  WorkloadIdentity workload;
  BackendIdentity backend;
  std::string namespace_name;
  std::string model_revision;
  bool strong{false};  // strong requires immediate authoritative revocation
};

struct InvalidationResult {
  std::size_t invalidated{0};
  std::size_t still_leasing{0};
  GraphGeneration new_graph_generation{GraphGeneration::Invalid};
  CacheGeneration new_cache_generation{CacheGeneration::Invalid};
};

// ---- Observability ----
struct PerScopeMetrics {
  std::uint64_t lookups{0};
  std::uint64_t hits{0};
  std::uint64_t misses{0};
  std::uint64_t replays{0};
  std::uint64_t captures{0};
  std::uint64_t invalidations{0};
};

struct Metrics {
  // Counters (measured unless noted).
  std::uint64_t lookups{0};
  std::uint64_t exact_hits{0};
  std::uint64_t compatible_hits{0};
  std::uint64_t host_hits{0};
  std::uint64_t backend_resident_hits{0};
  std::uint64_t persisted_hits{0};
  std::uint64_t misses{0};
  std::uint64_t captures{0};
  std::uint64_t deduplicated_captures{0};
  std::uint64_t capture_failures{0};
  std::uint64_t validation_failures{0};
  std::uint64_t recaptures{0};
  std::uint64_t instantiations{0};
  std::uint64_t replays{0};
  std::uint64_t replay_failures{0};
  std::uint64_t invalidations{0};
  std::uint64_t evictions{0};
  std::uint64_t reloads{0};
  std::uint64_t backend_resident_count{0};
  std::uint64_t stale_authority_rejections{0};
  std::uint64_t corruption_count{0};
  std::uint64_t dependency_invalidations{0};
  std::uint64_t active_leases{0};
  std::uint64_t active_captures{0};
  std::uint64_t active_replays{0};
  std::uint64_t avoided_captures{0};
  // Latencies (microseconds; measured).
  std::uint64_t lookup_latency_us{0};
  std::uint64_t capture_latency_us{0};
  std::uint64_t instantiate_latency_us{0};
  std::uint64_t replay_latency_us{0};
  std::uint64_t persistence_latency_us{0};
  std::uint64_t recovery_latency_us{0};
  // Per-scope breakdowns (measured). Keys are workload logical name, device
  // architecture, and namespace name.
  std::map<std::string, PerScopeMetrics> per_workload;
  std::map<std::string, PerScopeMetrics> per_device;
  std::map<std::string, PerScopeMetrics> per_namespace;
};

struct Snapshot {
  std::uint64_t graphs_total{0};
  std::uint64_t graphs_by_lifecycle[22]{};
  std::uint64_t graphs_by_residency[5]{};
  std::uint64_t active_captures{0};
  std::uint64_t active_validations{0};
  std::uint64_t active_loads{0};
  std::uint64_t active_leases{0};
  std::uint64_t active_replays{0};
  Metrics metrics;
};

struct Event {
  std::uint64_t sequence{0};
  std::int64_t timestamp_ms{0};
  std::string type;      // e.g. "lookup.hit", "capture.start"
  std::string message;
  std::string detail_json;
};

struct Explain {
  GraphArtifactId artifact_id;
  GraphCompatibilityClass decision;
  bool hit{false};
  LookupOutcome outcome{LookupOutcome::MissCaptureRequired};
  std::string text;
  std::string json;
};

// ---- Persistence store (file-backed) ----
class PersistenceStore {
 public:
  PersistenceStore(std::string directory, std::uint32_t format_version = 1);
  ~PersistenceStore();

  PersistenceStore(const PersistenceStore&) = delete;
  PersistenceStore& operator=(const PersistenceStore&) = delete;
  PersistenceStore(PersistenceStore&&) noexcept;
  PersistenceStore& operator=(PersistenceStore&&) noexcept;

  // Versioned, checksummed, atomic (temp + rename) writes.
  [[nodiscard]] Result<void> put(const GraphArtifactId& id, const std::vector<std::uint8_t>& payload);
  [[nodiscard]] Result<std::vector<std::uint8_t>> get(const GraphArtifactId& id);
  [[nodiscard]] Result<void> remove(const GraphArtifactId& id);
  [[nodiscard]] std::vector<GraphArtifactId> list();
  [[nodiscard]] std::string directory() const;
  [[nodiscard]] std::uint32_t format_version() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---- The cache ----
class GraphCache {
 public:
  explicit GraphCache(GraphCacheConfig config);
  ~GraphCache();
  GraphCache(const GraphCache&) = delete;
  GraphCache& operator=(const GraphCache&) = delete;
  GraphCache(GraphCache&&) = delete;
  GraphCache& operator=(GraphCache&&) = delete;

  // Full lookup pipeline. May trigger single-flight capture when a miss is
  // capture-able. Thread-safe.
  [[nodiscard]] GraphLookupResult lookup(const GraphLookupRequest& req);

  // Replay an execution-ready graph under a lease. Thread-safe per lease.
  [[nodiscard]] Result<GraphReplayResult> replay(const GraphReplayRequest& req);

  // Invalidate. Immediately prevents new leases; existing leases drain unless
  // strong invalidation is requested (then revocation is authoritative).
  [[nodiscard]] InvalidationResult invalidate(const InvalidationRequest& req);
  InvalidationResult invalidate_all();

  // Pin/unpin a graph generation (prevents eviction).
  [[nodiscard]] Result<void> pin(const GraphArtifactId& id);
  [[nodiscard]] Result<void> unpin(const GraphArtifactId& id);

  // Eviction / residency control.
  [[nodiscard]] Result<void> evict(const GraphArtifactId& id);
  [[nodiscard]] Result<void> unload(const GraphArtifactId& id);

  // Persistence.
  [[nodiscard]] Result<void> persist_all();
  [[nodiscard]] Result<void> recover();

  // Observability.
  [[nodiscard]] Metrics metrics() const;
  [[nodiscard]] Snapshot snapshot() const;
  [[nodiscard]] Explain explain(const GraphLookupResult& result) const;
  [[nodiscard]] std::vector<Event> events(std::size_t max = 100) const;

  [[nodiscard]] std::size_t graph_count() const;

  // Impl is a private pimpl type; it is forward-declared here so the
  // implementation translation unit may define it. It is never complete to
  // library consumers.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

} // namespace gc
