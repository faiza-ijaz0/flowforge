#pragma once

#include <chrono>

#include "flowforge/engine/job_handler.hpp"

namespace flowforge::handlers {

/// Waits for a caller-specified duration (payload: a non-negative integer
/// number of milliseconds, e.g. "500"), then succeeds. Useful for
/// demonstrating concurrent handler execution once a real worker pool
/// exists. `kMaxDelay` bounds the requested duration so a malicious or
/// buggy payload cannot tie up a worker thread indefinitely (see
/// IJobHandler's "untrusted input" note and
/// docs/architecture/execution-model.md, "Security"). Polls
/// `ExecutionContext::is_cancelled()` between short sleep increments so
/// cooperative cancellation (once a caller actually requests it) takes
/// effect within one poll interval instead of only after the full delay
/// elapses. Stateless.
class DelayHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "delay";
  static constexpr std::chrono::milliseconds kMaxDelay{30'000};

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;
};

}  // namespace flowforge::handlers
