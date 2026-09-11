#include "flowforge/services/input_processing_service.hpp"

#include <unordered_set>

#include "flowforge/services/user_import.hpp"
#include "flowforge/services/user_mapping.hpp"

namespace flowforge::services {

namespace {

/// Bounds a single `confirm()` call the same way a single CSV import is
/// bounded (`kMaxUserImportRows`) -- a caller submitting previously
/// previewed records has no reason to submit more than that many at once,
/// and this keeps `confirm()`'s worst-case work bounded regardless of
/// what generated the request body.
constexpr std::size_t kMaxConfirmRecords = 1000;

/// The one implemented `(InputSourceType, ProcessingTarget)` combination
/// for `process()`. See docs/architecture/input-processing.md, "What's
/// implemented vs. foundation-only" -- extending this is how a future
/// phase adds e.g. CSV+Products, never by loosening `WorkloadService`
/// itself. Deliberately excludes `Image`/`Screenshot` even once an
/// extractor exists for them -- see `InputProcessingService::process`'s
/// class comment and `preview()` below.
[[nodiscard]] bool is_supported(domain::InputSourceType source, domain::ProcessingTarget target) noexcept {
  return source == domain::InputSourceType::Csv && target == domain::ProcessingTarget::Users;
}

[[nodiscard]] bool is_image_source(domain::InputSourceType source) noexcept {
  return source == domain::InputSourceType::Image || source == domain::InputSourceType::Screenshot;
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

Result<PreviewResult> InputProcessingService::preview(const ProcessRequest& request) {
  if (request.target != domain::ProcessingTarget::Users || !is_image_source(request.source_type) ||
      !image_extractor_) {
    logger_->warn("input_processing_service", "preview rejected: unsupported combination",
                  {{.key = "source", .value = std::string(domain::to_string(request.source_type))},
                   {.key = "target", .value = std::string(domain::to_string(request.target))}});
    return std::unexpected(
        make_error(ErrorCode::Validation, "source '" + std::string(domain::to_string(request.source_type)) +
                                              "' does not support preview for target '" +
                                              std::string(domain::to_string(request.target)) + "'"));
  }

  const domain::InputPayload payload{.source_type = request.source_type, .content = request.payload};
  auto extracted = image_extractor_->extract(payload);
  if (!extracted) {
    logger_->warn("input_processing_service", "preview extraction failed",
                  {{.key = "reason", .value = extracted.error().message()}});
    return std::unexpected(extracted.error());
  }

  auto mapped = map_structured_records_to_users(extracted->records);

  // extracted->records only contains rows extraction itself found
  // structurally valid -- to report every rejection (structural and
  // mapping-level) against its real position in the original image, not
  // extracted->records' own (shorter, renumbered) index space,
  // reconstruct which original row each extracted->records[k] came from:
  // it's the k-th smallest original index that ISN'T already one of
  // extraction's own rejections.
  std::unordered_set<std::size_t> structurally_rejected;
  structurally_rejected.reserve(extracted->rejected_records.size());
  for (const auto& rejected : extracted->rejected_records) {
    structurally_rejected.insert(rejected.index);
  }
  std::vector<std::size_t> original_index_of;
  original_index_of.reserve(extracted->records.size());
  for (std::size_t original = 1; original <= extracted->total_records; ++original) {
    if (!structurally_rejected.contains(original)) {
      original_index_of.push_back(original);
    }
  }

  PreviewResult result;
  result.source_type = request.source_type;
  result.target = request.target;
  result.total_records = extracted->total_records;
  result.records = std::move(mapped.valid_records);
  result.warnings = extracted->warnings;
  result.average_confidence = extracted->average_confidence;

  result.rejected_records = extracted->rejected_records;
  result.rejected_records_truncated =
      extracted->rejected_records_truncated || mapped.rejected_records_truncated;
  for (const auto& rejection : mapped.rejected_records) {
    // rejection.index is 1-based within extracted->records.
    const std::size_t original_index = (rejection.index >= 1 && rejection.index <= original_index_of.size())
                                           ? original_index_of[rejection.index - 1]
                                           : rejection.index;
    result.rejected_records.push_back({.index = original_index, .reason = rejection.reason});
  }

  if (metrics_) {
    metrics_->increment_counter("flowforge_process_previews_total");
  }
  logger_->info("input_processing_service", "preview computed",
                {{.key = "total_records", .value = std::to_string(result.total_records)},
                 {.key = "valid_records", .value = std::to_string(result.records.size())},
                 {.key = "rejected_records", .value = std::to_string(result.rejected_records.size())}});

  return result;
}

Result<ProcessResult> InputProcessingService::confirm(const ConfirmRequest& request) {
  if (request.target != domain::ProcessingTarget::Users) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "target '" + std::string(domain::to_string(request.target)) + "' is not supported"));
  }
  if (request.records.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "at least one record is required"));
  }
  if (request.records.size() > kMaxConfirmRecords) {
    return std::unexpected(make_error(ErrorCode::Validation, "at most " + std::to_string(kMaxConfirmRecords) +
                                                                 " records may be confirmed at once"));
  }

  CreateWorkloadRequest create_request;
  create_request.type = std::string(domain::job_type_for_processing_target(domain::ProcessingTarget::Users));
  create_request.items.reserve(request.records.size());

  std::vector<domain::RejectedRecord> rejected_records;
  for (std::size_t i = 0; i < request.records.size(); ++i) {
    const auto& record = request.records[i];
    // Never trusted as already-valid -- see ConfirmRequest's class
    // comment: re-run the exact same validation CSV import and
    // preview() itself already ran.
    auto normalized = domain::validate_and_normalize_user_record(
        record.name, record.email,
        record.phone ? std::optional<std::string_view>(*record.phone) : std::nullopt);
    if (!normalized) {
      rejected_records.push_back({.index = i + 1, .reason = normalized.error().message()});
      continue;
    }
    create_request.items.push_back({.payload = domain::serialize_user_record_as_job_payload(*normalized)});
  }

  auto created = workload_service_->create_workload(create_request);
  if (!created) {
    return std::unexpected(created.error());
  }

  if (metrics_) {
    metrics_->increment_counter("flowforge_process_confirms_total");
  }
  logger_->info("input_processing_service", "confirm submitted",
                {{.key = "workload_id", .value = created->workload.id().value()},
                 {.key = "total_records", .value = std::to_string(request.records.size())},
                 {.key = "valid_records", .value = std::to_string(create_request.items.size())},
                 {.key = "rejected_records", .value = std::to_string(rejected_records.size())}});

  return ProcessResult{.workload = std::move(created->workload),
                       .items = std::move(created->items),
                       .total_records = request.records.size(),
                       .valid_records = create_request.items.size(),
                       .invalid_records = rejected_records.size(),
                       .rejected_records = std::move(rejected_records),
                       .rejected_records_truncated = false};
}

}  // namespace flowforge::services
