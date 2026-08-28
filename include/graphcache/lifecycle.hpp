#pragma once
// Graph lifecycle state machine. Transitions are explicit and validated.
// Invalid, corrupt, retired, or stale graphs are never replay-eligible without a
// new capture/validation generation.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include "graphcache/result.hpp"

namespace gc {

enum class GraphLifecycle : int {
  Discovered = 1,
  Capturing = 2,
  Captured = 3,
  Validating = 4,
  Valid = 5,
  Persisting = 6,
  Persisted = 7,
  Loading = 8,
  ResidentHost = 9,
  ResidentBackend = 10,
  Leasing = 11,
  Replaying = 12,
  InvalidationPending = 13,
  EvictionPending = 14,
  EvictedBackend = 15,
  EvictedHost = 16,
  Invalidated = 17,
  Corrupt = 18,
  Failed = 19,
  Retired = 20,
  Terminal = 21
};

[[nodiscard]] bool lifecycle_valid(GraphLifecycle s) noexcept;

// A directed, validated transition. Returns success only when the transition is
// declared legal. Terminal states absorb illegal transitions (they reject them).
[[nodiscard]] Result<void> transition_lifecycle(GraphLifecycle from, GraphLifecycle to);

[[nodiscard]] const char* to_string(GraphLifecycle) noexcept;

// Graphs in these states are never replay-eligible.
[[nodiscard]] bool lifecycle_is_replay_eligible(GraphLifecycle s) noexcept;

} // namespace gc
