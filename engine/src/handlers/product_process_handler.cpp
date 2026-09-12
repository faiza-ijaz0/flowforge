#include "flowforge/handlers/product_process_handler.hpp"

#include <chrono>
#include <optional>

#include "flowforge/domain/product_record.hpp"
#include "flowforge/infra/json_lite.hpp"

namespace flowforge::handlers {

namespace {

constexpr std::size_t kMaxPayloadBytes = std::size_t{16} * 1024;

using infra::extract_json_string_field;

}  // namespace

Result<domain::ExecutionResult> ProductProcessHandler::execute(const engine::ExecutionContext& context,
                                                               const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "product.process payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();

  if (context.is_cancelled()) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return domain::ExecutionResult::failure(ErrorCode::JobExecution,
                                            "product.process cancelled before execution",
                                            /*retryable=*/false, duration);
  }

  auto raw_sku = extract_json_string_field(payload, "sku");
  if (!raw_sku) {
    return std::unexpected(make_error(ErrorCode::Validation, "'sku' is required and must be a JSON string"));
  }
  auto raw_name = extract_json_string_field(payload, "name");
  if (!raw_name) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' is required and must be a JSON string"));
  }
  auto raw_price = extract_json_string_field(payload, "price");
  if (!raw_price) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'price' is required and must be a JSON string"));
  }
  auto raw_currency = extract_json_string_field(payload, "currency");
  auto raw_category = extract_json_string_field(payload, "category");
  auto raw_description = extract_json_string_field(payload, "description");
  auto raw_stock_quantity = extract_json_string_field(payload, "stock_quantity");

  // Shared with the Processing Center's preview/confirm path (see
  // domain/product_record.hpp's class comment) -- "what makes a valid
  // product record" is defined exactly once.
  auto normalized = domain::validate_and_normalize_product_record(
      *raw_sku, *raw_name, *raw_price,
      raw_currency ? std::optional<std::string_view>(*raw_currency) : std::nullopt,
      raw_category ? std::optional<std::string_view>(*raw_category) : std::nullopt,
      raw_description ? std::optional<std::string_view>(*raw_description) : std::nullopt,
      raw_stock_quantity ? std::optional<std::string_view>(*raw_stock_quantity) : std::nullopt);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  auto upserted = product_repository_->upsert(context.job_id(), *normalized);
  if (!upserted) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    context.logger().warn("product_process_handler", "product persistence failed",
                          {{.key = "job_id", .value = context.job_id().value()},
                           {.key = "sku", .value = normalized->sku},
                           {.key = "reason", .value = upserted.error().message()}});
    // Unlike a malformed payload (never retryable), a persistence
    // failure may be transient -- see the header's "Retryability" note.
    return domain::ExecutionResult::failure(upserted.error().code(), upserted.error().message(),
                                            /*retryable=*/true, duration);
  }

  context.logger().debug(
      "product_process_handler", "upserted product record",
      {{.key = "job_id", .value = context.job_id().value()}, {.key = "sku", .value = normalized->sku}});

  const std::string output = domain::serialize_product_record_as_job_payload(*normalized);
  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(output, duration, {{"operation", "product_process"}});
}

}  // namespace flowforge::handlers
