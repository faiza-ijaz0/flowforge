#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/domain/retry_policy.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/result.hpp"

namespace flowforge::services {

struct CreateJobRequest {
  std::string queue_name;
  std::string payload;
  int priority = 0;
  std::optional<domain::RetryPolicy> retry_policy;
  /// Optional (default ""). If non-empty, the created Job carries this
  /// job_type; JobService only validates its length/format here -- it
  /// does not check that a handler is registered for it (that would
  /// couple JobService to engine::HandlerRegistry, which is Scheduler's
  /// concern; see docs/architecture/execution-model.md).
  std::string job_type = "";
  /// Optional (default nullopt). Set only by `services::WorkloadService`
  /// when creating a job on behalf of a workload item -- the public `POST
  /// /api/v1/jobs` request body never populates this (see
  /// `apps/server/src/json/job_json.cpp::parse_create_job_request`, which
  /// has no `workload_id` field). See docs/architecture/workload-model.md,
  /// "Job <-> Workload relationship".
  std::optional<infra::WorkloadId> workload_id;
};

/// Application-level orchestration for job CRUD, sitting between the
/// transport layer (apps/server's HTTP routes) and the persistence layer.
/// This is where request validation and domain invariants are enforced --
/// route handlers should be a thin translation of HTTP <-> this service,
/// nothing more. Scheduling/execution are intentionally out of scope here
/// (see engine::IScheduler); this phase only supports creating,
/// retrieving, listing, and cancelling job records.
class JobService {
 public:
  JobService(std::shared_ptr<persistence::IJobRepository> repository, std::shared_ptr<infra::Clock> clock,
             std::shared_ptr<infra::Logger> logger, std::shared_ptr<infra::MetricsRegistry> metrics = nullptr)
      : repository_(std::move(repository)),
        clock_(std::move(clock)),
        logger_(std::move(logger)),
        metrics_(std::move(metrics)) {}

  [[nodiscard]] Result<domain::Job> create_job(const CreateJobRequest& request);
  [[nodiscard]] Result<domain::Job> get_job(const std::string& id) const;
  [[nodiscard]] Result<std::vector<domain::Job>> list_jobs(std::size_t limit, std::size_t offset) const;
  [[nodiscard]] Result<domain::Job> cancel_job(const std::string& id);

  /// Total number of jobs, independent of any list_jobs() page -- mirrors
  /// product/category routes' `->count()` use (Phase 3G), so `GET
  /// /api/v1/jobs` can report a `total` the same way those endpoints do.
  [[nodiscard]] Result<std::size_t> count_jobs() const;

  /// Transitions a job from Pending to Queued and persists it. Callers
  /// (job_routes.cpp, WorkloadService) must call this *before*
  /// engine::IScheduler::schedule(), never after: once scheduled, a worker
  /// can execute the job and persist Succeeded at any moment, and a
  /// Queued write landing after that would overwrite the finished job
  /// (a real lost-update race, found by the Phase 3I release run). If
  /// schedule() then rejects the job, undo with revert_queued(). Fails
  /// with ErrorCode::Conflict if the job is already in a terminal state.
  [[nodiscard]] Result<domain::Job> mark_queued(const std::string& id);

  /// Restores `previous` (the job as it was before mark_queued()) after
  /// the scheduler rejected it. Safe only because a rejected job never
  /// reached a worker, so nothing else can have changed its row.
  [[nodiscard]] Result<void> revert_queued(const domain::Job& previous);

 private:
  std::shared_ptr<persistence::IJobRepository> repository_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::services
