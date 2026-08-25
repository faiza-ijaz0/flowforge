#pragma once

#include <chrono>
#include <memory>

namespace flowforge::infra {

using TimePoint = std::chrono::system_clock::time_point;
using Duration = std::chrono::system_clock::duration;

/// Abstraction over "the current time". Every component that needs to
/// reason about time (timeouts, backoff, heartbeats, scheduling) takes a
/// `Clock&` instead of calling `std::chrono::system_clock::now()`
/// directly, so tests can inject a `ManualClock` and assert on
/// time-dependent behavior (e.g. exponential backoff scheduling)
/// deterministically instead of sleeping in tests.
class Clock {
 public:
  Clock() = default;
  virtual ~Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;

  [[nodiscard]] virtual TimePoint now() const = 0;
};

/// Production clock backed by the system clock.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] TimePoint now() const override { return std::chrono::system_clock::now(); }
};

/// Test clock that only advances when told to. Not thread-safe by design
/// -- intended for single-threaded unit tests of time-dependent logic.
class ManualClock final : public Clock {
 public:
  explicit ManualClock(TimePoint start = std::chrono::system_clock::now()) : now_(start) {}

  [[nodiscard]] TimePoint now() const override { return now_; }
  void advance(Duration delta) { now_ += delta; }
  void set(TimePoint time) { now_ = time; }

 private:
  TimePoint now_;
};

[[nodiscard]] inline std::shared_ptr<Clock> make_system_clock() {
  return std::make_shared<SystemClock>();
}

}  // namespace flowforge::infra
