#include "flowforge/domain/workload.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

infra::TimePoint fixed_time() {
  return std::chrono::system_clock::from_time_t(1'700'000'000);
}

TEST(WorkloadTest, NewWorkloadStartsPendingWithZeroProgress) {
  Workload workload(infra::WorkloadId::generate(), "user.process", 3, fixed_time());
  EXPECT_EQ(workload.status(), WorkloadStatus::Pending);
  EXPECT_EQ(workload.total_items(), 3u);
  EXPECT_EQ(workload.completed_items(), 0u);
  EXPECT_EQ(workload.failed_items(), 0u);
  EXPECT_EQ(workload.created_at(), fixed_time());
  EXPECT_EQ(workload.updated_at(), fixed_time());
}

TEST(WorkloadTest, RestoreReconstructsPersistedRowWithDefaultProgress) {
  const auto later = fixed_time() + std::chrono::seconds(5);
  auto id = infra::WorkloadId::generate();
  Workload workload = Workload::restore(id, "user.process", 10, fixed_time(), later);
  EXPECT_EQ(workload.id(), id);
  EXPECT_EQ(workload.type(), "user.process");
  EXPECT_EQ(workload.total_items(), 10u);
  EXPECT_EQ(workload.created_at(), fixed_time());
  EXPECT_EQ(workload.updated_at(), later);
  // Progress/status are not persisted -- restore() leaves them at their
  // defaults until apply_progress() is called (see class comment).
  EXPECT_EQ(workload.status(), WorkloadStatus::Pending);
  EXPECT_EQ(workload.completed_items(), 0u);
  EXPECT_EQ(workload.failed_items(), 0u);
}

TEST(WorkloadTest, ApplyProgressRecomputesStatus) {
  Workload workload(infra::WorkloadId::generate(), "user.process", 2, fixed_time());
  workload.apply_progress(1, 0);
  EXPECT_EQ(workload.completed_items(), 1u);
  EXPECT_EQ(workload.failed_items(), 0u);
  EXPECT_EQ(workload.status(), WorkloadStatus::Running);

  workload.apply_progress(2, 0);
  EXPECT_EQ(workload.status(), WorkloadStatus::Succeeded);
}

TEST(DeriveWorkloadStatusTest, ZeroItemsIsSucceeded) {
  EXPECT_EQ(derive_workload_status(0, 0, 0), WorkloadStatus::Succeeded);
}

TEST(DeriveWorkloadStatusTest, ItemsStillActiveIsRunning) {
  EXPECT_EQ(derive_workload_status(5, 2, 0), WorkloadStatus::Running);
  EXPECT_EQ(derive_workload_status(5, 0, 2), WorkloadStatus::Running);
  EXPECT_EQ(derive_workload_status(5, 2, 2), WorkloadStatus::Running);
}

TEST(DeriveWorkloadStatusTest, AllTerminalWithNoFailuresIsSucceeded) {
  EXPECT_EQ(derive_workload_status(5, 5, 0), WorkloadStatus::Succeeded);
}

TEST(DeriveWorkloadStatusTest, AllTerminalWithAnyFailureIsFailed) {
  EXPECT_EQ(derive_workload_status(5, 4, 1), WorkloadStatus::Failed);
  EXPECT_EQ(derive_workload_status(5, 0, 5), WorkloadStatus::Failed);
}

TEST(ClassifyJobStatusForWorkloadTest, SucceededIsCompleted) {
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Succeeded), WorkloadItemOutcome::Completed);
}

TEST(ClassifyJobStatusForWorkloadTest, CancelledAndDeadLetterAreFailed) {
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Cancelled), WorkloadItemOutcome::Failed);
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::DeadLetter), WorkloadItemOutcome::Failed);
}

TEST(ClassifyJobStatusForWorkloadTest, InFlightAndRetryableStatesAreActive) {
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Pending), WorkloadItemOutcome::Active);
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Queued), WorkloadItemOutcome::Active);
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Running), WorkloadItemOutcome::Active);
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Retrying), WorkloadItemOutcome::Active);
  // A failed *attempt* is not yet a workload-level failure -- it may still
  // be retried (see classify_job_status_for_workload's doc comment).
  EXPECT_EQ(classify_job_status_for_workload(JobStatus::Failed), WorkloadItemOutcome::Active);
}

TEST(WorkloadStatusStringTest, RoundTripsThroughAllValues) {
  for (auto status : {WorkloadStatus::Pending, WorkloadStatus::Queued, WorkloadStatus::Running,
                      WorkloadStatus::Succeeded, WorkloadStatus::Failed}) {
    auto parsed = workload_status_from_string(to_string(status));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, status);
  }
  EXPECT_FALSE(workload_status_from_string("not_a_status").has_value());
}

TEST(WorkloadStatusIsTerminalTest, OnlySucceededAndFailedAreTerminal) {
  EXPECT_FALSE(is_terminal(WorkloadStatus::Pending));
  EXPECT_FALSE(is_terminal(WorkloadStatus::Queued));
  EXPECT_FALSE(is_terminal(WorkloadStatus::Running));
  EXPECT_TRUE(is_terminal(WorkloadStatus::Succeeded));
  EXPECT_TRUE(is_terminal(WorkloadStatus::Failed));
}

}  // namespace
}  // namespace flowforge::domain
