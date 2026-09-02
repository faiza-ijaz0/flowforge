#include "flowforge/services/input_processing_service.hpp"

#include "flowforge/services/user_import.hpp"

namespace flowforge::services {

namespace {

/// The one implemented `(InputSourceType, ProcessingTarget)` combination
/// this phase. See docs/architecture/input-processing.md, "What's
/// implemented vs. foundation-only" -- extending this is how a future
/// phase adds e.g. CSV+Products, never by loosening `WorkloadService`
/// itself.
[[nodiscard]] bool is_supported(domain::InputSourceType source, domain::ProcessingTarget target) noexcept {
  return source == domain::InputSourceType::Csv && target == domain::ProcessingTarget::Users;
}

ProcessResult from_user_import_result(UserImportResult result) {
  std::vector<domain::RejectedRecord> rejected_records;
  rejected_records.reserve(result.rejected_rows.size());
  for (auto& row : result.rejected_rows) {
    rejected_records.push_back({.index = row.row_number, .reason = std::move(row.reason)});
  }
  // Aggregate-initialized (not default-constructed then assigned):
  // ProcessResult::workload is a domain::Workload, which has no default
  // constructor.
  return ProcessResult{.workload = std::move(result.workload),
                       .items = std::move(result.items),
                       .total_records = result.total_rows,
                       .valid_records = result.valid_rows,
                       .invalid_records = result.invalid_rows,
                       .rejected_records = std::move(rejected_records),
                       .rejected_records_truncated = result.rejected_rows_truncated};
}

}  // namespace

Result<ProcessResult> InputProcessingService::process(const ProcessRequest& request) {
  if (!is_supported(request.source_type, request.target)) {
    logger_->warn("input_processing_service", "process rejected: unsupported combination",
                  {{.key = "source", .value = std::string(domain::to_string(request.source_type))},
                   {.key = "target", .value = std::string(domain::to_string(request.target))}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_process_unsupported_total");
    }
    return std::unexpected(
        make_error(ErrorCode::Validation, "source '" + std::string(domain::to_string(request.source_type)) +
                                              "' is not yet supported for target '" +
                                              std::string(domain::to_string(request.target)) + "'"));
  }

  // The only implemented combination this phase reuses the exact same,
  // already-tested pipeline `POST /api/v1/workloads/user-imports` uses
  // (import_users_from_csv -> parse_user_import_csv +
  // WorkloadService::create_workload) -- see docs/architecture/
  // input-processing.md, "Why /api/v1/process delegates rather than
  // reimplements". This guarantees the new endpoint can never silently
  // drift from the existing, already-shipped one's behavior.
  auto imported = import_users_from_csv(*workload_service_, request.payload, logger_, metrics_);
  if (!imported) {
    return std::unexpected(imported.error());
  }
  return from_user_import_result(std::move(*imported));
}

}  // namespace flowforge::services
