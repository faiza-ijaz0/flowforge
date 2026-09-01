#include "flowforge/domain/job.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

infra::TimePoint fixed_time() {
  return std::chrono::system_clock::from_time_t(1'700'000'000);
}

TEST(JobTest, NewJobStartsPending) {
  Job job(infra::JobId::generate(), "default", R"({"a":1})", RetryPolicy{}, fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Pending);
  EXPECT_EQ(job.attempt_count(), 0u);
  EXPECT_FALSE(job.last_error().has_value());
}

TEST(JobTest, TransitionUpdatesStatusAndTimestamp) {
  Job job(infra::JobId::generate(), "default", "payload", RetryPolicy{}, fixed_time());
  const auto later = fixed_time() + std::chrono::seconds(5);
  job.transition_to(JobStatus::Queued, later);
  EXPECT_EQ(job.status(), JobStatus::Queued);
  EXPECT_EQ(job.updated_at(), later);
  EXPECT_EQ(job.created_at(), fixed_time());
}

TEST(JobTest, RecordAttemptFailureRetriesUntilExhausted) {
  RetryPolicy policy;
  policy.max_attempts = 2;
  Job job(infra::JobId::generate(), "default", "payload", policy, fixed_time());

  job.record_attempt_failure("boom", fixed_time());
  EXPECT_EQ(job.attempt_count(), 1u);
  EXPECT_EQ(job.status(), JobStatus::Retrying);
  const auto& last_error = job.last_error();
  ASSERT_TRUE(last_error.has_value());
  EXPECT_EQ(*last_error, "boom");

  job.record_attempt_failure("boom again", fixed_time());
  EXPECT_EQ(job.attempt_count(), 2u);
  EXPECT_EQ(job.status(), JobStatus::DeadLetter);
}

TEST(JobTest, RecordAttemptSuccessMarksSucceeded) {
  Job job(infra::JobId::generate(), "default", "payload", RetryPolicy{}, fixed_time());
  job.record_attempt_success(fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Succeeded);
  // A successful execution is still an attempt: attempt_count must reflect
  // it the same way record_attempt_failure()/record_execution_failure() do,
  // so it agrees with the execution_manager's attempt history (see
  // JobExecutor::execute(), which computes attempt_number from
  // job.attempt_count() + 1 before this call).
  EXPECT_EQ(job.attempt_count(), 1u);
}

TEST(JobTest, RecordAttemptSuccessAfterPriorFailuresCountsAllAttempts) {
  RetryPolicy policy;
  policy.max_attempts = 3;
  Job job(infra::JobId::generate(), "default", "payload", policy, fixed_time());

  job.record_attempt_failure("first failure", fixed_time());
  EXPECT_EQ(job.attempt_count(), 1u);
  EXPECT_EQ(job.status(), JobStatus::Retrying);

  job.record_attempt_success(fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Succeeded);
  // Total attempts made (1 failure + 1 success) must be reflected, not
  // reset or left frozen at the failure count.
  EXPECT_EQ(job.attempt_count(), 2u);
}

// Phase 2B-4 backward compatibility: a job created without any explicit
// retry_policy override (the common case -- services::CreateJobRequest::
// retry_policy is std::optional and JobService::create_job() defaults it
// to RetryPolicy{} -- see job_service.cpp) must retry exactly like any
// other job, using the default 3-attempt policy, with no special-casing
// anywhere in domain::Job for "no retry configuration".
TEST(JobTest, DefaultRetryPolicyAllowsThreeAttemptsBeforeDeadLetter) {
  Job job(infra::JobId::generate(), "default", "payload", RetryPolicy{}, fixed_time());
  ASSERT_EQ(job.retry_policy().max_attempts, 3u);

  job.record_attempt_failure("first", fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Retrying);
  job.record_attempt_failure("second", fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Retrying);
  job.record_attempt_failure("third", fixed_time());
  EXPECT_EQ(job.status(), JobStatus::DeadLetter);
  EXPECT_EQ(job.attempt_count(), 3u);
}

TEST(JobTest, RecordExecutionFailureGoesStraightToFailedNotRetrying) {
  RetryPolicy policy;
  policy.max_attempts = 5;  // plenty of attempts remaining -- must not matter.
  Job job(infra::JobId::generate(), "default", "payload", policy, fixed_time());

  job.record_execution_failure("handler exploded", fixed_time());
  EXPECT_EQ(job.status(), JobStatus::Failed);
  EXPECT_EQ(job.attempt_count(), 1u);
  ASSERT_TRUE(job.last_error().has_value());
  EXPECT_EQ(*job.last_error(), "handler exploded");
}

TEST(JobTest, RestoreReconstructsFullPersistedState) {
  const auto created = fixed_time();
  const auto updated = fixed_time() + std::chrono::minutes(3);
  RetryPolicy policy;
  policy.max_attempts = 5;

  Job restored = Job::restore(infra::JobId::generate(), "emails", R"({"a":1})", policy, /*priority=*/7,
                              JobStatus::Failed, /*attempt_count=*/2, std::string("boom"), created, updated);

  EXPECT_EQ(restored.queue_name(), "emails");
  EXPECT_EQ(restored.payload(), R"({"a":1})");
  EXPECT_EQ(restored.retry_policy().max_attempts, 5u);
  EXPECT_EQ(restored.priority(), 7);
  EXPECT_EQ(restored.status(), JobStatus::Failed);
  EXPECT_EQ(restored.attempt_count(), 2u);
  ASSERT_TRUE(restored.last_error().has_value());
  EXPECT_EQ(*restored.last_error(), "boom");
  EXPECT_EQ(restored.created_at(), created);
  EXPECT_EQ(restored.updated_at(), updated);
}

TEST(JobTest, RestoreWithNoLastErrorLeavesItEmpty) {
  Job restored = Job::restore(infra::JobId::generate(), "default", "payload", RetryPolicy{}, 0,
                              JobStatus::Pending, 0, std::nullopt, fixed_time(), fixed_time());
  EXPECT_FALSE(restored.last_error().has_value());
}

TEST(JobTest, JobStatusFromStringRoundTripsToString) {
  for (auto status : {JobStatus::Pending, JobStatus::Queued, JobStatus::Running, JobStatus::Succeeded,
                      JobStatus::Failed, JobStatus::Retrying, JobStatus::Cancelled, JobStatus::DeadLetter}) {
    auto parsed = job_status_from_string(to_string(status));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, status);
  }
  EXPECT_FALSE(job_status_from_string("not_a_status").has_value());
}

TEST(JobTest, TerminalStatusesAreIdentifiedCorrectly) {
  EXPECT_TRUE(is_terminal(JobStatus::Succeeded));
  EXPECT_TRUE(is_terminal(JobStatus::Cancelled));
  EXPECT_TRUE(is_terminal(JobStatus::DeadLetter));
  EXPECT_FALSE(is_terminal(JobStatus::Pending));
  EXPECT_FALSE(is_terminal(JobStatus::Queued));
  EXPECT_FALSE(is_terminal(JobStatus::Running));
  EXPECT_FALSE(is_terminal(JobStatus::Retrying));
  EXPECT_FALSE(is_terminal(JobStatus::Failed));
}

}  // namespace
}  // namespace flowforge::domain
