# Validation

Validation stages include topology integrity, node/edge consistency, dependency
validation, artifact integrity, backend compatibility, runtime compatibility,
architecture compatibility, executable dependency generation checks, binding
schema validation, backend load/instantiate validation, execution smoke, and
deterministic reference comparison where possible. Test suites cover unit,
adversarial, concurrency, persistence/recovery, and fixed-seed property
invariants.
## Concurrency and deadlock / reentrancy audit

Graph Cache uses a registry (shared_mutex) and per-entry mutex, plus an
atomic lease counter. The audit enforces:

- A read lock is never upgraded to a write lock on the same mutex; candidate
  selection runs under a shared registry lock, and backend materialization
  (which may capture) runs only after that shared lock is released.
- Backend capture / instantiate / launch / destroy is never performed while a
  central registry lock is held; it runs under the per-entry mutex only.
- Single-flight capture waits on a dedicated condition_variable guarded by its
  own mutex, never while holding the registry or a per-entry mutex.
- Lease release decrements atomically and invokes no writer callbacks while a
  lock is held (it runs a captured std::function that only updates counters).
- Metrics / events use dedicated mutexes and are never acquired while a registry
  lock is held.
- No worker thread is joined while holding state it needs to exit.
- The lock acquire order is strictly: registry (shared/unique) -> per-entry.
## Explainability

Operators can answer the following, via the structured Explain / reasons fields
and the compatibility decision:

- **Why was this graph a hit?** The lookup's decision class is ExactCompatible
  (or CompatibleWith*), and the Explain json carries the outcome, artifact,
  generation, decision class, and any compatible reasons.
- **Why was it a miss?** LookupResult.outcome gives MissIncompatible /
  MissStaleDependency / MissInvalidated / MissCaptureRequired / etc., and the
  reasons list names the failing field.
- **Why graph A over graph B?** Candidates are ranked by the best compatible
  decision; ExactCompatible is preferred over CompatibleWith*.
- **Why compatible / incompatible, and which field failed?** Each CompatibilityReason
  carries a code and a field (e.g., datatype, layout, alignment, topology,
  dependency, binding) plus wording; decide_compatibility records the first/prime
  failing class.
- **Why was recapture required?** Search found no replay-eligible candidate for a
  captured (or invalidated) workload, or a recovered entry must be recaptured.
- **Why was rebinding allowed / rejected?** Binding class and rebinding_eligible
  determine it; an ImmutableBinding or RecaptureRequiredBinding change is always
  rejected as IncompatibleMemoryBinding.
- **Why was this graph invalidated / which dependency made it stale?**
  InvalidationResult reports invalidated counts; is_dependency_fresh identifies a
  correctness-contributing dependency whose generation/digest/ABI changed
  (MissStaleDependency).
- **Why backend-resident / evicted?** ResidencyPolicy and the cost-aware scorer
  decide residency; eviction reasons are recorded as Events.
- **Why did a request wait on an existing capture?** LookupResult.waited_on_capture
  is set when a single-flight capture was joined rather than started.
- **Why was a stale capture / replay / recovery recapture rejected?** The
  generation-authority checks return StaleEpoch / StaleWorkerBoot /
  StaleCacheGeneration / StaleGraphGeneration / StaleCaptureAttempt /
  StaleReplayAttempt structured statuses.
- **Why was a persisted graph rejected as corrupt?** PersistenceStore rejects
  with PersistenceCorrupt / PersistenceTruncated / PersistenceUnknownVersion /
  PersistenceTrailingGarbage, and recover increments the corruption metric.
