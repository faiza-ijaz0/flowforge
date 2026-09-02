#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/workload.hpp"
#include "flowforge/engine/scheduler.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/persistence/workload_repository.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::services {

/// One item submitted as part of a workload's creation request. `payload`
/// is an opaque, already-serialized string -- exactly like
/// `domain::Job::payload()` -- for the same reason: the engine has no JSON
/// dependency (see docs/architecture/overview.md, "Dependency direction").
/// Shape validation of *what's inside* `payload` (e.g. "user.process"
/// requiring name/email) is the target handler's job (see
/// handlers::UserProcessHandler), not WorkloadService's -- WorkloadService
/// only bounds how many items there are and how large each one is, so it
/// stays generic across future workload types (see
/// docs/architecture/workload-model.md, "Why WorkloadService doesn't know
/// about users").
struct WorkloadItem {
  std::string payload;
};

struct CreateWorkloadRequest {
  /// Doubles as the job_type every item's Job is created with -- see
  /// docs/architecture/workload-model.md, "Why type == job_type".
  std::string type;
  std::vector<WorkloadItem> items;
};

/// Per-item outcome of a workload's item-dispatch loop -- mirrors the
/// "scheduling" object `POST /api/v1/jobs` already returns
/// (apps/server/src/http/routes/job_routes.cpp) so both APIs report the
/// same shape of information for the same underlying operation (create a
/// Job, try to schedule it). `job_id` is a default-constructed (empty)
/// JobId when `reason` reflects a rejection from JobService itself (the
/// item never became a persisted Job at all).
struct WorkloadItemDispatchOutcome {
  infra::JobId job_id;
  bool scheduled = false;
  std::optional<std::string> reason;
};

struct CreateWorkloadResult {
  domain::Workload workload;
  std::vector<WorkloadItemDispatchOutcome> items;
};

/// Application-level orchestration for workloads -- the aggregate that
/// groups related jobs submitted as one logical unit (see
/// docs/architecture/workload-model.md). Sits at the same layer as
/// `JobService` and deliberately reuses it rather than duplicating job
/// creation/scheduling logic: `WorkloadService` never writes a `jobs` row
/// or calls `IScheduler` directly for anything `JobService` already does
/// -- it is the same create-then-schedule sequence `POST /api/v1/jobs`
/// already performs (job_routes.cpp), applied once per item.
class WorkloadService {
 public:
  WorkloadService(std::shared_ptr<persistence::IWorkloadRepository> workload_repository,
                  std::shared_ptr<persistence::IJobRepository> job_repository,
                  std::shared_ptr<JobService> job_service, std::shared_ptr<engine::IScheduler> scheduler,
                  std::shared_ptr<infra::Clock> clock, std::shared_ptr<infra::Logger> logger,
                  std::shared_ptr<infra::MetricsRegistry> metrics = nullptr)
      : workload_repository_(std::move(workload_repository)),
        job_repository_(std::move(job_repository)),
        job_service_(std::move(job_service)),
        scheduler_(std::move(scheduler)),
        clock_(std::move(clock)),
        logger_(std::move(logger)),
        metrics_(std::move(metrics)) {}

  /// Validates `request` (type/item count/item size -- see .cpp for the
  /// exact bounds and rationale), creates and persists the Workload row,
  /// then creates one Job per item (job_type == request.type, payload ==
  /// item.payload, workload_id == the new workload's id) via `JobService`
  /// and attempts to schedule each one. A per-item rejection/scheduling
  /// failure (bad payload, unknown job_type, scheduler at capacity) is
  /// reported in that item's `WorkloadItemDispatchOutcome` and never fails
  /// the whole request -- the workload is genuinely created either way
  /// (see docs/architecture/workload-model.md, "Partial failure during
  /// dispatch"). Zero items is valid: it creates a workload with
  /// total_items() == 0, which `domain::derive_workload_status` reports as
  /// immediately Succeeded.
  [[nodiscard]] Result<CreateWorkloadResult> create_workload(const CreateWorkloadRequest& request);

  /// Returns the workload with completed_items()/failed_items()/status()
  /// computed live from its current child Job rows (see
  /// `domain::Workload::apply_progress`) -- never a stale/cached snapshot.
  [[nodiscard]] Result<domain::Workload> get_workload(const std::string& id) const;

  [[nodiscard]] Result<std::vector<domain::Workload>> list_workloads(std::size_t limit,
                                                                     std::size_t offset) const;

 private:
  [[nodiscard]] Result<domain::Workload> with_progress(domain::Workload workload) const;

  std::shared_ptr<persistence::IWorkloadRepository> workload_repository_;
  std::shared_ptr<persistence::IJobRepository> job_repository_;
  std::shared_ptr<JobService> job_service_;
  std::shared_ptr<engine::IScheduler> scheduler_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::services
