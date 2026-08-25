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

/// Domain representation of a job. Deliberately does not depend on any
/// JSON/serialization library: `payload` is an opaque, already-serialized
/// string owned by the caller. Translating between wire format (HTTP JSON
/// body) and this type is the API layer's responsibility (see
/// apps/server), which keeps the engine reusable from non-HTTP entry
/// points later (a CLI, a gRPC endpoint, etc.) without dragging a JSON
/// dependency through the domain model.
class Job {
 public:
  Job(infra::JobId id, std::string queue_name, std::string payload, RetryPolicy retry_policy,
      infra::TimePoint created_at, int priority = 0)
      : id_(std::move(id)),
        queue_name_(std::move(queue_name)),
        payload_(std::move(payload)),
        retry_policy_(retry_policy),
        priority_(priority),
        created_at_(created_at),
        updated_at_(created_at) {}

  [[nodiscard]] const infra::JobId& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& queue_name() const noexcept { return queue_name_; }
  [[nodiscard]] const std::string& payload() const noexcept { return payload_; }
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
    status_ = JobStatus::Succeeded;
    updated_at_ = now;
  }

 private:
  infra::JobId id_;
  std::string queue_name_;
  std::string payload_;
  RetryPolicy retry_policy_;
  int priority_;
  JobStatus status_ = JobStatus::Pending;
  std::uint32_t attempt_count_ = 0;
  std::optional<std::string> last_error_;
  infra::TimePoint created_at_;
  infra::TimePoint updated_at_;
};

}  // namespace flowforge::domain
