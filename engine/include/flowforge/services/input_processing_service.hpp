#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/input_source.hpp"
#include "flowforge/domain/processing_target.hpp"
#include "flowforge/domain/structured_record.hpp"
#include "flowforge/engine/input_extractor.hpp"
#include "flowforge/extractors/csv_extractor.hpp"
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
/// of whether `target` was `Users`, `Products`, or a future `Categories`.
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

/// One extraction+mapping preview -- see docs/architecture/
/// input-processing.md, "Preview". Deliberately does not contain a
/// `domain::Workload`: nothing is persisted at preview time (see
/// `InputProcessingService::preview`'s class comment).
///
/// `records` are generic `domain::StructuredRecord`s (Phase 3E -- widened
/// from the Phase 3D-1 `vector<domain::NormalizedUserRecord>`), but
/// **already normalized**: each one is the exact field-name/value shape
/// the matching `domain::validate_and_normalize_*_record` function
/// produced (canonical field names -- e.g. `name`/`email`/`phone` for
/// Users, `sku`/`name`/`price`/`currency`/`category`/`description`/
/// `stock_quantity` for Products -- never the raw, alias-prone header
/// text extraction produced). This is what keeps
/// `InputProcessingService` itself target-agnostic (see its class
/// comment): it never constructs or inspects a `NormalizedUserRecord`/
/// `NormalizedProductRecord` directly, only the generic map both
/// serialize down to. The frontend renders/round-trips these directly
/// into `InputProcessingService::confirm` -- `rejected_records` uses the
/// *original* row/record position from extraction, even for a record
/// rejected only at the mapping stage (see .cpp), so a caller never has
/// to reconcile two different numbering schemes.
struct PreviewResult {
  domain::InputSourceType source_type;
  domain::ProcessingTarget target;
  std::size_t total_records = 0;
  std::vector<domain::StructuredRecord> records;
  std::vector<domain::RejectedRecord> rejected_records;
  bool rejected_records_truncated = false;
  std::vector<std::string> warnings;
  std::optional<double> average_confidence;
};

/// A request to submit previously-previewed, already-normalized records
/// into the workload pipeline (Phase 3D-1, widened to generic records in
/// Phase 3E -- see docs/architecture/input-processing.md,
/// "Confirmation"). `records` is expected to be exactly (or a
/// user-trimmed subset of) `PreviewResult::records` echoed back by the
/// caller -- but is never *trusted* as already-valid:
/// `InputProcessingService::confirm` re-runs the target's
/// `domain::validate_and_normalize_*_record` on every record before it
/// becomes a job payload, the same defense-in-depth any other
/// externally-supplied input gets in this codebase.
struct ConfirmRequest {
  domain::ProcessingTarget target;
  std::vector<domain::StructuredRecord> records;
};

/// Orchestrates `input source -> extraction -> normalization -> validation
/// -> workload submission` for whichever `(InputSourceType,
/// ProcessingTarget)` combinations are actually implemented (Phase 3C --
/// see docs/architecture/input-processing.md). Implements none of that
/// itself: it composes existing, already-tested pieces
/// (`services::import_users_from_csv`, `services::
/// map_structured_records_to_users`/`map_structured_records_to_products`,
/// and the generic `WorkloadService::create_workload`) -- never
/// scheduler/worker/retry/database logic directly, exactly like
/// `WorkloadService` itself.
///
/// **Stays domain-agnostic despite serving multiple targets (Phase 3E --
/// see docs/architecture/product-processing.md, "Why InputProcessingService
/// is not duplicated for Products").** `preview()`/`confirm()` operate
/// entirely on generic `domain::StructuredRecord`s; the *only* place this
/// class knows "Users" and "Products" are different is a small,
/// deliberately-contained dispatch in the .cpp (mirroring how
/// `is_supported()` already special-cases CSV+Users) that calls out to
/// each target's own free functions in `product_record.hpp`/
/// `user_record.hpp`/`user_mapping.hpp`/`product_mapping.hpp` -- it never
/// reimplements a single validation rule, and adding a future
/// `Categories` target means adding one more branch plus that target's
/// own adapter module, never touching `WorkloadService` or duplicating
/// this class.
///
/// `WorkloadService` remains fully domain-agnostic (see its own class
/// comment): this class, not `WorkloadService`, is where "which business
/// target does this request want" is decided. See
/// docs/architecture/input-processing.md, "Processing types" for why a
/// new `(source, target)` combination is added here, never inside
/// `WorkloadService`.
///
/// Phase 3D-1 added a second, preview-first request shape --
/// `preview()`/`confirm()` -- alongside the original `process()`: a
/// screenshot's (or, as of Phase 3E, a Products CSV's) extracted records
/// must never be silently persisted the way a Users CSV's are (see
/// `preview()`'s class comment), so `image`/`screenshot` deliberately
/// never becomes `is_supported()` for `process()` even once an
/// `engine::IInputExtractor` is wired in for them, and neither does
/// `csv`+`products` -- see docs/architecture/product-processing.md, "Why
/// CSV+Products has no direct process() path".
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
  /// combination that isn't implemented yet (only CSV+Users today). Never
  /// a fake success, never a workload created for an unsupported
  /// combination -- see docs/architecture/input-processing.md, "What's
  /// implemented vs. foundation-only". Never accepts `Image`/`Screenshot`
  /// for any target, and never accepts `csv`+`products` -- both require
  /// the preview/confirm path instead, see this class's own comment.
  [[nodiscard]] Result<ProcessResult> process(const ProcessRequest& request);

  /// Runs extraction and target-specific mapping for `request` and
  /// returns the result WITHOUT creating a `Workload` or any `Job` -- no
  /// database write happens on this call path at all (see
  /// docs/architecture/input-processing.md, "Preview"). Supported for
  /// `(Image|Screenshot, Users)` when an image extractor was injected at
  /// construction, and for `(Csv|Image|Screenshot, Products)` (Csv always
  /// available; Image/Screenshot the same extractor-availability
  /// condition as Users) -- every other combination (including
  /// `Csv`+`Users`, which has no preview step -- see
  /// docs/architecture/input-processing.md, "Why CSV has no preview
  /// step") returns `ErrorCode::Validation`.
  [[nodiscard]] Result<PreviewResult> preview(const ProcessRequest& request);

  /// Submits `request.records` into the workload pipeline -- the same
  /// create-then-schedule path `process()` uses for CSV, just fed by
  /// already-extracted records instead of a fresh parse. Every record is
  /// re-validated (see `ConfirmRequest`'s class comment); a record that
  /// fails re-validation is reported in the returned `ProcessResult`'s
  /// `rejected_records` exactly like an invalid CSV row is, never
  /// silently dropped or silently accepted. Supported for `target ==
  /// Users` or `target == Products`.
  [[nodiscard]] Result<ProcessResult> confirm(const ConfirmRequest& request);

 private:
  std::shared_ptr<WorkloadService> workload_service_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<engine::IInputExtractor> image_extractor_;
  /// Stateless, no injected dependency needed (unlike `image_extractor_`)
  /// -- held directly rather than via a constructor parameter, since
  /// every caller wants the same, only, `CsvExtractor` implementation.
  extractors::CsvExtractor csv_extractor_;
};

}  // namespace flowforge::services
