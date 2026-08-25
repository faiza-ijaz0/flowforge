#include "flowforge/services/job_service.hpp"

namespace flowforge::services {

namespace {
constexpr std::size_t kMaxPayloadBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxQueueNameLength = 128;
}  // namespace

Result<domain::Job> JobService::create_job(const CreateJobRequest& request) {
  if (request.queue_name.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "queue_name must not be empty"));
  }
  if (request.queue_name.size() > kMaxQueueNameLength) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "queue_name must be <= " + std::to_string(kMaxQueueNameLength) + " characters"));
  }
  if (request.payload.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "payload must not be empty"));
  }
  if (request.payload.size() > kMaxPayloadBytes) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const domain::RetryPolicy retry_policy = request.retry_policy.value_or(domain::RetryPolicy{});
  if (retry_policy.max_attempts == 0) {
    return std::unexpected(make_error(ErrorCode::Validation, "retry_policy.max_attempts must be >= 1"));
  }

  domain::Job job(infra::JobId::generate(), request.queue_name, request.payload, retry_policy, clock_->now(),
                  request.priority);

  auto inserted = repository_->insert(job);
  if (!inserted) {
    return std::unexpected(inserted.error());
  }

  logger_->info("job_service", "job created",
                {{.key = "job_id", .value = job.id().value()}, {.key = "queue", .value = job.queue_name()}});
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

}  // namespace flowforge::services
