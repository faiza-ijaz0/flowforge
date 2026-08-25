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
    service = std::make_unique<JobService>(repository, clock, logger);
  }

  std::shared_ptr<persistence::InMemoryJobRepository> repository;
  std::shared_ptr<infra::ManualClock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::unique_ptr<JobService> service;
};

TEST_F(JobServiceTest, CreateJobSucceedsWithValidRequest) {
  CreateJobRequest request{.queue_name = "emails",
                           .payload = R"({"to":"a@example.com"})",
                           .priority = 0,
                           .retry_policy = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->queue_name(), "emails");
  EXPECT_EQ(result->status(), domain::JobStatus::Pending);
}

TEST_F(JobServiceTest, CreateJobRejectsEmptyQueueName) {
  CreateJobRequest request{.queue_name = "", .payload = "{}", .priority = 0, .retry_policy = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, CreateJobRejectsEmptyPayload) {
  CreateJobRequest request{
      .queue_name = "emails", .payload = "", .priority = 0, .retry_policy = std::nullopt};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, CreateJobRejectsZeroMaxAttempts) {
  domain::RetryPolicy policy;
  policy.max_attempts = 0;
  CreateJobRequest request{.queue_name = "emails", .payload = "{}", .priority = 0, .retry_policy = policy};
  auto result = service->create_job(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(JobServiceTest, GetJobReturnsPreviouslyCreatedJob) {
  auto created = service->create_job(
      {.queue_name = "emails", .payload = "{}", .priority = 0, .retry_policy = std::nullopt});
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
  std::ignore =
      service->create_job({.queue_name = "a", .payload = "{}", .priority = 0, .retry_policy = std::nullopt});
  std::ignore =
      service->create_job({.queue_name = "b", .payload = "{}", .priority = 0, .retry_policy = std::nullopt});
  auto listed = service->list_jobs(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(listed->size(), 2u);
}

TEST_F(JobServiceTest, CancelJobTransitionsToCancelled) {
  auto created = service->create_job(
      {.queue_name = "emails", .payload = "{}", .priority = 0, .retry_policy = std::nullopt});
  ASSERT_TRUE(created.has_value());
  auto cancelled = service->cancel_job(created->id().value());
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->status(), domain::JobStatus::Cancelled);
}

TEST_F(JobServiceTest, CancelJobTwiceReturnsConflict) {
  auto created = service->create_job(
      {.queue_name = "emails", .payload = "{}", .priority = 0, .retry_policy = std::nullopt});
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

}  // namespace
}  // namespace flowforge::services
