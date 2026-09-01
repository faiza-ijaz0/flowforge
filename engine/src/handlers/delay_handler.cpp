#include "flowforge/handlers/delay_handler.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <thread>

namespace flowforge::handlers {

namespace {
constexpr std::chrono::milliseconds kPollInterval{20};
}  // namespace

Result<domain::ExecutionResult> DelayHandler::execute(const engine::ExecutionContext& context,
                                                      const std::string& payload) {
  std::int64_t requested_ms{};
  auto [ptr, ec] = std::from_chars(payload.data(), payload.data() + payload.size(), requested_ms);
  if (ec != std::errc{} || ptr != payload.data() + payload.size() || requested_ms < 0) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "delay payload must be a non-negative integer number of "
                                      "milliseconds"));
  }

  const std::chrono::milliseconds requested{requested_ms};
  if (requested > kMaxDelay) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "delay must be <= " + std::to_string(kMaxDelay.count()) + "ms"));
  }

  context.logger().debug("delay_handler", "starting delay",
                         {{.key = "job_id", .value = context.job_id().value()},
                          {.key = "requested_ms", .value = std::to_string(requested_ms)}});

  const auto start = std::chrono::steady_clock::now();
  auto remaining = requested;
  while (remaining.count() > 0) {
    if (context.is_cancelled()) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
      return domain::ExecutionResult::failure(ErrorCode::JobExecution, "delay cancelled before completion",
                                              /*retryable=*/false, elapsed);
    }
    const auto sleep_for = std::min(remaining, kPollInterval);
    std::this_thread::sleep_for(sleep_for);
    remaining -= sleep_for;
  }

  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success("slept " + std::to_string(requested_ms) + "ms", duration,
                                          {{"operation", "delay"}});
}

}  // namespace flowforge::handlers
