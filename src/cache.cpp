#include "graphcache/cache.hpp"
#include "graphcache/serialization.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <sstream>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace gc {

namespace detail {

struct CacheEntry {
  GraphArtifactId artifact_id;
  GraphId graph_id;
  GraphGeneration generation;
  CacheGeneration cache_generation;
  GraphDescriptor descriptor;              // canonical + finalized, immutable
  CompatibilityFacts facts;
  GraphCompatibilityKey key;
  WorkloadIdentity workload;
  std::atomic<GraphLifecycle> lifecycle{GraphLifecycle::Valid};
  std::atomic<ResidencyTier> residency{ResidencyTier::MetadataOnly};
  std::shared_ptr<void> exec;
  std::shared_ptr<const GraphDescriptor> desc_view;
  std::atomic<std::uint64_t> pin_count{0};
  std::atomic<std::uint64_t> lease_count{0};
  std::atomic<std::uint64_t> active_replays{0};
  std::atomic<std::uint64_t> reuse_count{0};
  std::atomic<std::int64_t> last_access_ms{0};
  std::atomic<std::uint64_t> capture_cost_us{0};
  std::atomic<std::uint64_t> instantiate_cost_us{0};
  std::atomic<std::uint64_t> artifact_size{0};
  std::uint64_t dependency_generation{0};
  bool invalidated{false};
  std::atomic<bool> persisted_flag{false};
  std::mutex entry_mutex;   // guards exec handle + residency transitions
};

} // namespace detail

using detail::CacheEntry;

struct GraphLease::Entry {
  // The cache entry is retained for the lease duration so any backend-resident
  // executable stays alive until release. The callback defers accounting to the
  // engine and keeps the lease independent of the engine implementation type.
  std::shared_ptr<CacheEntry> keep_alive;
  std::function<void()> release_fn;
  std::atomic<bool> released{false};
};

} // namespace gc
namespace gc {

namespace {
GraphId derive_graph_id(const GraphDescriptor& desc, const WorkloadIdentity& wl,
                        const GraphCompatibilityKey& key) {
  Sha256 h;
  h.update(wl.logical_name);
  h.update("|");
  h.update(desc.artifact_id.to_string());
  h.update("|");
  h.update(key.digest_hex());
  auto d = h.final();
  return GraphId((static_cast<std::uint64_t>(d[0]) << 56) | (static_cast<std::uint64_t>(d[1]) << 48) |
                 (static_cast<std::uint64_t>(d[2]) << 40) | (static_cast<std::uint64_t>(d[3]) << 32) |
                 (static_cast<std::uint64_t>(d[4]) << 24) | (static_cast<std::uint64_t>(d[5]) << 16) |
                 (static_cast<std::uint64_t>(d[6]) << 8) | static_cast<std::uint64_t>(d[7]),
                 (static_cast<std::uint64_t>(d[8]) << 56) | (static_cast<std::uint64_t>(d[9]) << 48) |
                 (static_cast<std::uint64_t>(d[10]) << 40) | (static_cast<std::uint64_t>(d[11]) << 32) |
                 (static_cast<std::uint64_t>(d[12]) << 24) | (static_cast<std::uint64_t>(d[13]) << 16) |
                 (static_cast<std::uint64_t>(d[14]) << 8) | static_cast<std::uint64_t>(d[15]));
}

CompatibilityFacts make_facts(const GraphLookupRequest& req) {
  CompatibilityFacts f;
  f.workload = req.workload;
  f.backend = req.backend;
  f.runtime = req.runtime;
  f.device = req.device;
  f.capture_mode = req.descriptor.sync.capture_mode;
  f.stream_semantics = req.descriptor.sync.stream_semantics;
  f.topology_canonical = canonical_topology(req.descriptor.nodes, req.descriptor.edges);
  std::uint64_t align = 128;
  bool have_align = false;
  for (const auto& n : req.descriptor.nodes) {
    if (n.datatype != Datatype::None) {
      f.datatypes.push_back(n.datatype);
      f.layouts.push_back(n.layout);
      f.input_shapes.push_back(n.shape);
      if (n.scalar.specialized) f.scalar = n.scalar;
      if (!have_align || n.required_alignment > align) align = n.required_alignment;
      have_align = true;
      f.memory_binding_schema = n.binding.memory_binding_schema;
      f.binding = n.binding;
    }
    if (n.quantization.mode != QuantizationConfig::Mode::None) f.quantization = n.quantization;
  }
  f.required_alignment = align;
  f.dependencies = req.descriptor.dependencies;
  f.model_operator_revision = req.workload.model_operator_revision;
  f.policy_generation = req.workload.policy_generation;
  return f;
}

std::string workload_key(const WorkloadIdentity& wl) {
  return wl.logical_name + "|" + wl.namespace_name;
}

CompatibilityFacts facts_from_descriptor_and_workload(const GraphDescriptor& desc,
                                                      const WorkloadIdentity& wl) {
  CompatibilityFacts f;
  f.workload = wl;
  f.backend = desc.backend;
  f.runtime = desc.runtime;
  f.device = desc.device;
  f.capture_mode = desc.sync.capture_mode;
  f.stream_semantics = desc.sync.stream_semantics;
  f.topology_canonical = canonical_topology(desc.nodes, desc.edges);
  std::uint64_t align = 128;
  bool have_align = false;
  for (const auto& n : desc.nodes) {
    if (n.datatype != Datatype::None) {
      f.datatypes.push_back(n.datatype);
      f.layouts.push_back(n.layout);
      f.input_shapes.push_back(n.shape);
      if (n.scalar.specialized) f.scalar = n.scalar;
      if (!have_align || n.required_alignment > align) align = n.required_alignment;
      have_align = true;
      f.memory_binding_schema = n.binding.memory_binding_schema;
      f.binding = n.binding;
    }
    if (n.quantization.mode != QuantizationConfig::Mode::None) f.quantization = n.quantization;
  }
  f.required_alignment = align;
  f.dependencies = desc.dependencies;
  f.model_operator_revision = wl.model_operator_revision;
  f.policy_generation = wl.policy_generation;
  return f;
}

bool is_dependency_fresh(const CacheEntry& e, const CompatibilityFacts& request) {
  for (const auto& dep : request.dependencies) {
    if (!dep.contributes_to_correctness) continue;
    // Find the matching captured dependency.
    for (const auto& cd : e.facts.dependencies) {
      if (cd.name == dep.name && cd.contributes_to_correctness) {
        if (cd.generation != dep.generation ||
            cd.content_digest != dep.content_digest ||
            cd.abi != dep.abi) {
          return false;
        }
      }
    }
  }
  return true;
}

std::string shape_summary(const GraphCompatibilityDecision& d) {
  std::string s;
  for (const auto& r : d.reasons) {
    if (!s.empty()) s += "; ";
    s += std::string(to_string(r.code)) + "(" + r.field + ")";
  }
  return s;
}

} // namespace

struct GraphCache::Impl {
  GraphCacheConfig config;
  std::unique_ptr<GraphBackend> backend;
  std::unique_ptr<PersistenceStore> store;
  Clock* clock_ptr{nullptr};

