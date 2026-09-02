#include "flowforge/services/workload_service.hpp"

#include <gtest/gtest.h>

#include <tuple>

#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::services {
namespace {

class WorkloadServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    workload_repository = std::make_shared<persistence::InMemoryWorkloadRepository>();
    job_repository = std::make_shared<persistence::InMemoryJobRepository>();
    clock = std::make_shared<infra::ManualClock>();
    logger = infra::make_logger(infra::LogLevel::Off, false);
    metrics = infra::make_in_memory_metrics_registry();
    job_service = std::make_shared<JobService>(job_repository, clock, logger, metrics);

    handler_registry = std::make_shared<engine::HandlerRegistry>();
    std::ignore = handlers::register_builtin_handlers(*handler_registry);
    // No IWorkerPool is wired in -- the dispatch loop only resolves the
    // handler (proving the job is routable) and stops there, exactly like
    // job_routes.cpp's own tests that don't need real execution. Jobs
    // dispatched here observably stay Queued, which is sufficient to
    // exercise WorkloadService's create-then-schedule sequence and
    // progress aggregation deterministically.
    scheduler = std::make_shared<engine::PriorityScheduler>(handler_registry, engine::SchedulerConfig{},
                                                            logger, metrics);
    ASSERT_TRUE(scheduler->start().has_value());

    service = std::make_unique<WorkloadService>(workload_repository, job_repository, job_service, scheduler,
                                                clock, logger, metrics);
  }

  std::shared_ptr<persistence::InMemoryWorkloadRepository> workload_repository;
  std::shared_ptr<persistence::InMemoryJobRepository> job_repository;
  std::shared_ptr<infra::ManualClock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::shared_ptr<infra::MetricsRegistry> metrics;
  std::shared_ptr<JobService> job_service;
  std::shared_ptr<engine::HandlerRegistry> handler_registry;
  std::shared_ptr<engine::PriorityScheduler> scheduler;
  std::unique_ptr<WorkloadService> service;
};

TEST_F(WorkloadServiceTest, CreateWorkloadCreatesAndAssociatesJobs) {
  CreateWorkloadRequest request{.type = "user.process",
                                .items = {{R"({"name":"Alice","email":"alice@example.com"})"},
                                          {R"({"name":"Bob","email":"bob@example.com"})"}}};
  auto result = service->create_workload(request);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->workload.total_items(), 2u);
  ASSERT_EQ(result->items.size(), 2u);
  for (const auto& outcome : result->items) {
    EXPECT_TRUE(outcome.scheduled) << outcome.reason.value_or("");
    EXPECT_FALSE(outcome.job_id.empty());
  }

  auto jobs = job_repository->list_by_workload_id(result->workload.id(), 10, 0);
  ASSERT_TRUE(jobs.has_value());
  ASSERT_EQ(jobs->size(), 2u);
  for (const auto& job : *jobs) {
    EXPECT_EQ(job.job_type(), "user.process");
    ASSERT_TRUE(job.workload_id().has_value());
    EXPECT_EQ(*job.workload_id(), result->workload.id());
    // create_workload() mirrors POST /api/v1/jobs's create-then-schedule
    // sequence: a successfully-scheduled item's Job is marked Queued
    // synchronously, before create_workload() returns.
    EXPECT_EQ(job.status(), domain::JobStatus::Queued);
  }
}

TEST_F(WorkloadServiceTest, CreateWorkloadWithZeroItemsIsImmediatelySucceeded) {
  CreateWorkloadRequest request{.type = "user.process", .items = {}};
  auto result = service->create_workload(request);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->workload.total_items(), 0u);
  EXPECT_TRUE(result->items.empty());

  auto fetched = service->get_workload(result->workload.id().value());
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->status(), domain::WorkloadStatus::Succeeded);
}

