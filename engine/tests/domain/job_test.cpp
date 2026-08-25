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
