#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace flowforge::domain {

/// Exponential backoff with a cap. `compute_backoff` is a pure function so
/// it can be unit tested without a running scheduler -- the scheduler
/// (Phase 2) will simply call it when deciding when to re-queue a failed
/// attempt.
struct RetryPolicy {
  std::uint32_t max_attempts = 3;
  std::chrono::milliseconds initial_backoff{1000};
  std::chrono::milliseconds max_backoff{60'000};
  double backoff_multiplier = 2.0;

  /// Returns the delay before attempt number `attempt` (1-indexed: the
  /// delay before the *second* attempt is `compute_backoff(1)`, i.e. the
  /// argument is the number of attempts already made).
  [[nodiscard]] std::chrono::milliseconds compute_backoff(std::uint32_t attempts_made) const {
    if (attempts_made == 0) {
      return std::chrono::milliseconds{0};
    }
    auto delay_ms = static_cast<double>(initial_backoff.count());
    for (std::uint32_t i = 1; i < attempts_made; ++i) {
      delay_ms *= backoff_multiplier;
      if (delay_ms >= static_cast<double>(max_backoff.count())) {
        delay_ms = static_cast<double>(max_backoff.count());
        break;
      }
    }
    delay_ms = std::min(delay_ms, static_cast<double>(max_backoff.count()));
    return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
  }

  [[nodiscard]] bool exhausted(std::uint32_t attempts_made) const { return attempts_made >= max_attempts; }
};

}  // namespace flowforge::domain
