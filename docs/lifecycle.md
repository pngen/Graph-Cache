# Lifecycle

Each graph is guarded by an explicit state machine: Discovered, Capturing,
Captured, Validating, Valid, Persisting, Persisted, Loading, ResidentHost,
ResidentBackend, Leasing, Replaying, InvalidationPending, EvictionPending,
EvictedBackend, EvictedHost, Invalidated, Corrupt, Failed, Retired, Terminal.

Transitions are explicitly validated; illegal transitions are rejected.
lifecycle_is_replay_eligible is true only for Valid, Persisted, ResidentHost,
ResidentBackend, and Leasing. Invalid, corrupt, retired, or stale graphs are
never replay-eligible without a new capture/validation generation. Terminal
states absorb illegal transitions.

Graph artifacts become immutable once validated and published. Mutable
operational metadata (access counts, active leases, residency state, last replay
time) never alters graph identity. A recaptured or semantically changed graph
requires a new generation or artifact identity.