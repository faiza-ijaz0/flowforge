#pragma once

#include <memory>

#include "flowforge/engine/job_handler.hpp"
#include "flowforge/persistence/product_repository.hpp"

namespace flowforge::handlers {

/// The Product domain's workload-processing handler (Phase 3E), mirroring
/// `UserProcessHandler`'s conventions with one deliberate difference: see
/// "Why ProductProcessHandler writes to PostgreSQL directly" below.
/// Registered under job_type "product.process", the same string
/// `domain::job_type_for_processing_target(ProcessingTarget::Products)`
/// and `services::CreateWorkloadRequest::type` use for a product-import
/// workload.
///
/// Validates and normalizes one product record's payload -- a flat JSON
/// object `{"sku","name","price","currency","category"?,"description"?,
/// "stock_quantity"}`, every value a JSON string (see
/// `domain::serialize_product_record_as_job_payload`) -- via the same
/// `domain::validate_and_normalize_product_record()` the Processing
/// Center's preview/confirm path already ran at import time (see
/// `services::map_structured_records_to_products`), so "what makes a
/// valid product record" cannot drift between the two call sites, exactly
/// mirroring `user-import.md`'s rationale for `UserProcessHandler`.
///
/// **Why `ProductProcessHandler` writes to PostgreSQL directly, unlike
/// `UserProcessHandler`.** This phase's brief calls for real Product
/// persistence (a `products` table, a `GET /api/v1/products` read API) --
/// `UserProcessHandler` has no equivalent because no such requirement
/// exists for Users. `engine::ExecutionContext` remains exactly as narrow
/// as `IJobHandler`'s contract requires (no database connection, no
/// repository reachable through it -- see execution_context.hpp's class
/// comment): the `IProductRepository` this handler writes through is
/// this *handler instance's own* constructor-injected dependency,
/// resolved once at application composition (`apps/server/src/http/
/// app.cpp`), the same way `services::WorkloadService` gets its
/// repositories -- never reached around `ExecutionContext`. This keeps
/// `IJobHandler` implementations free to be stateless (as every other
/// built-in handler is) or to hold their own narrowly-scoped, thread-safe
/// dependencies, without widening what every handler can see.
///
/// Retryability: a validation failure (malformed/missing fields) is
/// always non-retryable, identical to `UserProcessHandler` -- retrying an
/// unfixable payload can never succeed. A `products` table write failure
/// (`ErrorCode::Database`/`Infrastructure`) is retryable=true: unlike a
/// malformed payload, a transient database/connection-pool issue may
/// succeed on a later attempt.
class ProductProcessHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "product.process";

  explicit ProductProcessHandler(std::shared_ptr<persistence::IProductRepository> product_repository)
      : product_repository_(std::move(product_repository)) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;

 private:
  std::shared_ptr<persistence::IProductRepository> product_repository_;
};

}  // namespace flowforge::handlers
