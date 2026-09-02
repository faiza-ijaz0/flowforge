#include "flowforge/services/user_import.hpp"

namespace flowforge::services {

Result<UserImportResult> import_users_from_csv(WorkloadService& workload_service,
                                               const std::string& csv_content,
                                               const std::shared_ptr<infra::Logger>& logger,
                                               const std::shared_ptr<infra::MetricsRegistry>& metrics) {
  auto parsed = parse_user_import_csv(csv_content);
  if (!parsed) {
    // Whole-file rejection: nothing is created (see .hpp's class comment
    // and docs/architecture/user-import.md, "Bulk submission semantics",
    // scenario A). Deliberately not logged with the raw CSV content --
    // see docs/architecture/user-import.md, "Security" -- only the parser
    // error's own message (never the uploaded bytes) is logged.
    logger->warn("user_import", "user import rejected: invalid CSV",
                 {{.key = "reason", .value = parsed.error().message()}});
    return std::unexpected(parsed.error());
  }

  const std::size_t total_rows = parsed->total_rows;
  const std::size_t valid_row_count = parsed->valid_rows.size();
  const std::size_t rejected_row_count = parsed->rejected_row_count;

  // "user.process" is the only workload type a CSV import produces in
  // this phase (see Step 6 of the phase brief: reuse the existing
  // handler, do not add another workload type yet). This is the one
  // place -- outside `WorkloadService` -- that knows "user import" maps
  // to the "user.process" job type; a future product/category import
  // would name its own job type here, in its own analogous function.
  CreateWorkloadRequest request;
  request.type = "user.process";
  request.items.reserve(valid_row_count);
  for (const auto& record : parsed->valid_rows) {
    request.items.push_back({.payload = domain::serialize_user_record_as_job_payload(record)});
  }

  auto created = workload_service.create_workload(request);
  if (!created) {
    return std::unexpected(created.error());
  }

  if (metrics) {
    metrics->increment_counter("flowforge_user_imports_created_total");
    metrics->increment_counter("flowforge_user_import_rows_total", static_cast<std::int64_t>(total_rows));
    metrics->increment_counter("flowforge_user_import_rows_rejected_total",
                               static_cast<std::int64_t>(rejected_row_count));
  }
  logger->info("user_import", "user import created",
               {{.key = "workload_id", .value = created->workload.id().value()},
                {.key = "total_rows", .value = std::to_string(total_rows)},
                {.key = "valid_rows", .value = std::to_string(valid_row_count)},
                {.key = "rejected_rows", .value = std::to_string(rejected_row_count)}});

  return UserImportResult{.workload = std::move(created->workload),
                          .items = std::move(created->items),
                          .total_rows = total_rows,
                          .valid_rows = valid_row_count,
                          .invalid_rows = rejected_row_count,
                          .rejected_rows = std::move(parsed->rejected_rows),
                          .rejected_rows_truncated = parsed->rejected_rows_truncated};
}

}  // namespace flowforge::services
