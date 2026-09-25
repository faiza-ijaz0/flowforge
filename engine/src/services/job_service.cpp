#include "flowforge/services/job_service.hpp"

namespace flowforge::services {

namespace {
constexpr std::size_t kMaxPayloadBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxQueueNameLength = 128;
constexpr std::size_t kMaxJobTypeLength = 128;
}  // namespace

Result<domain::Job> JobService::create_job(const CreateJobRequest& request) {
  // Every early-return below is a caller-input rejection (ErrorCode::
  // Validation) at the API boundary -- logged once, at WARN (recoverable:
  // the caller can fix the request and retry), never with the raw
  // payload, only the specific rule that failed and its size/length where
  // relevant. See docs/architecture/execution-model.md, "Logging policy".
  if (request.queue_name.empty()) {
    logger_->warn("job_service", "job creation rejected: queue_name must not be empty", {});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(make_error(ErrorCode::Validation, "queue_name must not be empty"));
  }
  if (request.queue_name.size() > kMaxQueueNameLength) {
    logger_->warn("job_service", "job creation rejected: queue_name too long",
                  {{.key = "length", .value = std::to_string(request.queue_name.size())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "queue_name must be <= " + std::to_string(kMaxQueueNameLength) + " characters"));
  }
  if (request.payload.empty()) {
    logger_->warn("job_service", "job creation rejected: payload must not be empty", {});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(make_error(ErrorCode::Validation, "payload must not be empty"));
  }
  if (request.payload.size() > kMaxPayloadBytes) {
    logger_->warn("job_service", "job creation rejected: payload too large",
                  {{.key = "size_bytes", .value = std::to_string(request.payload.size())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  if (request.job_type.size() > kMaxJobTypeLength) {
    logger_->warn("job_service", "job creation rejected: job_type too long",
                  {{.key = "length", .value = std::to_string(request.job_type.size())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(make_error(
        ErrorCode::Validation, "job_type must be <= " + std::to_string(kMaxJobTypeLength) + " characters"));
  }

  const domain::RetryPolicy retry_policy = request.retry_policy.value_or(domain::RetryPolicy{});
  if (retry_policy.max_attempts == 0) {
    logger_->warn("job_service", "job creation rejected: retry_policy.max_attempts must be >= 1", {});
    if (metrics_) {
      metrics_->increment_counter("flowforge_jobs_rejected_total");
    }
    return std::unexpected(make_error(ErrorCode::Validation, "retry_policy.max_attempts must be >= 1"));
  }

  domain::Job job(infra::JobId::generate(), request.queue_name, request.payload, retry_policy, clock_->now(),
                  request.priority, request.job_type, request.workload_id);

  auto inserted = repository_->insert(job);
  if (!inserted) {
    return std::unexpected(inserted.error());
  }

  logger_->info("job_service", "job created",
                {{.key = "job_id", .value = job.id().value()},
                 {.key = "queue", .value = job.queue_name()},
                 {.key = "job_type", .value = job.job_type()}});
  return job;
}

Result<domain::Job> JobService::get_job(const std::string& id) const {
  if (id.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "job id must not be empty"));
  }
  return repository_->find_by_id(infra::JobId{id});
}

Result<std::vector<domain::Job>> JobService::list_jobs(std::size_t limit, std::size_t offset) const {
  constexpr std::size_t kMaxLimit = 500;
  if (limit == 0 || limit > kMaxLimit) {
    limit = kMaxLimit;
  }
  return repository_->list(limit, offset);
}

Result<std::size_t> JobService::count_jobs() const {
  return repository_->count();
}

Result<domain::Job> JobService::cancel_job(const std::string& id) {
  auto found = repository_->find_by_id(infra::JobId{id});
  if (!found) {
    return std::unexpected(found.error());
  }
  domain::Job job = *found;
  if (domain::is_terminal(job.status())) {
    return std::unexpected(make_error(ErrorCode::Conflict, "job '" + id + "' is already in terminal state '" +
                                                               std::string(domain::to_string(job.status())) +
                                                               "'"));
  }
  job.transition_to(domain::JobStatus::Cancelled, clock_->now());
  auto updated = repository_->update(job);
  if (!updated) {
    return std::unexpected(updated.error());
  }
  logger_->info("job_service", "job cancelled", {{.key = "job_id", .value = id}});
  return job;
}

Result<void> JobService::revert_queued(const domain::Job& previous) {
  auto restored = repository_->update(previous);
  if (!restored) {
    logger_->error("job_service", "failed to revert job after scheduler rejection",
                   {{.key = "job_id", .value = previous.id().value()},
                    {.key = "error", .value = restored.error().message()}});
  }
  return restored;
}

Result<domain::Job> JobService::mark_queued(const std::string& id) {
  auto found = repository_->find_by_id(infra::JobId{id});
  if (!found) {
    return std::unexpected(found.error());
  }
  domain::Job job = *found;
  if (domain::is_terminal(job.status())) {
    return std::unexpected(make_error(ErrorCode::Conflict, "job '" + id + "' is already in terminal state '" +
                                                               std::string(domain::to_string(job.status())) +
                                                               "'"));
  }
  job.transition_to(domain::JobStatus::Queued, clock_->now());
  auto updated = repository_->update(job);
  if (!updated) {
    return std::unexpected(updated.error());
  }
  if (metrics_) {
    metrics_->increment_counter("flowforge_jobs_queued_total");
  }
  logger_->info("job_service", "job queued", {{.key = "job_id", .value = id}});
  return job;
}

}  // namespace flowforge::services
