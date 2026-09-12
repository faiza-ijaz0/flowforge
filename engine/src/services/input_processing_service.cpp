#include "flowforge/services/input_processing_service.hpp"

#include <cstdio>
#include <unordered_set>

#include "flowforge/domain/product_record.hpp"
#include "flowforge/domain/user_record.hpp"
#include "flowforge/services/product_mapping.hpp"
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
/// implemented vs. foundation-only" -- CSV+Products deliberately does
/// NOT appear here even though it's implemented, because it goes through
/// preview()/confirm() only -- see docs/architecture/
/// product-processing.md, "Why CSV+Products has no direct process()
/// path".
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

// --- Generic <-> per-target record conversion --------------------------
//
// This is the ONLY place InputProcessingService knows "Users" and
// "Products" are different targets -- see the header's class comment.
// Each function here is a thin adapter over that target's own free
// functions (domain::validate_and_normalize_*_record,
// services::map_structured_records_to_*); none of them reimplement a
// validation rule.

domain::StructuredRecord user_record_to_structured(const domain::NormalizedUserRecord& record) {
  domain::StructuredRecord structured;
  structured.fields.emplace("name", record.name);
  structured.fields.emplace("email", record.email);
  if (record.phone) {
    structured.fields.emplace("phone", *record.phone);
  }
  return structured;
}

std::string format_price(double price) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.2f", price);
  return buffer;
}

domain::StructuredRecord product_record_to_structured(const domain::NormalizedProductRecord& record) {
  domain::StructuredRecord structured;
  structured.fields.emplace("sku", record.sku);
  structured.fields.emplace("name", record.name);
  structured.fields.emplace("price", format_price(record.price));
  structured.fields.emplace("currency", record.currency);
  if (record.category) {
    structured.fields.emplace("category", *record.category);
  }
  if (record.description) {
    structured.fields.emplace("description", *record.description);
  }
  structured.fields.emplace("stock_quantity", std::to_string(record.stock_quantity));
  return structured;
}

/// The generic shape `map_structured_records_to_users`/
/// `map_structured_records_to_products` both reduce to once their
/// target-specific `NormalizedXRecord` is converted to a
/// `StructuredRecord` -- see the two functions above.
struct GenericMappedRecords {
  std::vector<domain::StructuredRecord> valid_records;
  std::vector<domain::RejectedRecord> rejected_records;
  bool rejected_records_truncated = false;
};

[[nodiscard]] Result<GenericMappedRecords> map_for_target(
    domain::ProcessingTarget target, const std::vector<domain::StructuredRecord>& records) {
  GenericMappedRecords result;
  if (target == domain::ProcessingTarget::Users) {
    auto mapped = map_structured_records_to_users(records);
    result.valid_records.reserve(mapped.valid_records.size());
    for (auto& record : mapped.valid_records) {
      result.valid_records.push_back(user_record_to_structured(record));
    }
    for (auto& rejection : mapped.rejected_records) {
      result.rejected_records.push_back({.index = rejection.index, .reason = std::move(rejection.reason)});
    }
    result.rejected_records_truncated = mapped.rejected_records_truncated;
    return result;
  }
  if (target == domain::ProcessingTarget::Products) {
    auto mapped = map_structured_records_to_products(records);
    result.valid_records.reserve(mapped.valid_records.size());
    for (auto& record : mapped.valid_records) {
      result.valid_records.push_back(product_record_to_structured(record));
    }
    for (auto& rejection : mapped.rejected_records) {
      result.rejected_records.push_back({.index = rejection.index, .reason = std::move(rejection.reason)});
    }
    result.rejected_records_truncated = mapped.rejected_records_truncated;
    return result;
  }
  return std::unexpected(make_error(
      ErrorCode::Validation, "target '" + std::string(domain::to_string(target)) + "' is not supported"));
}

