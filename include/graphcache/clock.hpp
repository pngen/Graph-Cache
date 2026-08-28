#pragma once
// Time abstraction for reproducible measurements and scheduling.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include <chrono>

namespace gc {

// Clock abstraction so the engine can be tested deterministically while
// production uses the system monotonic clock.
class Clock {
 public:
  using time_point = std::chrono::steady_clock::time_point;
  using duration = std::chrono::steady_clock::duration;

  virtual ~Clock() = default;
  [[nodiscard]] virtual time_point now() const = 0;

  [[nodiscard]] static Clock& system();
};

class SystemClock final : public Clock {
 public:
  [[nodiscard]] time_point now() const override {
    return std::chrono::steady_clock::now();
  }
};

inline Clock& Clock::system() {
  static SystemClock inst;
  return inst;
}

// Milliseconds since epoch (wall) for reporting.
[[nodiscard]] inline std::int64_t wall_unix_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace gc