  mutable std::shared_mutex registry_mutex;
  std::unordered_map<GraphArtifactId, std::shared_ptr<CacheEntry>> by_artifact;
  std::unordered_map<std::string, std::vector<std::shared_ptr<CacheEntry>>> by_key;
  std::unordered_map<std::string, std::vector<std::shared_ptr<CacheEntry>>> by_workload;
  std::unordered_map<std::string, GraphGeneration> key_current_gen;
  std::unordered_set<std::string> invalidated_keys;

  // Single-flight capture.
  std::mutex capture_mutex;
  struct CaptureAttempt {
    std::mutex m;
    std::condition_variable cv;
    bool done{false};
    Result<GraphArtifactId> res{GraphArtifactId{}};
    std::uint32_t waiters{0};
  };
  std::unordered_map<std::string, std::shared_ptr<CaptureAttempt>> inflight;

  // Generation authority.
  std::atomic<std::uint64_t> capture_counter{0};
  std::atomic<std::uint64_t> graph_generation_counter{1};
  std::atomic<std::uint64_t> cache_generation{1};
  std::atomic<std::uint64_t> worker_id{1};
  std::atomic<std::uint64_t> worker_boot_id{1};
  std::atomic<std::uint64_t> coordinator_epoch{1};

  // Leases / activity.
  std::atomic<std::uint64_t> active_leases{0};
  std::atomic<std::uint64_t> active_captures{0};
  std::atomic<std::uint64_t> active_replays{0};

  // Metrics / events.
  mutable std::mutex metrics_mutex;
  Metrics metrics;
  std::deque<Event> events;
  mutable std::mutex events_mutex;
  std::uint64_t event_seq{0};

  explicit Impl(GraphCacheConfig cfg)
      : config(std::move(cfg)), clock_ptr(config.clock) {
    backend = create_backend(config.backend_kind);
    if (!backend) {
      throw std::runtime_error("GraphCache: requested backend is unavailable");
    }
    if (config.enable_persistence) {
      store = std::make_unique<PersistenceStore>(config.persistence_dir);
    }
  }

  std::int64_t now_ms() const {
    if (clock_ptr) {
      auto n = clock_ptr->now();
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 n.time_since_epoch()).count();
    }
    return wall_unix_ms();
  }
  std::uint64_t now_us() const {
    if (clock_ptr) {
      auto n = clock_ptr->now();
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                 n.time_since_epoch()).count());
    }
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                 std::chrono::steady_clock::now().time_since_epoch()).count());
  }

  void record_event(std::string type, std::string msg, std::string detail_json = "") {
    std::lock_guard<std::mutex> lock(events_mutex);
    events.push_back(Event{++event_seq, now_ms(), std::move(type), std::move(msg), std::move(detail_json)});
    if (events.size() > config.event_log_capacity) events.pop_front();
  }

  void touch_metric(auto&& mutator) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    mutator(metrics);
  }

  void bump_scopes(const CompatibilityFacts& facts, auto&& fn) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    fn(metrics.per_workload[facts.workload.logical_name]);
    fn(metrics.per_device[facts.device.architecture]);
    fn(metrics.per_namespace[facts.workload.namespace_name]);
  }

  void bump_scope_raw(const std::string& wl, const std::string& dev, const std::string& ns, auto&& fn) {
    std::lock_guard<std::mutex> lock(metrics_mutex);
    fn(metrics.per_workload[wl]);
    fn(metrics.per_device[dev]);
    fn(metrics.per_namespace[ns]);
  }

  struct SpawnResult {
    Result<GraphArtifactId> res;
    bool owned{false};
  };

  SpawnResult run_single_flight(const std::string& keyhex, std::function<Result<GraphArtifactId>()> fn);
  Result<GraphArtifactId> capture_graph(const GraphLookupRequest& req, CaptureAttemptId attempt);
  GraphLookupResult search_candidates(const CompatibilityFacts& facts,
                                      const GraphCompatibilityPolicy& policy,
                                      const std::string& keyhex);
  void finish_hit(GraphLookupResult& res, const GraphLookupRequest& req, std::uint64_t start_us);
  std::string miss_reason(const GraphLookupResult& res, const CompatibilityFacts& facts,
                          const GraphCompatibilityPolicy& policy, const std::string& keyhex);
  std::shared_ptr<GraphLease> make_lease(const std::shared_ptr<CacheEntry>& e, bool needs_rebind);
  Result<void> materialize_exec(const std::shared_ptr<CacheEntry>& e);
  Result<void> persist_entry(const std::shared_ptr<CacheEntry>& e);
  Result<std::shared_ptr<CacheEntry>> load_entry(const GraphArtifactId& id);
};
} // namespace gc

