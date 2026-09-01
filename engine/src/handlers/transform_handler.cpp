#include "flowforge/handlers/transform_handler.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace flowforge::handlers {

namespace {
constexpr std::size_t kMaxPayloadBytes = std::size_t{256} * 1024;
}  // namespace

Result<domain::ExecutionResult> TransformHandler::execute(const engine::ExecutionContext& context,
                                                          const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "transform payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();
  std::string output = payload;
  std::ranges::transform(output, output.begin(),
                         [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  context.logger().debug("transform_handler", "uppercased payload",
                         {{.key = "job_id", .value = context.job_id().value()}});
  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(std::move(output), duration, {{"operation", "uppercase"}});
}

}  // namespace flowforge::handlers
