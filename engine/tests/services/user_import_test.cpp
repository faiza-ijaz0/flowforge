#include "flowforge/services/user_import.hpp"

#include <gtest/gtest.h>

#include <tuple>

#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::services {
namespace {

/// Exercises `import_users_from_csv` -- the per-business-domain adapter
/// that composes `parse_user_import_csv` with the generic
/// `WorkloadService::create_workload` (see user_import.hpp's class
/// comment). Mirrors WorkloadServiceTest's fixture (workload_service_test
/// .cpp) since this still drives a real `WorkloadService` end to end; the
/// only difference is exercising the free function rather than a
/// `WorkloadService` method, since `WorkloadService` itself no longer
/// knows CSV/user import exists.
class UserImportTest : public ::testing::Test {
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
    scheduler = std::make_shared<engine::PriorityScheduler>(handler_registry, engine::SchedulerConfig{},
                                                            logger, metrics);
    ASSERT_TRUE(scheduler->start().has_value());

    workload_service = std::make_unique<WorkloadService>(workload_repository, job_repository, job_service,
                                                         scheduler, clock, logger, metrics);
  }

  std::shared_ptr<persistence::InMemoryWorkloadRepository> workload_repository;
  std::shared_ptr<persistence::InMemoryJobRepository> job_repository;
  std::shared_ptr<infra::ManualClock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::shared_ptr<infra::MetricsRegistry> metrics;
  std::shared_ptr<JobService> job_service;
  std::shared_ptr<engine::HandlerRegistry> handler_registry;
  std::shared_ptr<engine::PriorityScheduler> scheduler;
  std::unique_ptr<WorkloadService> workload_service;
};

TEST_F(UserImportTest, CreatesJobsForValidRowsOnly) {
  const std::string csv =
      "name,email\n"
      "Alice Khan,ALICE@example.com\n"
      ",blank-name@example.com\n"
      "Bob,bob@example.com\n";
  auto result = import_users_from_csv(*workload_service, csv, logger, metrics);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 3u);
  EXPECT_EQ(result->valid_rows, 2u);
  EXPECT_EQ(result->invalid_rows, 1u);
  ASSERT_EQ(result->rejected_rows.size(), 1u);
  EXPECT_EQ(result->rejected_rows[0].row_number, 2u);
  EXPECT_EQ(result->workload.total_items(), 2u);
  EXPECT_EQ(result->items.size(), 2u);

  auto jobs = job_repository->list_by_workload_id(result->workload.id(), 10, 0);
  ASSERT_TRUE(jobs.has_value());
  ASSERT_EQ(jobs->size(), 2u);
  for (const auto& job : *jobs) {
    EXPECT_EQ(job.job_type(), "user.process");
    // The normalized (trimmed/lowercased) record is what gets serialized
    // as the Job's payload -- see domain::serialize_user_record_as_job_payload.
    EXPECT_NE(job.payload().find("\"name\""), std::string::npos);
    EXPECT_NE(job.payload().find("\"email\""), std::string::npos);
  }
}

TEST_F(UserImportTest, RejectsStructurallyInvalidCsvWithoutCreatingAWorkload) {
  auto result = import_users_from_csv(*workload_service, "", logger, metrics);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);

  auto listed = workload_service->list_workloads(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_TRUE(listed->empty());
}

TEST_F(UserImportTest, WithAllInvalidRowsStillCreatesWorkload) {
  const std::string csv = "name,email\n,not-an-email\n";
  auto result = import_users_from_csv(*workload_service, csv, logger, metrics);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 1u);
  EXPECT_EQ(result->valid_rows, 0u);
  EXPECT_EQ(result->invalid_rows, 1u);
  EXPECT_EQ(result->workload.total_items(), 0u);
  EXPECT_EQ(result->workload.status(), domain::WorkloadStatus::Succeeded);
}

TEST_F(UserImportTest, MetricsFreeCallWorksWithoutAMetricsRegistry) {
  // `metrics` is optional on `import_users_from_csv` (mirrors every other
  // service in this codebase, e.g. JobService) -- proves a caller that
  // doesn't care about metrics doesn't have to construct one.
  const std::string csv = "name,email\nAlice,alice@example.com\n";
  auto result = import_users_from_csv(*workload_service, csv, logger, nullptr);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->valid_rows, 1u);
}

}  // namespace
}  // namespace flowforge::services
