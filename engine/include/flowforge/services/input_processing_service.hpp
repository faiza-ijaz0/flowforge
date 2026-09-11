#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/input_source.hpp"
#include "flowforge/domain/processing_target.hpp"
#include "flowforge/domain/structured_record.hpp"
#include "flowforge/domain/user_record.hpp"
#include "flowforge/engine/input_extractor.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::services {

/// One processing request: an input source type, the business target its
/// records are meant to become jobs for, and the raw payload (Phase 3C --
/// see docs/architecture/input-processing.md).
struct ProcessRequest {
  domain::InputSourceType source_type;
  domain::ProcessingTarget target;
  std::string payload;
};

/// Result of a processing request -- deliberately target-agnostic (see
/// `InputProcessingService`'s class comment): the same shape regardless
/// of whether `target` was `Users`, a future `Products`, or `Categories`.
/// Mirrors `UserImportResult`'s fields (renamed from "rows" to "records"
/// since a future non-CSV source has no literal "row"). Also the return
/// shape of `InputProcessingService::confirm` (Phase 3D-1): submitting
/// previously-previewed records into the workload pipeline is, from this
/// point on, the exact same "create a workload from records" operation
/// `process()` performs for CSV.
struct ProcessResult {
  domain::Workload workload;
  std::vector<WorkloadItemDispatchOutcome> items;
  std::size_t total_records = 0;
  std::size_t valid_records = 0;
  std::size_t invalid_records = 0;
  std::vector<domain::RejectedRecord> rejected_records;
  bool rejected_records_truncated = false;
};

/// One `Image`/`Screenshot` extraction+mapping preview -- see
/// docs/architecture/input-processing.md, "Preview". Deliberately does not
/// contain a `domain::Workload`: nothing is persisted at preview time (see
/// `InputProcessingService::preview`'s class comment). `records` are
/// already normalized (`domain::validate_and_normalize_user_record` has
/// already run) so the frontend can render them directly and round-trip
/// them, unmodified, into `InputProcessingService::confirm` --
/// `rejected_records` uses the *original* row/record position from
/// extraction, even for a record rejected only at the mapping stage (see
/// .cpp), so a caller never has to reconcile two different numbering
/// schemes.
struct PreviewResult {
  domain::InputSourceType source_type;
  domain::ProcessingTarget target;
  std::size_t total_records = 0;
  std::vector<domain::NormalizedUserRecord> records;
  std::vector<domain::RejectedRecord> rejected_records;
  bool rejected_records_truncated = false;
  std::vector<std::string> warnings;
  std::optional<double> average_confidence;
};

/// A request to submit previously-previewed, already-normalized records
/// into the workload pipeline (Phase 3D-1 -- see docs/architecture/
/// input-processing.md, "Confirmation"). `records` is expected to be
/// exactly (or a user-trimmed subset of) `PreviewResult::records` echoed
/// back by the caller -- but is never *trusted* as already-valid:
/// `InputProcessingService::confirm` re-runs
/// `domain::validate_and_normalize_user_record` on every record before it
/// becomes a job payload, the same defense-in-depth any other externally-
/// supplied input gets in this codebase.
struct ConfirmRequest {
  domain::ProcessingTarget target;
  std::vector<domain::NormalizedUserRecord> records;
};

/// Orchestrates `input source -> extraction -> normalization -> validation
/// -> workload submission` for whichever `(InputSourceType,
/// ProcessingTarget)` combinations are actually implemented (Phase 3C --
/// see docs/architecture/input-processing.md). Implements none of that
/// itself: it composes existing, already-tested pieces
/// (`services::import_users_from_csv`, which itself composes
/// `services::parse_user_import_csv` and the generic
/// `WorkloadService::create_workload`) -- never scheduler/worker/retry/
/// database logic directly, exactly like `WorkloadService` itself.
///
/// `WorkloadService` remains fully domain-agnostic (see its own class
/// comment): this class, not `WorkloadService`, is where "which business
/// target does this request want" is decided. See
/// docs/architecture/input-processing.md, "Processing types" for why a
/// new `(source, target)` combination is added here, never inside
/// `WorkloadService`.
///
/// Phase 3D-1 adds a second, image-specific request shape --
/// `preview()`/`confirm()` -- alongside the original `process()`: a
/// screenshot's extracted records must never be silently persisted the
/// way a CSV's are (see `preview()`'s class comment), so `image`/
/// `screenshot` deliberately never becomes `is_supported()` for
/// `process()` even once an `engine::IInputExtractor` is wired in for
/// them.
class InputProcessingService {
 public:
  InputProcessingService(std::shared_ptr<WorkloadService> workload_service,
                         std::shared_ptr<infra::Logger> logger,
                         std::shared_ptr<infra::MetricsRegistry> metrics = nullptr,
                         std::shared_ptr<engine::IInputExtractor> image_extractor = nullptr)
      : workload_service_(std::move(workload_service)),
        logger_(std::move(logger)),
        metrics_(std::move(metrics)),
        image_extractor_(std::move(image_extractor)) {}

  /// Returns `ErrorCode::Validation` immediately -- without attempting
  /// any extraction -- for a `(request.source_type, request.target)`
  /// combination that isn't implemented yet (every combination except
  /// CSV+Users this phase). Never a fake success, never a workload
  /// created for an unsupported combination -- see
  /// docs/architecture/input-processing.md, "What's implemented vs.
  /// foundation-only". Never accepts `Image`/`Screenshot`, even with a
  /// real extractor wired in -- see this class's own comment.
  [[nodiscard]] Result<ProcessResult> process(const ProcessRequest& request);

  /// Runs extraction and Users-mapping for `request` and returns the
  /// result WITHOUT creating a `Workload` or any `Job` -- no database
  /// write happens on this call path at all (see
  /// docs/architecture/input-processing.md, "Preview"). Supported only
  /// for `(Image|Screenshot, Users)` when an image extractor was
  /// injected at construction; every other combination (including
  /// `Csv`+`Users`, which has no preview step this phase -- see
  /// docs/architecture/input-processing.md, "Why CSV has no preview
  /// step") returns `ErrorCode::Validation`.
  [[nodiscard]] Result<PreviewResult> preview(const ProcessRequest& request);

  /// Submits `request.records` into the workload pipeline -- the same
  /// create-then-schedule path `process()` uses for CSV, just fed by
  /// already-extracted records instead of a fresh parse. Every record is
  /// re-validated (see `ConfirmRequest`'s class comment); a record that
  /// fails re-validation is reported in the returned `ProcessResult`'s
  /// `rejected_records` exactly like an invalid CSV row is, never
  /// silently dropped or silently accepted.
  [[nodiscard]] Result<ProcessResult> confirm(const ConfirmRequest& request);

 private:
  std::shared_ptr<WorkloadService> workload_service_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<engine::IInputExtractor> image_extractor_;
};

}  // namespace flowforge::services
