#include "flowforge/services/job_service.hpp"

#include <gtest/gtest.h>

#include <tuple>

#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::services {
namespace {

class JobServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repository = std::make_shared<persistence::InMemoryJobRepository>();
    clock = std::make_shared<infra::ManualClock>();
    logger = infra::make_logger(infra::LogLevel::Off, false);
    metrics = infra::make_in_memory_metrics_registry();
    service = std::make_unique<JobService>(repository, clock, logger, metrics);
  }

  std::shared_ptr<persistence::InMemoryJobRepository> repository;
  std::shared_ptr<infra::ManualClock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::shared_ptr<infra::MetricsRegistry> metrics;
  std::unique_ptr<JobService> service;
};

TEST_F(JobServiceTest, CreateJobSucceedsWithValidRequest) {
  CreateJobRequest request{.queue_name = "emails",
                           .payload = R"({"to":"a@example.com"})",
                           .priority = 0,
                           .retry_policy = std::nullopt,
                           .job_type = "",
                           .workload_id = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->queue_name(), "emails");
  EXPECT_EQ(result->status(), domain::JobStatus::Pending);
}

TEST_F(JobServiceTest, CreateJobRejectsEmptyQueueName) {
  CreateJobRequest request{.queue_name = "",
                           .payload = "{}",
                           .priority = 0,
                           .retry_policy = std::nullopt,
                           .job_type = "",
                           .workload_id = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, CreateJobRejectsEmptyPayload) {
  CreateJobRequest request{.queue_name = "emails",
                           .payload = "",
                           .priority = 0,
                           .retry_policy = std::nullopt,
                           .job_type = "",
                           .workload_id = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, CreateJobRejectsZeroMaxAttempts) {
  domain::RetryPolicy policy;
  policy.max_attempts = 0;
  CreateJobRequest request{.queue_name = "emails",
                           .payload = "{}",
                           .priority = 0,
                           .retry_policy = policy,
                           .workload_id = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, GetJobReturnsPreviouslyCreatedJob) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());
  auto fetched = service->get_job(created->id().value());
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->id(), created->id());
}

TEST_F(JobServiceTest, GetJobReturnsNotFoundForUnknownId) {
  auto result = service->get_job("does-not-exist");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST_F(JobServiceTest, ListJobsReturnsAllCreatedJobs) {
  std::ignore = service->create_job({.queue_name = "a",
                                     .payload = "{}",
                                     .priority = 0,
                                     .retry_policy = std::nullopt,
                                     .job_type = "",
                                     .workload_id = std::nullopt});
  std::ignore = service->create_job({.queue_name = "b",
                                     .payload = "{}",
                                     .priority = 0,
                                     .retry_policy = std::nullopt,
                                     .job_type = "",
                                     .workload_id = std::nullopt});
  auto listed = service->list_jobs(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(listed->size(), 2u);
}

TEST_F(JobServiceTest, CancelJobTransitionsToCancelled) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());
  auto cancelled = service->cancel_job(created->id().value());
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->status(), domain::JobStatus::Cancelled);
}

TEST_F(JobServiceTest, CancelJobTwiceReturnsConflict) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(service->cancel_job(created->id().value()).has_value());

  auto second_cancel = service->cancel_job(created->id().value());
  ASSERT_FALSE(second_cancel.has_value());
  EXPECT_EQ(second_cancel.error().code(), ErrorCode::Conflict);
}

TEST_F(JobServiceTest, CancelUnknownJobReturnsNotFound) {
  auto result = service->cancel_job("does-not-exist");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST_F(JobServiceTest, CreateJobDefaultsJobTypeToEmpty) {
  auto result = service->create_job({.queue_name = "emails",
                                     .payload = "{}",
                                     .priority = 0,
                                     .retry_policy = std::nullopt,
                                     .job_type = "",
                                     .workload_id = std::nullopt});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->job_type().empty());
}

TEST_F(JobServiceTest, CreateJobPersistsProvidedJobType) {
  auto result = service->create_job({.queue_name = "emails",
                                     .payload = "{}",
                                     .priority = 0,
                                     .retry_policy = std::nullopt,
                                     .job_type = "echo",
                                     .workload_id = std::nullopt});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->job_type(), "echo");

  auto fetched = service->get_job(result->id().value());
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->job_type(), "echo");
}

TEST_F(JobServiceTest, CreateJobRejectsOverlyLongJobType) {
  auto result = service->create_job({.queue_name = "emails",
                                     .payload = "{}",
                                     .priority = 0,
                                     .retry_policy = std::nullopt,
                                     .job_type = std::string(200, 'x'),
                                     .workload_id = std::nullopt});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, MarkQueuedTransitionsFromPendingToQueued) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());

  auto queued = service->mark_queued(created->id().value());
  ASSERT_TRUE(queued.has_value());
  EXPECT_EQ(queued->status(), domain::JobStatus::Queued);
}

TEST_F(JobServiceTest, MarkQueuedOnTerminalJobReturnsConflict) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(service->cancel_job(created->id().value()).has_value());

  auto queued = service->mark_queued(created->id().value());
  ASSERT_FALSE(queued.has_value());
  EXPECT_EQ(queued.error().code(), ErrorCode::Conflict);
}

TEST_F(JobServiceTest, MarkQueuedOnUnknownJobReturnsNotFound) {
  auto result = service->mark_queued("does-not-exist");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

// --- Phase 2B-5: observability -----------------------------------------

TEST_F(JobServiceTest, RejectedCreateJobIncrementsRejectedCounter) {
  CreateJobRequest request{.queue_name = "",
                           .payload = "{}",
                           .priority = 0,
                           .retry_policy = std::nullopt,
                           .job_type = "",
                           .workload_id = std::nullopt};
  ASSERT_FALSE(service->create_job(request).has_value());

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.counters.find("flowforge_jobs_rejected_total");
  ASSERT_NE(it, snapshot.counters.end());
  EXPECT_EQ(it->second, 1);
}

TEST_F(JobServiceTest, SuccessfulCreateJobDoesNotIncrementRejectedCounter) {
  ASSERT_TRUE(service
                  ->create_job({.queue_name = "emails",
                                .payload = "{}",
                                .priority = 0,
                                .retry_policy = std::nullopt,
                                .job_type = "",
                                .workload_id = std::nullopt})
                  .has_value());

  const auto snapshot = metrics->snapshot();
  EXPECT_EQ(snapshot.counters.find("flowforge_jobs_rejected_total"), snapshot.counters.end());
}

TEST_F(JobServiceTest, MarkQueuedIncrementsQueuedCounter) {
  auto created = service->create_job({.queue_name = "emails",
                                      .payload = "{}",
                                      .priority = 0,
                                      .retry_policy = std::nullopt,
                                      .job_type = "",
                                      .workload_id = std::nullopt});
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(service->mark_queued(created->id().value()).has_value());

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.counters.find("flowforge_jobs_queued_total");
  ASSERT_NE(it, snapshot.counters.end());
  EXPECT_EQ(it->second, 1);
}

}  // namespace
}  // namespace flowforge::services
