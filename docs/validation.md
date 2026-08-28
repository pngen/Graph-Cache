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