/// Re-validates one already-normalized generic record against `target`'s
/// own rules and serializes it as that target's job payload -- confirm()'s
/// per-record step. Never trusts `record` as already-valid (see
/// `ConfirmRequest`'s class comment).
[[nodiscard]] Result<std::string> validate_record_for_target(domain::ProcessingTarget target,
                                                             const domain::StructuredRecord& record) {
  if (target == domain::ProcessingTarget::Users) {
    const auto name = record.field("name");
    const auto email = record.field("email");
    if (!name || !email) {
      return std::unexpected(make_error(ErrorCode::Validation, "'name' and 'email' are required"));
    }
    auto normalized = domain::validate_and_normalize_user_record(*name, *email, record.field("phone"));
    if (!normalized) {
      return std::unexpected(normalized.error());
    }
    return domain::serialize_user_record_as_job_payload(*normalized);
  }
  if (target == domain::ProcessingTarget::Products) {
    const auto sku = record.field("sku");
    const auto name = record.field("name");
    const auto price = record.field("price");
    if (!sku || !name || !price) {
      return std::unexpected(make_error(ErrorCode::Validation, "'sku', 'name', and 'price' are required"));
    }
    auto normalized = domain::validate_and_normalize_product_record(
        *sku, *name, *price, record.field("currency"), record.field("category"), record.field("description"),
        record.field("stock_quantity"));
    if (!normalized) {
      return std::unexpected(normalized.error());
    }
    return domain::serialize_product_record_as_job_payload(*normalized);
  }
  return std::unexpected(make_error(
      ErrorCode::Validation, "target '" + std::string(domain::to_string(target)) + "' is not supported"));
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
  const engine::IInputExtractor* extractor = nullptr;
  if (request.target == domain::ProcessingTarget::Users) {
    if (is_image_source(request.source_type) && image_extractor_) {
      extractor = image_extractor_.get();
    }
  } else if (request.target == domain::ProcessingTarget::Products) {
    if (request.source_type == domain::InputSourceType::Csv) {
      extractor = &csv_extractor_;
    } else if (is_image_source(request.source_type) && image_extractor_) {
      extractor = image_extractor_.get();
    }
  }
  if (!extractor) {
    logger_->warn("input_processing_service", "preview rejected: unsupported combination",
                  {{.key = "source", .value = std::string(domain::to_string(request.source_type))},
                   {.key = "target", .value = std::string(domain::to_string(request.target))}});
    return std::unexpected(
        make_error(ErrorCode::Validation, "source '" + std::string(domain::to_string(request.source_type)) +
                                              "' does not support preview for target '" +
                                              std::string(domain::to_string(request.target)) + "'"));
  }

  const domain::InputPayload payload{.source_type = request.source_type, .content = request.payload};
  auto extracted = extractor->extract(payload);
  if (!extracted) {
    logger_->warn("input_processing_service", "preview extraction failed",
                  {{.key = "reason", .value = extracted.error().message()}});
    return std::unexpected(extracted.error());
  }

  auto mapped = map_for_target(request.target, extracted->records);
  if (!mapped) {
    return std::unexpected(mapped.error());
  }

  // extracted->records only contains rows extraction itself found
  // structurally valid -- to report every rejection (structural and
  // mapping-level) against its real position in the original input, not
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
  result.records = std::move(mapped->valid_records);
  result.warnings = extracted->warnings;
  result.average_confidence = extracted->average_confidence;

  result.rejected_records = extracted->rejected_records;
  result.rejected_records_truncated =
      extracted->rejected_records_truncated || mapped->rejected_records_truncated;
  for (const auto& rejection : mapped->rejected_records) {
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
                {{.key = "target", .value = std::string(domain::to_string(request.target))},
                 {.key = "total_records", .value = std::to_string(result.total_records)},
                 {.key = "valid_records", .value = std::to_string(result.records.size())},
                 {.key = "rejected_records", .value = std::to_string(result.rejected_records.size())}});

  return result;
}

Result<ProcessResult> InputProcessingService::confirm(const ConfirmRequest& request) {
  if (request.target != domain::ProcessingTarget::Users &&
      request.target != domain::ProcessingTarget::Products) {
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
  create_request.type = std::string(domain::job_type_for_processing_target(request.target));
  create_request.items.reserve(request.records.size());

  std::vector<domain::RejectedRecord> rejected_records;
  for (std::size_t i = 0; i < request.records.size(); ++i) {
    // Never trusted as already-valid -- see ConfirmRequest's class
    // comment: re-run the exact same validation preview() itself
    // already ran (and, for Users, the CSV import path also runs).
    auto payload = validate_record_for_target(request.target, request.records[i]);
    if (!payload) {
      rejected_records.push_back({.index = i + 1, .reason = payload.error().message()});
      continue;
    }
    create_request.items.push_back({.payload = std::move(*payload)});
  }

  auto created = workload_service_->create_workload(create_request);
  if (!created) {
    return std::unexpected(created.error());
  }

  if (metrics_) {
    metrics_->increment_counter("flowforge_process_confirms_total");
  }
  logger_->info("input_processing_service", "confirm submitted",
                {{.key = "target", .value = std::string(domain::to_string(request.target))},
                 {.key = "workload_id", .value = created->workload.id().value()},
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