TEST_F(WorkloadServiceTest, CreateWorkloadRejectsEmptyType) {
  CreateWorkloadRequest request{.type = "", .items = {}};
  auto result = service->create_workload(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(WorkloadServiceTest, CreateWorkloadRejectsTooManyItems) {
  CreateWorkloadRequest request{.type = "user.process", .items = {}};
  request.items.resize(1001, WorkloadItem{.payload = "{}"});
  auto result = service->create_workload(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(WorkloadServiceTest, CreateWorkloadRejectsEmptyItemPayload) {
  CreateWorkloadRequest request{.type = "user.process", .items = {{""}}};
  auto result = service->create_workload(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(WorkloadServiceTest, ItemThatFailsJobValidationIsReportedNotFatal) {
  // An empty payload would be rejected up front (see the test above); this
  // exercises a job that JobService itself rejects for a reason
  // WorkloadService doesn't pre-check -- an oversized job_type is not
  // realistic via `type`, so instead this asserts the *shape* of a
  // reported-but-non-fatal failure using a scheduler that will reject an
  // unregistered job_type, proving one item's failure doesn't abort the
  // loop or fail the whole call.
  CreateWorkloadRequest request{.type = "no.such.handler", .items = {{"{}"}, {"{}"}}};
  auto result = service->create_workload(request);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->workload.total_items(), 2u);
  ASSERT_EQ(result->items.size(), 2u);
  for (const auto& outcome : result->items) {
    EXPECT_FALSE(outcome.scheduled);
    ASSERT_TRUE(outcome.reason.has_value());
    EXPECT_FALSE(outcome.job_id.empty());  // The Job itself was still created.
  }
}

TEST_F(WorkloadServiceTest, GetWorkloadComputesProgressFromChildJobStatuses) {
  domain::Workload workload(infra::WorkloadId::generate(), "user.process", 3,
                            std::chrono::system_clock::now());
  ASSERT_TRUE(workload_repository->insert(workload).has_value());

  domain::Job succeeded(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                        std::chrono::system_clock::now(), 0, "user.process", workload.id());
  succeeded.transition_to(domain::JobStatus::Succeeded, std::chrono::system_clock::now());
  domain::Job dead_lettered(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                            std::chrono::system_clock::now(), 0, "user.process", workload.id());
  dead_lettered.transition_to(domain::JobStatus::DeadLetter, std::chrono::system_clock::now());
  domain::Job still_running(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                            std::chrono::system_clock::now(), 0, "user.process", workload.id());
  still_running.transition_to(domain::JobStatus::Running, std::chrono::system_clock::now());

  ASSERT_TRUE(job_repository->insert(succeeded).has_value());
  ASSERT_TRUE(job_repository->insert(dead_lettered).has_value());
  ASSERT_TRUE(job_repository->insert(still_running).has_value());

  auto fetched = service->get_workload(workload.id().value());
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->queued_items(), 0u);
  EXPECT_EQ(fetched->running_items(), 1u);
  EXPECT_EQ(fetched->completed_items(), 1u);
  EXPECT_EQ(fetched->failed_items(), 1u);
  EXPECT_EQ(fetched->status(), domain::WorkloadStatus::Running);  // still_running is not yet terminal.

  still_running.transition_to(domain::JobStatus::Succeeded, std::chrono::system_clock::now());
  ASSERT_TRUE(job_repository->update(still_running).has_value());

  auto fetched_again = service->get_workload(workload.id().value());
  ASSERT_TRUE(fetched_again.has_value());
  EXPECT_EQ(fetched_again->running_items(), 0u);
  EXPECT_EQ(fetched_again->completed_items(), 2u);
  EXPECT_EQ(fetched_again->failed_items(), 1u);
  EXPECT_EQ(fetched_again->status(), domain::WorkloadStatus::Failed);
}

TEST_F(WorkloadServiceTest, GetWorkloadReturnsNotFoundForUnknownId) {
  auto result = service->get_workload(infra::WorkloadId::generate().value());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST_F(WorkloadServiceTest, ListWorkloadsReturnsEnrichedProgress) {
  for (int i = 0; i < 2; ++i) {
    CreateWorkloadRequest request{.type = "user.process",
                                  .items = {{R"({"name":"A","email":"a@example.com"})"}}};
    ASSERT_TRUE(service->create_workload(request).has_value());
  }

  auto listed = service->list_workloads(10, 0);
  ASSERT_TRUE(listed.has_value());
  ASSERT_EQ(listed->size(), 2u);
  for (const auto& workload : *listed) {
    EXPECT_EQ(workload.total_items(), 1u);
    EXPECT_EQ(workload.status(), domain::WorkloadStatus::Running);
  }
}

TEST_F(WorkloadServiceTest, ListItemsReturnsBoundedPageAndTotal) {
  // list_items() is generic (works for any workload type) -- its test
  // setup deliberately does not go through CSV import (see
  // engine/tests/services/user_import_test.cpp for that path's own
  // coverage) so this test cannot be mistaken for depending on it.
  CreateWorkloadRequest request{.type = "user.process",
                                .items = {{R"({"name":"Alice","email":"alice@example.com"})"},
                                          {R"({"name":"Bob","email":"bob@example.com"})"},
                                          {R"({"name":"Carol","email":"carol@example.com"})"}}};
  auto created = service->create_workload(request);
  ASSERT_TRUE(created.has_value()) << created.error().message();
  const std::string workload_id = created->workload.id().value();

  auto page1 = service->list_items(workload_id, 2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  EXPECT_EQ(page1->total, 3u);
  EXPECT_EQ(page1->jobs.size(), 2u);

  auto page2 = service->list_items(workload_id, 2, 2);
  ASSERT_TRUE(page2.has_value());
  EXPECT_EQ(page2->total, 3u);
  EXPECT_EQ(page2->jobs.size(), 1u);
}

TEST_F(WorkloadServiceTest, ListItemsReturnsNotFoundForUnknownWorkload) {
  auto result = service->list_items(infra::WorkloadId::generate().value(), 10, 0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

}  // namespace
}  // namespace flowforge::services
