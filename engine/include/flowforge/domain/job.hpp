#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "flowforge/domain/retry_policy.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

enum class JobStatus : std::uint8_t {
  Pending,     ///< Created, not yet placed on a queue.
  Queued,      ///< On a queue, waiting for a worker.
  Running,     ///< Currently executing on a worker.
  Succeeded,   ///< Completed successfully.
  Failed,      ///< Most recent attempt failed; may still be retried.
  Retrying,    ///< Failed and scheduled for another attempt.
  Cancelled,   ///< Cancelled by a caller before/while running.
  DeadLetter,  ///< Retries exhausted; requires manual intervention.
};

[[nodiscard]] std::string_view to_string(JobStatus status) noexcept;
[[nodiscard]] bool is_terminal(JobStatus status) noexcept;

/// Inverse of to_string(JobStatus). Used by persistence implementations
/// mapping a stored status column back to the enum; returns std::nullopt
/// for any value that isn't one of the known status strings (e.g. a row
/// written by a future FlowForge version).
[[nodiscard]] std::optional<JobStatus> job_status_from_string(std::string_view value) noexcept;

/// Domain representation of a job. Deliberately does not depend on any
/// JSON/serialization library: `payload` is an opaque, already-serialized
/// string owned by the caller. Translating between wire format (HTTP JSON
/// body) and this type is the API layer's responsibility (see
/// apps/server), which keeps the engine reusable from non-HTTP entry
/// points later (a CLI, a gRPC endpoint, etc.) without dragging a JSON
/// dependency through the domain model.
///
/// `job_type` (e.g. "echo", "delay") is the key the future Executor will
/// use to resolve an `engine::IJobHandler` from an `engine::HandlerRegistry`
/// (see engine/handler_registry.hpp) -- distinct from `queue_name`, which is
/// about *routing/capacity* (which logical queue a job waits on), not
/// *what code runs*. It defaults to "" and is deliberately NOT yet wired
/// through `services::JobService::CreateJobRequest`, the HTTP API, or
/// persistence (`PostgresJobRepository`/`InMemoryJobRepository`): nothing
/// in this phase reads it back off a persisted job to dispatch work, so
/// threading it through the create-job API/schema now would be
/// unused plumbing. See docs/architecture/execution-model.md.
class Job {
 public:
  Job(infra::JobId id, std::string queue_name, std::string payload, RetryPolicy retry_policy,
      infra::TimePoint created_at, int priority = 0, std::string job_type = "")
      : id_(std::move(id)),
        queue_name_(std::move(queue_name)),
        payload_(std::move(payload)),
        retry_policy_(retry_policy),
        priority_(priority),
        job_type_(std::move(job_type)),
        created_at_(created_at),
        updated_at_(created_at) {}

  [[nodiscard]] const infra::JobId& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& queue_name() const noexcept { return queue_name_; }
  [[nodiscard]] const std::string& payload() const noexcept { return payload_; }
  [[nodiscard]] const std::string& job_type() const noexcept { return job_type_; }
  [[nodiscard]] const RetryPolicy& retry_policy() const noexcept { return retry_policy_; }
  [[nodiscard]] int priority() const noexcept { return priority_; }
  [[nodiscard]] JobStatus status() const noexcept { return status_; }
  [[nodiscard]] std::uint32_t attempt_count() const noexcept { return attempt_count_; }
  [[nodiscard]] const std::optional<std::string>& last_error() const noexcept { return last_error_; }
  [[nodiscard]] infra::TimePoint created_at() const noexcept { return created_at_; }
  [[nodiscard]] infra::TimePoint updated_at() const noexcept { return updated_at_; }

  void transition_to(JobStatus status, infra::TimePoint now) {
    status_ = status;
    updated_at_ = now;
  }

  void record_attempt_failure(std::string error, infra::TimePoint now) {
    ++attempt_count_;
    last_error_ = std::move(error);
    status_ = retry_policy_.exhausted(attempt_count_) ? JobStatus::DeadLetter : JobStatus::Retrying;
    updated_at_ = now;
  }

  void record_attempt_success(infra::TimePoint now) {
    ++attempt_count_;
    status_ = JobStatus::Succeeded;
    updated_at_ = now;
  }

  /// Records a terminal execution failure: increments `attempt_count`,
  /// sets `last_error`, and transitions straight to `Failed` -- unlike
  /// `record_attempt_failure()`, this never lands on `Retrying`/
  /// `DeadLetter`. It exists because no retry engine runs yet (Phase
  /// 2B-3): landing on `Retrying` with nothing that will ever retry it
  /// would leave the job permanently, misleadingly stuck in a state that
  /// implies a retry is coming. `record_attempt_failure()` remains
  /// reserved for the future retry engine to use once it actually
  /// reschedules retries.
  void record_execution_failure(std::string error, infra::TimePoint now) {
    ++attempt_count_;
    last_error_ = std::move(error);
    status_ = JobStatus::Failed;
    updated_at_ = now;
  }

  /// Reconstructs a Job from already-persisted state -- used by repository
  /// implementations (e.g. PostgresJobRepository) mapping a database row
  /// back to a domain object. Deliberately bypasses transition_to() /
  /// record_attempt_*(): those encode the business rules for deciding a
  /// *new* transition (e.g. consulting retry_policy to decide Retrying vs.
  /// DeadLetter), not for restoring a state a persistence layer already
  /// recorded as valid.
  [[nodiscard]] static Job restore(infra::JobId id, std::string queue_name, std::string payload,
                                   RetryPolicy retry_policy, int priority, JobStatus status,
                                   std::uint32_t attempt_count, std::optional<std::string> last_error,
                                   infra::TimePoint created_at, infra::TimePoint updated_at,
                                   std::string job_type = "");

 private:
  infra::JobId id_;
  std::string queue_name_;
  std::string payload_;
  RetryPolicy retry_policy_;
  int priority_;
  std::string job_type_;
  JobStatus status_ = JobStatus::Pending;
  std::uint32_t attempt_count_ = 0;
  std::optional<std::string> last_error_;
  infra::TimePoint created_at_;
  infra::TimePoint updated_at_;
};

}  // namespace flowforge::domain
