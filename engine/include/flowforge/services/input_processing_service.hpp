#pragma once

#include <memory>
#include <string>
#include <vector>

#include "flowforge/domain/input_source.hpp"
#include "flowforge/domain/processing_target.hpp"
#include "flowforge/domain/structured_record.hpp"
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
/// since a future non-CSV source has no literal "row").
struct ProcessResult {
  domain::Workload workload;
  std::vector<WorkloadItemDispatchOutcome> items;
  std::size_t total_records = 0;
  std::size_t valid_records = 0;
  std::size_t invalid_records = 0;
  std::vector<domain::RejectedRecord> rejected_records;
  bool rejected_records_truncated = false;
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
class InputProcessingService {
 public:
  InputProcessingService(std::shared_ptr<WorkloadService> workload_service,
                         std::shared_ptr<infra::Logger> logger,
                         std::shared_ptr<infra::MetricsRegistry> metrics = nullptr)
      : workload_service_(std::move(workload_service)),
        logger_(std::move(logger)),
        metrics_(std::move(metrics)) {}

  /// Returns `ErrorCode::Validation` immediately -- without attempting
  /// any extraction -- for a `(request.source_type, request.target)`
  /// combination that isn't implemented yet (every combination except
  /// CSV+Users this phase). Never a fake success, never a workload
  /// created for an unsupported combination -- see
  /// docs/architecture/input-processing.md, "What's implemented vs.
  /// foundation-only".
  [[nodiscard]] Result<ProcessResult> process(const ProcessRequest& request);

 private:
  std::shared_ptr<WorkloadService> workload_service_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::services
