#include "flowforge/services/workload_service.hpp"

namespace flowforge::services {

namespace {
constexpr std::size_t kMaxWorkloadTypeLength = 128;
/// Bounds a single workload's item count. Chosen to comfortably cover the
/// product direction's stated range ("50, 100, 500+ users") with headroom,
/// while still bounding the cost of a single synchronous HTTP request that
/// creates+schedules one Job per item (see create_workload()) -- avoids an
/// arbitrarily large request holding the HTTP thread indefinitely. Larger
/// bulk imports are Phase 3B's concern (chunked/asynchronous CSV upload).
constexpr std::size_t kMaxWorkloadItems = 1000;
/// Mirrors `JobService`'s per-job kMaxPayloadBytes convention, but smaller:
/// a single workload item (one user-import row) is expected to be a small,
/// flat record, not an arbitrary job payload.
constexpr std::size_t kMaxItemPayloadBytes = std::size_t{64} * 1024;
}  // namespace

Result<CreateWorkloadResult> WorkloadService::create_workload(const CreateWorkloadRequest& request) {
  // Every early-return below is a caller-input rejection (ErrorCode::
  // Validation), mirroring JobService::create_job's logging/metrics
  // convention (see docs/architecture/execution-model.md, "Logging
  // policy").
  if (request.type.empty()) {
    logger_->warn("workload_service", "workload creation rejected: type must not be empty", {});
    if (metrics_) {
      metrics_->increment_counter("flowforge_workloads_rejected_total");
    }
    return std::unexpected(make_error(ErrorCode::Validation, "type must not be empty"));
  }
  if (request.type.size() > kMaxWorkloadTypeLength) {
    logger_->warn("workload_service", "workload creation rejected: type too long",
                  {{.key = "length", .value = std::to_string(request.type.size())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_workloads_rejected_total");
    }
    return std::unexpected(make_error(
        ErrorCode::Validation, "type must be <= " + std::to_string(kMaxWorkloadTypeLength) + " characters"));
  }
  if (request.items.size() > kMaxWorkloadItems) {
    logger_->warn("workload_service", "workload creation rejected: too many items",
                  {{.key = "count", .value = std::to_string(request.items.size())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_workloads_rejected_total");
    }
    return std::unexpected(make_error(
        ErrorCode::Validation, "items must contain <= " + std::to_string(kMaxWorkloadItems) + " entries"));
  }
  for (const auto& item : request.items) {
    if (item.payload.empty()) {
      logger_->warn("workload_service", "workload creation rejected: item payload must not be empty", {});
      if (metrics_) {
        metrics_->increment_counter("flowforge_workloads_rejected_total");
      }
      return std::unexpected(make_error(ErrorCode::Validation, "every item payload must not be empty"));
    }
    if (item.payload.size() > kMaxItemPayloadBytes) {
      logger_->warn("workload_service", "workload creation rejected: item payload too large",
                    {{.key = "size_bytes", .value = std::to_string(item.payload.size())}});
      if (metrics_) {
        metrics_->increment_counter("flowforge_workloads_rejected_total");
      }
      return std::unexpected(
          make_error(ErrorCode::Validation,
                     "every item payload must be <= " + std::to_string(kMaxItemPayloadBytes) + " bytes"));
    }
  }

  domain::Workload workload(infra::WorkloadId::generate(), request.type, request.items.size(), clock_->now());
  if (auto inserted = workload_repository_->insert(workload); !inserted) {
    return std::unexpected(inserted.error());
  }
  if (metrics_) {
    metrics_->increment_counter("flowforge_workloads_created_total");
  }
  logger_->info("workload_service", "workload created",
                {{.key = "workload_id", .value = workload.id().value()},
                 {.key = "type", .value = workload.type()},
                 {.key = "total_items", .value = std::to_string(workload.total_items())}});

  // One Job per item, created-then-scheduled exactly like POST
  // /api/v1/jobs (job_routes.cpp) -- a per-item failure is reported in
  // that item's outcome and never aborts the loop or fails this call: the
  // workload and every other item's Job are genuinely created either way.
  // See docs/architecture/workload-model.md, "Partial failure during
  // dispatch".
  std::vector<WorkloadItemDispatchOutcome> outcomes;
  outcomes.reserve(request.items.size());
  for (const auto& item : request.items) {
    CreateJobRequest job_request;
    job_request.queue_name = request.type;
    job_request.payload = item.payload;
    job_request.job_type = request.type;
    job_request.workload_id = workload.id();

    auto created = job_service_->create_job(job_request);
    if (!created) {
      outcomes.push_back({.job_id = infra::JobId{}, .scheduled = false, .reason = created.error().message()});
      continue;
    }

    WorkloadItemDispatchOutcome outcome{.job_id = created->id(), .scheduled = false, .reason = std::nullopt};
    auto scheduled = scheduler_->schedule(*created);
    if (scheduled) {
      auto queued = job_service_->mark_queued(created->id().value());
      if (queued) {
        outcome.scheduled = true;
      } else {
        outcome.reason = queued.error().message();
      }
    } else {
      outcome.reason = scheduled.error().message();
    }
    if (metrics_) {
      metrics_->increment_counter("flowforge_workload_items_submitted_total");
    }
    outcomes.push_back(std::move(outcome));
  }

  // Every dispatched item's Job row is already persisted at this point
  // (JobService::create_job/mark_queued both write synchronously) -- reuse
  // with_progress() rather than returning the freshly-constructed,
  // default-progress `workload` object, so the create response reflects
  // the same live-computed status/counts as GET /api/v1/workloads/{id}
  // (e.g. a zero-item workload must come back Succeeded here too, not
  // its default Pending).
  auto enriched = with_progress(std::move(workload));
  if (!enriched) {
    return std::unexpected(enriched.error());
  }
  return CreateWorkloadResult{.workload = std::move(*enriched), .items = std::move(outcomes)};
}

Result<domain::Workload> WorkloadService::get_workload(const std::string& id) const {
  if (id.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "workload id must not be empty"));
  }
  auto workload = workload_repository_->find_by_id(infra::WorkloadId{id});
  if (!workload) {
    return std::unexpected(workload.error());
  }
  return with_progress(std::move(*workload));
}

Result<std::vector<domain::Workload>> WorkloadService::list_workloads(std::size_t limit,
                                                                      std::size_t offset) const {
  constexpr std::size_t kMaxLimit = 500;
  if (limit == 0 || limit > kMaxLimit) {
    limit = kMaxLimit;
  }
  auto workloads = workload_repository_->list(limit, offset);
  if (!workloads) {
    return std::unexpected(workloads.error());
  }
  std::vector<domain::Workload> result;
  result.reserve(workloads->size());
  for (auto& workload : *workloads) {
    auto enriched = with_progress(std::move(workload));
    if (!enriched) {
      return std::unexpected(enriched.error());
    }
    result.push_back(std::move(*enriched));
  }
  return result;
}

Result<std::size_t> WorkloadService::count_workloads() const {
  return workload_repository_->count();
}

Result<WorkloadItemsPage> WorkloadService::list_items(const std::string& workload_id, std::size_t limit,
                                                      std::size_t offset) const {
  if (workload_id.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "workload id must not be empty"));
  }
  auto workload = workload_repository_->find_by_id(infra::WorkloadId{workload_id});
  if (!workload) {
    return std::unexpected(workload.error());
  }

  constexpr std::size_t kMaxLimit = 200;
  if (limit == 0 || limit > kMaxLimit) {
    limit = kMaxLimit;
  }
  auto jobs = job_repository_->list_by_workload_id(workload->id(), limit, offset);
  if (!jobs) {
    return std::unexpected(jobs.error());
  }
  return WorkloadItemsPage{.jobs = std::move(*jobs), .total = workload->total_items()};
}

Result<domain::Workload> WorkloadService::with_progress(domain::Workload workload) const {
  // Bounded by total_items() -- itself bounded by kMaxWorkloadItems at
  // creation time -- so this can never silently truncate a real
  // workload's job list. offset=0: this needs every child job, not a page
  // of them (see list_items() for the paginated, UI-facing equivalent).
  auto jobs = job_repository_->list_by_workload_id(workload.id(), workload.total_items(), 0);
  if (!jobs) {
    return std::unexpected(jobs.error());
  }
  std::size_t queued = 0;
  std::size_t running = 0;
  std::size_t succeeded = 0;
  std::size_t failed = 0;
  std::size_t retrying = 0;
  std::size_t dead_letter = 0;
  for (const auto& job : *jobs) {
    switch (domain::classify_job_status_for_workload(job.status())) {
      case domain::WorkloadItemOutcome::Queued:
        ++queued;
        break;
      case domain::WorkloadItemOutcome::Running:
        ++running;
        break;
      case domain::WorkloadItemOutcome::Succeeded:
        ++succeeded;
        break;
      case domain::WorkloadItemOutcome::Failed:
        ++failed;
        break;
    }
    // Sub-counts within the Queued/Failed buckets above (Phase 3G) -- see
    // Workload::retrying_items()/dead_letter_items() doc comments for why
    // these don't change classify_job_status_for_workload's four-bucket
    // status-derivation classification.
    if (job.status() == domain::JobStatus::Retrying) {
      ++retrying;
    } else if (job.status() == domain::JobStatus::DeadLetter) {
      ++dead_letter;
    }
  }
  workload.apply_progress(queued, running, succeeded, failed, retrying, dead_letter);
  return workload;
}

}  // namespace flowforge::services