namespace gc {

GraphLease::GraphLease(std::shared_ptr<Entry> entry, GraphArtifactId id, GraphGeneration gen,
                       std::shared_ptr<void> exec, std::shared_ptr<const GraphDescriptor> desc,
                       bool rebind)
    : entry_(std::move(entry)), artifact_id_(id), generation_(gen), executable_(std::move(exec)),
      descriptor_(std::move(desc)), needs_rebinding_(rebind) {}

GraphLease::GraphLease(GraphLease&& o) noexcept
    : entry_(std::move(o.entry_)), artifact_id_(o.artifact_id_), generation_(o.generation_),
      executable_(std::move(o.executable_)), descriptor_(std::move(o.descriptor_)),
      needs_rebinding_(o.needs_rebinding_) {
  o.entry_ = nullptr;
}
GraphLease& GraphLease::operator=(GraphLease&& o) noexcept {
  if (this != &o) {
    release();
    entry_ = std::move(o.entry_);
    artifact_id_ = o.artifact_id_;
    generation_ = o.generation_;
    executable_ = std::move(o.executable_);
    descriptor_ = std::move(o.descriptor_);
    needs_rebinding_ = o.needs_rebinding_;
    o.entry_ = nullptr;
  }
  return *this;
}
GraphLease::~GraphLease() { release(); }

void GraphLease::release() noexcept {
  if (!entry_) return;
  bool expected = false;
  if (entry_->released.compare_exchange_strong(expected, true)) {
    if (entry_->release_fn) entry_->release_fn();
  }
  entry_.reset();
}

} // namespace gc
namespace gc {

namespace {
constexpr std::uint16_t P_DESC = 1;
constexpr std::uint16_t P_MD = 2;

void encode_entry_payload(const detail::CacheEntry& e, std::vector<std::uint8_t>& out) {
  std::vector<std::uint8_t> desc_bytes;
  [[maybe_unused]] auto _sd = serialize_descriptor(e.descriptor, desc_bytes);
  std::vector<std::uint8_t> md_bytes;
  PersistedMetadata md;
  md.artifact_id = e.artifact_id;
  md.generation = e.generation;
  md.cache_generation = e.cache_generation;
  md.workload = e.workload;
  md.artifact_size = e.artifact_size.load();
  md.capture_cost_us = e.capture_cost_us.load();
  md.instantiate_cost_us = e.instantiate_cost_us.load();
  md.dependency_generation = e.dependency_generation;
  md.invalidated = e.invalidated;
  [[maybe_unused]] auto _sm = serialize_metadata(md, md_bytes);
  CanonicalWriter w;
  w.put_bytes(P_DESC, desc_bytes);
  w.put_bytes(P_MD, md_bytes);
  out = w.take();
}
} // namespace

GraphCache::GraphCache(GraphCacheConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
GraphCache::~GraphCache() = default;

std::size_t GraphCache::graph_count() const {
  std::shared_lock lk(impl_->registry_mutex);
  return impl_->by_artifact.size();
}

GraphLookupResult GraphCache::lookup(const GraphLookupRequest& req) {
  const auto start = impl_->now_us();
  impl_->touch_metric([&](Metrics& m) { m.lookups++; });
  GraphLookupResult res;
  CompatibilityFacts facts = make_facts(req);
  auto keyres = GraphCompatibilityKey::build(facts);
  if (!keyres.ok()) {
    res.outcome = LookupOutcome::MissPolicyRejected;
    res.reason_text = "cannot build compatibility key: " + keyres.error().message;
    return res;
  }
  const std::string keyhex = keyres.value().digest_hex();
  impl_->bump_scopes(facts, [](auto& m) { m.lookups++; });

  GraphCompatibilityPolicy policy = req.policy;

  auto cand = impl_->search_candidates(facts, policy, keyhex);
  if (cand.hit()) {
    impl_->finish_hit(cand, req, start);
    return cand;
  }
  res = cand;  // preserve the miss outcome/decision
  impl_->bump_scopes(facts, [](auto& m) { m.misses++; });
  res.reason_text = impl_->miss_reason(res, facts, policy, keyhex);

  const bool allowed = req.allow_capture && impl_->config.allow_capture;
  if (allowed && res.outcome == LookupOutcome::MissCaptureRequired) {
    auto attempt = CaptureAttemptId(impl_->capture_counter.fetch_add(1) + 1);
    auto spawned = impl_->run_single_flight(keyhex, [&] { return impl_->capture_graph(req, attempt); });
    res.capture_started = true;
    res.waited_on_capture = !spawned.owned;
    if (spawned.res.ok()) {
      auto again = impl_->search_candidates(facts, policy, keyhex);
      if (again.hit()) {
        impl_->finish_hit(again, req, start);
        again.capture_started = true;
        again.waited_on_capture = !spawned.owned;
        again.reason_text = "captured and replayed-compatible; hit";
        return again;
      }
      res.outcome = LookupOutcome::MissIncompatible;
      res.reason_text = "capture completed but no compatible candidate is replay-eligible";
    } else {
      res.outcome = LookupOutcome::MissCaptureRequired;
      res.reason_text = "capture failed: " + spawned.res.error().message;
      impl_->touch_metric([&](Metrics& m) { m.capture_failures++; });
    }
  }

  const auto elapsed = impl_->now_us() - start;
  impl_->touch_metric([&](Metrics& m) { m.lookup_latency_us = elapsed; });
  impl_->record_event(res.hit() ? "lookup.hit" : "lookup.miss",
                      res.hit() ? "hit" : "miss", "\"outcome\":\"" + std::string(to_string(res.outcome)) + "\"");
  return res;
}

Result<GraphReplayResult> GraphCache::replay(const GraphReplayRequest& req) {
  if (!req.lease || !req.lease->valid()) {
    return Result<GraphReplayResult>::failure(Error(ErrorCode::LeaseNotHeld, "replay called with no valid lease"));
  }
  if (req.expected_generation.valid() && req.lease->generation() != req.expected_generation) {
    return Result<GraphReplayResult>::failure(Error(ErrorCode::GenerationMismatch, "lease generation != expected generation"));
  }
  std::shared_ptr<detail::CacheEntry> entry = req.lease->entry_->keep_alive;
  if (!entry) return Result<GraphReplayResult>::failure(Error(ErrorCode::LeaseNotHeld, "lease lost its entry"));
  if (!impl_->backend) return Result<GraphReplayResult>::failure(Error(ErrorCode::BackendUnavailable, "no backend"));
  if (!req.lease->executable()) return Result<GraphReplayResult>::failure(Error(ErrorCode::NotResident, "lease executable not backend-resident"));

  impl_->active_replays.fetch_add(1);
  entry->active_replays.fetch_add(1);
  const auto start = impl_->now_us();
  auto replay = impl_->backend->replay(entry->descriptor, req.lease->executable(), req.buffers, req.descriptor);
  const auto elapsed = impl_->now_us() - start;
  entry->active_replays.fetch_sub(1);
  impl_->active_replays.fetch_sub(1);
  if (!replay.ok()) {
    impl_->touch_metric([&](Metrics& m) { m.replay_failures++; });
    return Result<GraphReplayResult>::failure(replay.error());
  }
  entry->reuse_count.fetch_add(1);
  impl_->bump_scope_raw(entry->workload.logical_name, entry->descriptor.device.architecture,
                        entry->workload.namespace_name, [](auto& m) { m.replays++; });
  impl_->touch_metric([&](Metrics& m) { m.replays++; m.replay_latency_us = elapsed; });
  impl_->record_event("replay", "ok", "\"nodes\":\"" + std::to_string(replay.value().replayed_nodes) + "\"");
  return Result<GraphReplayResult>::success(std::move(replay.value()));
}

} // namespace gc
namespace gc {

GraphCache::Impl::SpawnResult GraphCache::Impl::run_single_flight(
    const std::string& keyhex, std::function<Result<GraphArtifactId>()> fn) {
  std::shared_ptr<CaptureAttempt> attempt;
  bool owned = false;
  {
    std::lock_guard<std::mutex> lock(capture_mutex);
    auto it = inflight.find(keyhex);
    if (it != inflight.end()) {
      attempt = it->second;
      attempt->waiters++;
    } else {
      attempt = std::make_shared<CaptureAttempt>();
      inflight[keyhex] = attempt;
      owned = true;
    }
  }
  if (owned) {
    active_captures.fetch_add(1);
    auto res = fn();
    {
      std::unique_lock<std::mutex> lock(attempt->m);
      attempt->res = res;
      attempt->done = true;
      attempt->cv.notify_all();
    }
    {
      std::lock_guard<std::mutex> lock(capture_mutex);
      inflight.erase(keyhex);
    }
    active_captures.fetch_sub(1);
    return SpawnResult{std::move(res), true};
  }
  {
    std::unique_lock<std::mutex> lock(attempt->m);
    attempt->cv.wait(lock, [&] { return attempt->done; });
  }
  return SpawnResult{attempt->res, false};
}

Result<GraphArtifactId> GraphCache::Impl::capture_graph(const GraphLookupRequest& req,
                                                        CaptureAttemptId attempt) {
  const auto start = now_us();
  const std::uint64_t gen_at_start = cache_generation.load();
  GraphDescriptor desc = req.descriptor;
  auto fin = desc.finalize();
  if (!fin.ok()) {
    return Result<GraphArtifactId>::failure(Error(ErrorCode::InvalidArgument, "descriptor finalize failed"));
  }
  auto topologycheck = desc.validate_topology();
  if (!topologycheck.ok()) {
    return Result<GraphArtifactId>::failure(topologycheck.error());
  }
  auto execres = backend->capture(desc, attempt);
  if (!execres.ok()) {
    touch_metric([](Metrics& m) { m.capture_failures++; });
    return Result<GraphArtifactId>::failure(execres.error());
  }
  auto exec = execres.value();
  auto val = backend->validate(desc, exec);
  if (!val.ok()) {
    touch_metric([](Metrics& m) { m.validation_failures++; });
    return Result<GraphArtifactId>::failure(Error(ErrorCode::ValidationFailed, val.error().message));
  }
  auto entry = std::make_shared<CacheEntry>();
  entry->artifact_id = desc.artifact_id;
  entry->descriptor = std::move(desc);
  entry->desc_view = std::make_shared<const GraphDescriptor>(entry->descriptor);
  entry->facts = make_facts(req);
  entry->workload = req.workload;
  auto kb = GraphCompatibilityKey::build(entry->facts);
  entry->key = kb.ok() ? kb.value() : GraphCompatibilityKey{};
  entry->graph_id = derive_graph_id(entry->descriptor, req.workload, entry->key);
  entry->generation = GraphGeneration(graph_generation_counter.fetch_add(1));
  entry->cache_generation = CacheGeneration(cache_generation.load());
  entry->lifecycle = GraphLifecycle::Valid;
  entry->residency = ResidencyTier::HostResident;
  entry->exec = exec;
  entry->capture_cost_us = now_us() - start;
  entry->artifact_size = entry->descriptor.semantic_canonical.size() + 256;
  std::uint64_t dg = 0;
  for (const auto& d : entry->descriptor.dependencies) {
    if (d.generation.value > dg) dg = d.generation.value;
  }
  entry->dependency_generation = dg;
  entry->last_access_ms = now_ms();

  const std::string keyhex = entry->key.digest_hex();
  {
    std::unique_lock<std::mutex> lock(capture_mutex);
    // A stale capture must not publish: generation rolled or key invalidated.
    if (cache_generation.load() != gen_at_start) {
      return Result<GraphArtifactId>::failure(Error(ErrorCode::StaleCompletion, "cache generation rolled during capture"));
    }
  }
  {
    std::unique_lock<std::shared_mutex> lk(registry_mutex);
    by_artifact[entry->artifact_id] = entry;
    by_key[keyhex].push_back(entry);
    by_workload[workload_key(entry->workload)].push_back(entry);
    key_current_gen[keyhex] = entry->generation;
    invalidated_keys.erase(keyhex);
  }
  if (store) {
    auto p = persist_entry(entry);
    if (p.ok()) entry->persisted_flag = true;
  }
  touch_metric([](Metrics& m) { m.captures++; });
  bump_scope_raw(entry->workload.logical_name, entry->descriptor.device.architecture,
                 entry->workload.namespace_name, [](auto& m) { m.captures++; });
  record_event("capture.complete", "graph captured", "\"artifact\":\"" + entry->artifact_id.to_string() + "\"");
  return Result<GraphArtifactId>::success(entry->artifact_id);
}

GraphLookupResult GraphCache::Impl::search_candidates(const CompatibilityFacts& facts,
                                                      const GraphCompatibilityPolicy& policy,
                                                      const std::string& keyhex) {
  GraphLookupResult res;
  std::shared_ptr<CacheEntry> chosen;
  GraphCompatibilityDecision best;
  bool found = false;
  (void)keyhex;
  {
    std::shared_lock<std::shared_mutex> lk(registry_mutex);
    // Candidates are searched by workload identity so that a request with a
    // different specialization (datatype/layout/shape/binding) still compares
    // against the captured authority and yields a structured incompatible /
    // stale / rebindable decision rather than a bare "no such key" miss.
    auto it = by_workload.find(workload_key(facts.workload));
    if (it == by_workload.end()) {
      res.outcome = LookupOutcome::MissCaptureRequired;
      return res;
    }
    bool saw_incompatible = false;
    GraphCompatibilityDecision best_incompat;
    for (const auto& e : it->second) {
      if (e->invalidated) continue;
      auto cur = key_current_gen.find(e->key.digest_hex());
      if (cur == key_current_gen.end() || e->generation != cur->second) continue;  // current generation for its key
      if (!lifecycle_is_replay_eligible(e->lifecycle.load())) continue;
      if (!is_dependency_fresh(*e, facts)) {
        res.outcome = LookupOutcome::MissStaleDependency;
        continue;
      }
      auto dec = decide_compatibility(facts, e->facts, policy);
      if (dec.compatible()) {
        if (!found || dec.klass == GraphCompatibilityClass::ExactCompatible) {
          found = true;
          chosen = e;
          best = dec;
          res.decision = dec;
        }
      } else {
        saw_incompatible = true;
        best_incompat = dec;
      }
    }
    if (!found) {
      if (saw_incompatible) {
        res.outcome = LookupOutcome::MissIncompatible;
        if (res.decision.klass == GraphCompatibilityClass::NotACandidate) res.decision = best_incompat;
      } else if (res.outcome != LookupOutcome::MissStaleDependency) {
        res.outcome = LookupOutcome::MissCaptureRequired;
      }
      return res;
    }
    chosen->last_access_ms = now_ms();
    chosen->reuse_count.fetch_add(1);
  }
  // Materialize backend-resident (or recapture) state outside the registry lock.
  auto mat = materialize_exec(chosen);
  if (!mat.ok()) {
    res.outcome = LookupOutcome::MissCaptureRequired;
    res.reason_text = "cannot materialize graph executable: " + mat.error().message;
    return res;
  }
  {
    std::shared_lock<std::shared_mutex> lk(registry_mutex);
    if (chosen->invalidated || !lifecycle_is_replay_eligible(chosen->lifecycle.load())) {
      res.outcome = LookupOutcome::MissInvalidated;
      return res;
    }
  }
  res.artifact_id = chosen->artifact_id;
  res.generation = chosen->generation;
  res.lease = make_lease(chosen, best.needs_rebinding());
  switch (best.klass) {
    case GraphCompatibilityClass::ExactCompatible:
      res.outcome = (chosen->residency.load() == ResidencyTier::BackendResident)
                        ? LookupOutcome::ExactBackendResidentHit : LookupOutcome::ExactHostHit;
      break;
    case GraphCompatibilityClass::CompatibleWithRebinding:
      res.outcome = LookupOutcome::CompatibleHitRebinding;
      break;
    case GraphCompatibilityClass::CompatibleWithDynamicShapeConstraint:
      res.outcome = LookupOutcome::CompatibleDynamicShapeHit;
      break;
    default:
      res.outcome = LookupOutcome::ExactHostHit;
      break;
  }
  res.reasons = best.reasons;
  return res;
}

Result<void> GraphCache::Impl::materialize_exec(const std::shared_ptr<CacheEntry>& e) {
  std::lock_guard<std::mutex> lm(e->entry_mutex);
  if (e->exec) return Result<void>::success();
  auto attempt = CaptureAttemptId(capture_counter.fetch_add(1) + 1);
  auto cap = backend->capture(e->descriptor, attempt);
  if (!cap.ok()) return Result<void>::failure(cap.error());
  auto val = backend->validate(e->descriptor, cap.value());
  if (!val.ok()) return Result<void>::failure(Error(ErrorCode::ValidationFailed, val.error().message));
  e->exec = cap.value();
  e->residency = ResidencyTier::HostResident;
  touch_metric([](Metrics& m) { m.recaptures++; m.instantiations++; });
  return Result<void>::success();
}

std::shared_ptr<GraphLease> GraphCache::Impl::make_lease(const std::shared_ptr<CacheEntry>& e,
                                                         bool needs_rebind) {
  auto entry = std::make_shared<GraphLease::Entry>();
  entry->keep_alive = e;
  std::weak_ptr<CacheEntry> weak = e;
  entry->release_fn = [this, weak]() {
    if (auto sp = weak.lock()) {
      if (sp->lease_count.load() > 0) sp->lease_count.fetch_sub(1);
    }
    if (active_leases.load() > 0) active_leases.fetch_sub(1);
  };
  e->lease_count.fetch_add(1);
  active_leases.fetch_add(1);
  // Construct directly (not make_shared) so this member function, which is a
  // friend context, can invoke the private GraphLease constructor.
  return std::shared_ptr<GraphLease>(new GraphLease(entry, e->artifact_id, e->generation, e->exec,
                                                    e->desc_view, needs_rebind));
}

void GraphCache::Impl::finish_hit(GraphLookupResult& res, const GraphLookupRequest& req,
                                  std::uint64_t start_us) {
  const auto elapsed = now_us() - start_us;
  bump_scope_raw(req.workload.logical_name, req.device.architecture,
                 req.workload.namespace_name, [](auto& m) { m.hits++; });
  touch_metric([&](Metrics& m) {
    m.lookup_latency_us = elapsed;
    switch (res.outcome) {
      case LookupOutcome::ExactBackendResidentHit: m.exact_hits++; m.backend_resident_hits++; break;
      case LookupOutcome::ExactHostHit: m.exact_hits++; m.host_hits++; break;
      case LookupOutcome::CompatibleHitRebinding: m.compatible_hits++; break;
      case LookupOutcome::CompatibleDynamicShapeHit: m.compatible_hits++; break;
      default: break;
    }
  });
  record_event("lookup.hit", "hit",
               "\"outcome\":\"" + std::string(to_string(res.outcome)) + "\"");
}

std::string GraphCache::Impl::miss_reason(const GraphLookupResult& res,
                                          const CompatibilityFacts& /*facts*/,
                                          const GraphCompatibilityPolicy& /*policy*/,
                                          const std::string& /*keyhex*/) {
  switch (res.outcome) {
    case LookupOutcome::MissCaptureRequired: return "no replay-eligible candidate; capture required";
    case LookupOutcome::MissIncompatible: return "candidate exists but is incompatible";
    case LookupOutcome::MissStaleDependency: return "candidate dependency generation is stale";
    case LookupOutcome::MissInvalidated: return "candidate was invalidated";
    case LookupOutcome::MissCorruptPersistence: return "persisted candidate is corrupt";
    case LookupOutcome::MissResidencyPressure: return "no residency capacity";
    case LookupOutcome::MissPolicyRejected: return "rejected by compatibility policy";
    default: return "miss";
  }
}

Result<void> GraphCache::Impl::persist_entry(const std::shared_ptr<CacheEntry>& e) {
  std::vector<std::uint8_t> payload;
  encode_entry_payload(*e, payload);
  const auto start = now_us();
  auto res = store->put(e->artifact_id, payload);
  const auto elapsed = now_us() - start;
  touch_metric([&](Metrics& m) { m.persistence_latency_us = elapsed; });
  return res;
}

} // namespace gc
namespace gc {

const char* to_string(ResidencyTier t) noexcept {
  switch (t) {
    case ResidencyTier::MetadataOnly: return "MetadataOnly";
    case ResidencyTier::PersistentStorage: return "PersistentStorage";
    case ResidencyTier::HostResident: return "HostResident";
    case ResidencyTier::BackendResident: return "BackendResident";
  }
  return "MetadataOnly";
}
const char* to_string(LookupOutcome o) noexcept {
  switch (o) {
    case LookupOutcome::ExactBackendResidentHit: return "ExactBackendResidentHit";
    case LookupOutcome::ExactHostHit: return "ExactHostHit";
    case LookupOutcome::CompatibleHitRebinding: return "CompatibleHitRebinding";
    case LookupOutcome::CompatibleDynamicShapeHit: return "CompatibleDynamicShapeHit";
    case LookupOutcome::MissCaptureRequired: return "MissCaptureRequired";
    case LookupOutcome::MissIncompatible: return "MissIncompatible";
    case LookupOutcome::MissInvalidated: return "MissInvalidated";
    case LookupOutcome::MissStaleDependency: return "MissStaleDependency";
    case LookupOutcome::MissCorruptPersistence: return "MissCorruptPersistence";
    case LookupOutcome::MissResidencyPressure: return "MissResidencyPressure";
    case LookupOutcome::MissPolicyRejected: return "MissPolicyRejected";
  }
  return "MissCaptureRequired";
}

namespace {
bool decode_entry_payload(std::span<const std::uint8_t> payload,
                          detail::CacheEntry& e,
                          GraphCompatibilityKey& key_placeholder) {
  (void)key_placeholder;
  CanonicalReader r(payload);
  std::uint16_t tag;
  std::span<const std::uint8_t> p;
  bool have_desc = false, have_md = false;
  GraphDescriptor desc;
  PersistedMetadata md;
  while (r.next(tag, p)) {
    if (tag == P_DESC) {
      auto dres = deserialize_descriptor(p);
      if (!dres.ok()) return false;
      desc = std::move(dres.value());
      have_desc = true;
    } else if (tag == P_MD) {
      auto mres = deserialize_metadata(p);
      if (!mres.ok()) return false;
      md = std::move(mres.value());
      have_md = true;
    } else {
      return false;
    }
  }
  if (r.malformed() || !have_desc || !have_md) return false;
  e.artifact_id = md.artifact_id;
  e.generation = md.generation;
  e.cache_generation = md.cache_generation;
  e.workload = md.workload;
  e.descriptor = std::move(desc);
  e.desc_view = std::make_shared<const GraphDescriptor>(e.descriptor);
  e.facts = facts_from_descriptor_and_workload(e.descriptor, e.workload);
  auto kb = GraphCompatibilityKey::build(e.facts);
  e.key = kb.ok() ? kb.value() : GraphCompatibilityKey{};
  e.graph_id = derive_graph_id(e.descriptor, e.workload, e.key);
  e.artifact_size.store(md.artifact_size);
  e.capture_cost_us.store(md.capture_cost_us);
  e.instantiate_cost_us.store(md.instantiate_cost_us);
  e.dependency_generation = md.dependency_generation;
  e.invalidated = md.invalidated;
  e.lifecycle = md.invalidated ? GraphLifecycle::Invalidated : GraphLifecycle::Valid;
  e.residency = ResidencyTier::MetadataOnly;
  e.exec = nullptr;
  e.persisted_flag = true;
  return true;
}
} // namespace

Result<std::shared_ptr<detail::CacheEntry>> GraphCache::Impl::load_entry(const GraphArtifactId& id) {
  if (!store) return Result<std::shared_ptr<detail::CacheEntry>>::failure(Error(ErrorCode::OperationNotSupported, "persistence disabled"));
  auto data = store->get(id);
  if (!data.ok()) return Result<std::shared_ptr<detail::CacheEntry>>::failure(data.error());
  auto e = std::make_shared<CacheEntry>();
  if (!decode_entry_payload(data.value(), *e, e->key)) {
    touch_metric([](Metrics& m) { m.corruption_count++; });
    return Result<std::shared_ptr<detail::CacheEntry>>::failure(Error(ErrorCode::PersistenceCorrupt, "decoded entry rejected"));
  }
  return Result<std::shared_ptr<detail::CacheEntry>>::success(std::move(e));
}

} // namespace gc
namespace gc {

InvalidationResult GraphCache::invalidate(const InvalidationRequest& req) {
  const auto new_gen = CacheGeneration(impl_->cache_generation.fetch_add(1) + 1);
  InvalidationResult res;
  std::vector<std::shared_ptr<detail::CacheEntry>> targets;
  {
    std::unique_lock<std::shared_mutex> lk(impl_->registry_mutex);
    auto& by_art = impl_->by_artifact;
    switch (req.scope) {
      case InvalidationScope::ByAll:
        for (auto& [id, e] : by_art) targets.push_back(e);
        break;
      case InvalidationScope::ByArtifactId:
        if (auto it = by_art.find(req.artifact_id); it != by_art.end()) targets.push_back(it->second);
        break;
      case InvalidationScope::ByGraphId:
        for (auto& [id, e] : by_art) if (e->graph_id == req.graph_id) targets.push_back(e);
        break;
      case InvalidationScope::ByNamespace:
        for (auto& [id, e] : by_art) if (e->workload.namespace_name == req.namespace_name) targets.push_back(e);
        break;
      case InvalidationScope::ByWorkload:
        for (auto& [id, e] : by_art)
          if (e->workload.logical_name == req.workload.logical_name &&
              (req.namespace_name.empty() || e->workload.namespace_name == req.namespace_name))
            targets.push_back(e);
        break;
      case InvalidationScope::ByCompatibilityKey:
      case InvalidationScope::ByTopology:
        // Matched by workload identity (the primary discriminating authority).
        for (auto& [id, e] : by_art)
          if (e->workload.logical_name == req.workload.logical_name) targets.push_back(e);
        break;
      case InvalidationScope::ByModelRevision:
        for (auto& [id, e] : by_art)
          if (!e->workload.model_operator_revision.empty() &&
              e->workload.model_operator_revision == req.model_revision)
            targets.push_back(e);
        break;
      case InvalidationScope::ByBackend:
        for (auto& [id, e] : by_art)
          if (e->descriptor.backend.kind == req.backend.kind &&
              (req.backend.backend_name.empty() || e->descriptor.backend.backend_name == req.backend.backend_name))
            targets.push_back(e);
        break;
      case InvalidationScope::ByDependencyGeneration:
        for (auto& [id, e] : by_art)
          if (req.dependency_generation.valid() && e->dependency_generation != req.dependency_generation.value)
            targets.push_back(e);
        break;
    }
  }
  for (auto& e : targets) {
    if (e->invalidated) continue;
    e->invalidated = true;
    e->lifecycle = GraphLifecycle::Invalidated;
    impl_->invalidated_keys.insert(e->key.digest_hex());
    std::lock_guard<std::mutex> lm(e->entry_mutex);
    if (req.strong && e->lease_count.load() == 0 &&
        e->residency.load() == ResidencyTier::BackendResident) {
      auto u = impl_->backend->unload(e->descriptor, e->exec);
      if (u.ok()) e->residency = ResidencyTier::PersistentStorage;
    }
    if (e->lease_count.load() > 0) res.still_leasing++;
    res.invalidated++;
    impl_->graph_generation_counter.fetch_add(1);
    impl_->bump_scope_raw(e->workload.logical_name, e->descriptor.device.architecture,
                          e->workload.namespace_name, [](auto& m) { m.invalidations++; });
  }
  res.new_cache_generation = new_gen;
  impl_->touch_metric([&](Metrics& m) { m.invalidations += res.invalidated; });
  impl_->record_event("invalidate", std::to_string(res.invalidated) + " graphs invalidated", "");
  return res;
}

InvalidationResult GraphCache::invalidate_all() {
  InvalidationRequest req;
  req.scope = InvalidationScope::ByAll;
  return invalidate(req);
}

Result<void> GraphCache::pin(const GraphArtifactId& id) {
  std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
  auto it = impl_->by_artifact.find(id);
  if (it == impl_->by_artifact.end()) return Result<void>::failure(Error(ErrorCode::NoSuchArtifact, "no such artifact"));
  it->second->pin_count.fetch_add(1);
  return Result<void>::success();
}

Result<void> GraphCache::unpin(const GraphArtifactId& id) {
  std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
  auto it = impl_->by_artifact.find(id);
  if (it == impl_->by_artifact.end()) return Result<void>::failure(Error(ErrorCode::NoSuchArtifact, "no such artifact"));
  auto& e = it->second;
  std::uint64_t cur = e->pin_count.load();
  if (cur == 0) return Result<void>::failure(Error(ErrorCode::LeaseUnderflow, "unpin underflow"));
  e->pin_count.fetch_sub(1);
  return Result<void>::success();
}

Result<void> GraphCache::evict(const GraphArtifactId& id) {
  std::shared_ptr<detail::CacheEntry> e;
  {
    std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
    auto it = impl_->by_artifact.find(id);
    if (it == impl_->by_artifact.end()) return Result<void>::failure(Error(ErrorCode::NoSuchArtifact, "no such artifact"));
    e = it->second;
  }
  if (e->pin_count.load() > 0 || e->lease_count.load() > 0) {
    return Result<void>::failure(Error(ErrorCode::EvictionImpossible, "graph is pinned or leased"));
  }
  std::lock_guard<std::mutex> lm(e->entry_mutex);
  if (e->residency.load() == ResidencyTier::BackendResident) {
    auto u = impl_->backend->unload(e->descriptor, e->exec);
    if (u.ok()) e->residency = ResidencyTier::PersistentStorage;
  }
  impl_->touch_metric([](Metrics& m) { m.evictions++; });
  impl_->record_event("evict", "graph evicted", "\"artifact\":\"" + id.to_string() + "\"");
  return Result<void>::success();
}

Result<void> GraphCache::unload(const GraphArtifactId& id) {
  std::shared_ptr<detail::CacheEntry> e;
  {
    std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
    auto it = impl_->by_artifact.find(id);
    if (it == impl_->by_artifact.end()) return Result<void>::failure(Error(ErrorCode::NoSuchArtifact, "no such artifact"));
    e = it->second;
  }
  if (e->lease_count.load() > 0) return Result<void>::failure(Error(ErrorCode::NotResident, "cannot unload while leased"));
  std::lock_guard<std::mutex> lm(e->entry_mutex);
  if (e->residency.load() == ResidencyTier::BackendResident) {
    auto u = impl_->backend->unload(e->descriptor, e->exec);
    if (u.ok()) e->residency = ResidencyTier::PersistentStorage;
  }
  return Result<void>::success();
}

} // namespace gc
namespace gc {

Result<void> GraphCache::persist_all() {
  if (!impl_->store) return Result<void>::failure(Error(ErrorCode::OperationNotSupported, "persistence disabled"));
  std::vector<std::shared_ptr<detail::CacheEntry>> snap;
  {
    std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
    for (auto& [id, e] : impl_->by_artifact) snap.push_back(e);
  }
  for (auto& e : snap) {
    auto r = impl_->persist_entry(e);
    if (!r.ok()) return r;
    e->persisted_flag = true;
  }
  impl_->record_event("persist_all", std::to_string(snap.size()) + " entries persisted", "");
  return Result<void>::success();
}

Result<void> GraphCache::recover() {
  if (!impl_->store) return Result<void>::failure(Error(ErrorCode::OperationNotSupported, "persistence disabled"));
  const auto start = impl_->now_us();
  std::size_t recovered = 0;
  std::size_t corrupt = 0;
  // Remove orphan temp artifacts before scanning.
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(impl_->store->directory())) {
    const std::string name = entry.path().filename().string();
    if (name.find(".tmp") != std::string::npos) std::filesystem::remove(entry.path(), ec);
  }
  for (const auto& id : impl_->store->list()) {
    auto le = impl_->load_entry(id);
    if (!le.ok()) {
      ++corrupt;
      impl_->touch_metric([](Metrics& m) { m.corruption_count++; });
      impl_->record_event("recover.corrupt", "rejected corrupt entry " + id.to_string(),
                          "\"error\":\"" + le.error().message + "\"");
      continue;
    }
    auto e = le.value();
    // Backend residency is always absent after restart (never inherit old live
    // CUDA context/stream/graph executable authority).
    e->residency = ResidencyTier::MetadataOnly;
    e->exec = nullptr;
    std::unique_lock<std::shared_mutex> lk(impl_->registry_mutex);
    impl_->by_artifact[e->artifact_id] = e;
    impl_->by_key[e->key.digest_hex()].push_back(e);
    impl_->by_workload[workload_key(e->workload)].push_back(e);
    if (e->invalidated) {
      impl_->invalidated_keys.insert(e->key.digest_hex());
    } else {
      impl_->key_current_gen[e->key.digest_hex()] = e->generation;
    }
    ++recovered;
  }
  // Ensure generation counters are monotonic past recovered authority.
  std::uint64_t max_graph_gen = 0;
  std::uint64_t max_cache_gen = 0;
  {
    std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
    for (auto& [id, e] : impl_->by_artifact) {
      if (e->generation.value > max_graph_gen) max_graph_gen = e->generation.value;
      if (e->cache_generation.value > max_cache_gen) max_cache_gen = e->cache_generation.value;
    }
  }
  std::uint64_t g = impl_->graph_generation_counter.load();
  if (max_graph_gen + 1 > g) impl_->graph_generation_counter.store(max_graph_gen + 1);
  std::uint64_t c = impl_->cache_generation.load();
  if (max_cache_gen + 1 > c) impl_->cache_generation.store(max_cache_gen + 1);
  const auto elapsed = impl_->now_us() - start;
  impl_->touch_metric([&](Metrics& m) { m.recovery_latency_us = elapsed; });
  impl_->record_event("recover.complete",
                      std::to_string(recovered) + " recovered, " + std::to_string(corrupt) + " rejected", "");
  return Result<void>::success();
}

Metrics GraphCache::metrics() const {
  std::uint64_t resident = 0;
  {
    std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
    for (auto& [id, e] : impl_->by_artifact)
      if (e->residency.load() == ResidencyTier::BackendResident) ++resident;
  }
  std::lock_guard<std::mutex> lk(impl_->metrics_mutex);
  Metrics m = impl_->metrics;
  m.backend_resident_count = resident;
  m.active_leases = impl_->active_leases.load();
  m.active_captures = impl_->active_captures.load();
  m.active_replays = impl_->active_replays.load();
  return m;
}

Snapshot GraphCache::snapshot() const {
  Snapshot s;
  s.metrics = metrics();
  std::shared_lock<std::shared_mutex> lk(impl_->registry_mutex);
  for (auto& [id, e] : impl_->by_artifact) {
    s.graphs_total++;
    int li = static_cast<int>(e->lifecycle.load());
    if (li >= 0 && li < 22) s.graphs_by_lifecycle[li]++;
    int ri = static_cast<int>(e->residency.load());
    if (ri >= 0 && ri < 5) s.graphs_by_residency[ri]++;
  }
  s.active_captures = impl_->active_captures.load();
  s.active_validations = 0;
  s.active_loads = 0;
  s.active_leases = impl_->active_leases.load();
  s.active_replays = impl_->active_replays.load();
  return s;
}

namespace {
std::string escape_json(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}
} // namespace

Explain GraphCache::explain(const GraphLookupResult& res) const {
  Explain x;
  x.artifact_id = res.artifact_id;
  x.decision = res.decision.klass;
  x.hit = res.hit();
  x.outcome = res.outcome;
  x.text = res.reason_text;
  std::string j = "{\"outcome\":\"" + std::string(to_string(res.outcome)) +
                  "\",\"hit\":" + (res.hit() ? "true" : "false") +
                  ",\"artifact\":\"" + res.artifact_id.to_string() +
                  "\",\"generation\":" + std::to_string(res.generation.value) +
                  ",\"decision\":\"" + std::string(to_string(res.decision.klass)) + "\"";
  if (!res.reasons.empty()) {
    j += ",\"reasons\":[";
    for (std::size_t i = 0; i < res.reasons.size(); ++i) {
      if (i) j += ",";
      j += "{\"code\":\"" + std::string(to_string(res.reasons[i].code)) +
           "\",\"field\":\"" + escape_json(res.reasons[i].field) + "\"}";
    }
    j += "]";
  }
  j += "}";
  x.json = j;
  return x;
}

std::vector<Event> GraphCache::events(std::size_t max) const {
  std::lock_guard<std::mutex> lock(impl_->events_mutex);
  std::vector<Event> out;
  std::size_t start = impl_->events.size() > max ? impl_->events.size() - max : 0;
  for (std::size_t i = start; i < impl_->events.size(); ++i) out.push_back(impl_->events[i]);
  return out;
}

} // namespace gc
