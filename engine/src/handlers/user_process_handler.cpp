#include "flowforge/handlers/user_process_handler.hpp"

#include <chrono>
#include <optional>

#include "flowforge/domain/user_record.hpp"
#include "flowforge/infra/json_lite.hpp"

namespace flowforge::handlers {

namespace {

constexpr std::size_t kMaxPayloadBytes = std::size_t{16} * 1024;

using infra::extract_json_string_field;
using infra::json_escape;

}  // namespace

Result<domain::ExecutionResult> UserProcessHandler::execute(const engine::ExecutionContext& context,
                                                            const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "user.process payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();

  if (context.is_cancelled()) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return domain::ExecutionResult::failure(ErrorCode::JobExecution,
                                            "user.process cancelled before execution",
                                            /*retryable=*/false, duration);
  }

  auto raw_name = extract_json_string_field(payload, "name");
  if (!raw_name) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' is required and must be a JSON string"));
  }
  auto raw_email = extract_json_string_field(payload, "email");
  if (!raw_email) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'email' is required and must be a JSON string"));
  }
  auto raw_phone = extract_json_string_field(payload, "phone");

  // Shared with services::WorkloadService's CSV import path (see
  // domain/user_record.hpp's class comment) -- "what makes a valid user
  // record" is defined exactly once.
  auto normalized = domain::validate_and_normalize_user_record(
      *raw_name, *raw_email, raw_phone ? std::optional<std::string_view>(*raw_phone) : std::nullopt);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  auto upserted = user_repository_->upsert(context.job_id(), *normalized);
  if (!upserted) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    context.logger().warn("user_process_handler", "user persistence failed",
                          {{.key = "job_id", .value = context.job_id().value()},
                           {.key = "email", .value = normalized->email},
                           {.key = "reason", .value = upserted.error().message()}});
    // Unlike a malformed payload (never retryable), a persistence failure
    // may be transient -- see the header's "Retryability" note.
    return domain::ExecutionResult::failure(upserted.error().code(), upserted.error().message(),
                                            /*retryable=*/true, duration);
  }

  context.logger().debug(
      "user_process_handler", "upserted user record",
      {{.key = "job_id", .value = context.job_id().value()}, {.key = "email", .value = normalized->email}});

  std::string output = R"({"name":")" + json_escape(normalized->name) + R"(","email":")" +
                       json_escape(normalized->email) + R"(")";
  if (normalized->phone) {
    output += R"(,"phone":")" + json_escape(*normalized->phone) + R"(")";
  }
  output += R"(,"valid":true})";

  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(std::move(output), duration, {{"operation", "user_process"}});
}

}  // namespace flowforge::handlers
