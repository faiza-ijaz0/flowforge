#include "flowforge/handlers/echo_handler.hpp"

#include <chrono>

namespace flowforge::handlers {

namespace {
// Independent of JobService::kMaxPayloadBytes on purpose: a handler must
// not assume every caller of IJobHandler::execute() went through
// JobService's validation (see IJobHandler's "untrusted input" note) --
// defense in depth, not a duplicate of that check.
constexpr std::size_t kMaxPayloadBytes = std::size_t{256} * 1024;
}  // namespace

Result<domain::ExecutionResult> EchoHandler::execute(const engine::ExecutionContext& context,
                                                     const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(make_error(
        ErrorCode::Validation, "echo payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();
  context.logger().debug("echo_handler", "echoing payload",
                         {{.key = "job_id", .value = context.job_id().value()}});
  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(payload, duration, {{"operation", "echo"}});
}

}  // namespace flowforge::handlers
