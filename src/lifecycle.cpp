#include "graphcache/lifecycle.hpp"

#include <array>
#include <string>

namespace gc {

namespace {
struct transition {
  GraphLifecycle from;
  GraphLifecycle to;
};
constexpr transition kTransitions[] = {
  {GraphLifecycle::Discovered, GraphLifecycle::Capturing},
  {GraphLifecycle::Discovered, GraphLifecycle::Invalidated},
  {GraphLifecycle::Discovered, GraphLifecycle::Corrupt},
  {GraphLifecycle::Discovered, GraphLifecycle::Failed},
  {GraphLifecycle::Discovered, GraphLifecycle::Terminal},
  {GraphLifecycle::Capturing, GraphLifecycle::Captured},
  {GraphLifecycle::Capturing, GraphLifecycle::Failed},
  {GraphLifecycle::Capturing, GraphLifecycle::Invalidated},
  {GraphLifecycle::Capturing, GraphLifecycle::Terminal},
  {GraphLifecycle::Captured, GraphLifecycle::Validating},
  {GraphLifecycle::Captured, GraphLifecycle::Failed},
  {GraphLifecycle::Captured, GraphLifecycle::Invalidated},
  {GraphLifecycle::Captured, GraphLifecycle::Terminal},
  {GraphLifecycle::Validating, GraphLifecycle::Valid},
  {GraphLifecycle::Validating, GraphLifecycle::Failed},
  {GraphLifecycle::Validating, GraphLifecycle::Invalidated},
  {GraphLifecycle::Validating, GraphLifecycle::Corrupt},
  {GraphLifecycle::Validating, GraphLifecycle::Terminal},
  {GraphLifecycle::Valid, GraphLifecycle::Persisting},
  {GraphLifecycle::Valid, GraphLifecycle::Loading},
  {GraphLifecycle::Valid, GraphLifecycle::Leasing},
  {GraphLifecycle::Valid, GraphLifecycle::ResidentHost},
  {GraphLifecycle::Valid, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::Valid, GraphLifecycle::InvalidationPending},
  {GraphLifecycle::Valid, GraphLifecycle::EvictionPending},
  {GraphLifecycle::Valid, GraphLifecycle::Retired},
  {GraphLifecycle::Valid, GraphLifecycle::Terminal},
  {GraphLifecycle::Persisting, GraphLifecycle::Persisted},
  {GraphLifecycle::Persisting, GraphLifecycle::Failed},
  {GraphLifecycle::Persisting, GraphLifecycle::Terminal},
  {GraphLifecycle::Persisted, GraphLifecycle::Loading},
  {GraphLifecycle::Persisted, GraphLifecycle::Leasing},
  {GraphLifecycle::Persisted, GraphLifecycle::ResidentHost},
  {GraphLifecycle::Persisted, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::Persisted, GraphLifecycle::EvictionPending},
  {GraphLifecycle::Persisted, GraphLifecycle::Retired},
  {GraphLifecycle::Persisted, GraphLifecycle::Terminal},
  {GraphLifecycle::Loading, GraphLifecycle::ResidentHost},
  {GraphLifecycle::Loading, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::Loading, GraphLifecycle::Failed},
  {GraphLifecycle::Loading, GraphLifecycle::Invalidated},
  {GraphLifecycle::Loading, GraphLifecycle::Terminal},
  {GraphLifecycle::ResidentHost, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::ResidentHost, GraphLifecycle::Persisted},
  {GraphLifecycle::ResidentHost, GraphLifecycle::Leasing},
  {GraphLifecycle::ResidentHost, GraphLifecycle::EvictionPending},
  {GraphLifecycle::ResidentHost, GraphLifecycle::Retired},
  {GraphLifecycle::ResidentHost, GraphLifecycle::Terminal},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::ResidentHost},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::Persisted},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::Leasing},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::Replaying},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::EvictionPending},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::InvalidationPending},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::Retired},
  {GraphLifecycle::ResidentBackend, GraphLifecycle::Terminal},
  {GraphLifecycle::Leasing, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::Leasing, GraphLifecycle::Replaying},
  {GraphLifecycle::Leasing, GraphLifecycle::ResidentHost},
  {GraphLifecycle::Leasing, GraphLifecycle::InvalidationPending},
  {GraphLifecycle::Leasing, GraphLifecycle::EvictionPending},
  {GraphLifecycle::Leasing, GraphLifecycle::Retired},
  {GraphLifecycle::Leasing, GraphLifecycle::Terminal},
  {GraphLifecycle::Replaying, GraphLifecycle::ResidentBackend},
  {GraphLifecycle::Replaying, GraphLifecycle::Leasing},
  {GraphLifecycle::Replaying, GraphLifecycle::Terminal},
  {GraphLifecycle::InvalidationPending, GraphLifecycle::Invalidated},
  {GraphLifecycle::InvalidationPending, GraphLifecycle::EvictionPending},
  {GraphLifecycle::InvalidationPending, GraphLifecycle::Terminal},
  {GraphLifecycle::EvictionPending, GraphLifecycle::EvictedBackend},
  {GraphLifecycle::EvictionPending, GraphLifecycle::EvictedHost},
  {GraphLifecycle::EvictionPending, GraphLifecycle::Terminal}
};
} // namespace

bool lifecycle_valid(GraphLifecycle s) noexcept {
  return s >= GraphLifecycle::Discovered && s <= GraphLifecycle::Terminal;
}

bool lifecycle_is_replay_eligible(GraphLifecycle s) noexcept {
  return s == GraphLifecycle::Valid || s == GraphLifecycle::Persisted ||
         s == GraphLifecycle::ResidentHost || s == GraphLifecycle::ResidentBackend ||
         s == GraphLifecycle::Leasing;
}

Result<void> transition_lifecycle(GraphLifecycle from, GraphLifecycle to) {
  const auto& t = kTransitions;
  for (const auto& e : t) {
    if (e.from == from && e.to == to) return Result<void>::success();
  }
  return Result<void>::failure(Error(ErrorCode::IllegalTransition,
                                    "illegal lifecycle transition " + std::string(to_string(from)) +
                                    " -> " + std::string(to_string(to))));
}

const char* to_string(GraphLifecycle s) noexcept {
  switch (s) {
    case GraphLifecycle::Discovered: return "Discovered";
    case GraphLifecycle::Capturing: return "Capturing";
    case GraphLifecycle::Captured: return "Captured";
    case GraphLifecycle::Validating: return "Validating";
    case GraphLifecycle::Valid: return "Valid";
    case GraphLifecycle::Persisting: return "Persisting";
    case GraphLifecycle::Persisted: return "Persisted";
    case GraphLifecycle::Loading: return "Loading";
    case GraphLifecycle::ResidentHost: return "ResidentHost";
    case GraphLifecycle::ResidentBackend: return "ResidentBackend";
    case GraphLifecycle::Leasing: return "Leasing";
    case GraphLifecycle::Replaying: return "Replaying";
    case GraphLifecycle::InvalidationPending: return "InvalidationPending";
    case GraphLifecycle::EvictionPending: return "EvictionPending";
    case GraphLifecycle::EvictedBackend: return "EvictedBackend";
    case GraphLifecycle::EvictedHost: return "EvictedHost";
    case GraphLifecycle::Invalidated: return "Invalidated";
    case GraphLifecycle::Corrupt: return "Corrupt";
    case GraphLifecycle::Failed: return "Failed";
    case GraphLifecycle::Retired: return "Retired";
    case GraphLifecycle::Terminal: return "Terminal";
  }
  return "Unknown";
}

} // namespace gc
