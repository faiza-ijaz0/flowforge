#include "flowforge/services/input_processing_service.hpp"

#include <gtest/gtest.h>

#include <tuple>

#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::services {
namespace {

class InputProcessingServiceTest : public ::testing::Test {
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

    workload_service = std::make_shared<WorkloadService>(workload_repository, job_repository, job_service,
                                                         scheduler, clock, logger, metrics);
    service = std::make_unique<InputProcessingService>(workload_service, logger, metrics);
  }

  std::shared_ptr<persistence::InMemoryWorkloadRepository> workload_repository;
  std::shared_ptr<persistence::InMemoryJobRepository> job_repository;
  std::shared_ptr<infra::ManualClock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::shared_ptr<infra::MetricsRegistry> metrics;
  std::shared_ptr<JobService> job_service;
  std::shared_ptr<engine::HandlerRegistry> handler_registry;
  std::shared_ptr<engine::PriorityScheduler> scheduler;
  std::shared_ptr<WorkloadService> workload_service;
  std::unique_ptr<InputProcessingService> service;
};

TEST_F(InputProcessingServiceTest, CsvPlusUsersCreatesAWorkloadThroughTheRealPipeline) {
  ProcessRequest request{.source_type = domain::InputSourceType::Csv,
                         .target = domain::ProcessingTarget::Users,
                         .payload = "name,email\nAlice,alice@example.com\nBob,bob@example.com\n"};
  auto result = service->process(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  EXPECT_EQ(result->valid_records, 2u);
  EXPECT_EQ(result->invalid_records, 0u);
  EXPECT_EQ(result->workload.total_items(), 2u);
  EXPECT_EQ(result->workload.type(), "user.process");

  auto jobs = job_repository->list_by_workload_id(result->workload.id(), 10, 0);
  ASSERT_TRUE(jobs.has_value());
  EXPECT_EQ(jobs->size(), 2u);
}

TEST_F(InputProcessingServiceTest, PartiallyInvalidCsvReportsRejectedRecordsAsGenericRecords) {
  ProcessRequest request{.source_type = domain::InputSourceType::Csv,
                         .target = domain::ProcessingTarget::Users,
                         .payload = "name,email\n,blank-name@example.com\nBob,bob@example.com\n"};
  auto result = service->process(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  EXPECT_EQ(result->valid_records, 1u);
  EXPECT_EQ(result->invalid_records, 1u);
  ASSERT_EQ(result->rejected_records.size(), 1u);
  EXPECT_EQ(result->rejected_records[0].index, 1u);
}

TEST_F(InputProcessingServiceTest, ImageSourceIsRejectedAsUnsupportedWithoutCreatingAWorkload) {
  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = "raw bytes"};
  auto result = service->process(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
  EXPECT_NE(result.error().message().find("not yet supported"), std::string::npos);

  auto listed = workload_service->list_workloads(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_TRUE(listed->empty());
}

TEST_F(InputProcessingServiceTest, ScreenshotTextAndUrlSourcesAreAllRejectedAsUnsupported) {
  for (auto source :
       {domain::InputSourceType::Screenshot, domain::InputSourceType::Text, domain::InputSourceType::Url}) {
    ProcessRequest request{.source_type = source, .target = domain::ProcessingTarget::Users, .payload = "x"};
    auto result = service->process(request);
    ASSERT_FALSE(result.has_value()) << "source " << domain::to_string(source) << " unexpectedly succeeded";
    EXPECT_EQ(result.error().code(), ErrorCode::Validation);
  }
}

TEST_F(InputProcessingServiceTest, ProductsAndCategoriesTargetsAreRejectedAsUnsupportedEvenForCsv) {
  for (auto target : {domain::ProcessingTarget::Products, domain::ProcessingTarget::Categories}) {
    ProcessRequest request{
        .source_type = domain::InputSourceType::Csv, .target = target, .payload = "name\nfoo\n"};
    auto result = service->process(request);
    ASSERT_FALSE(result.has_value()) << "target " << domain::to_string(target) << " unexpectedly succeeded";
    EXPECT_EQ(result.error().code(), ErrorCode::Validation);
  }
}

TEST_F(InputProcessingServiceTest, EmptyPayloadForSupportedCombinationIsStillRejected) {
  ProcessRequest request{
      .source_type = domain::InputSourceType::Csv, .target = domain::ProcessingTarget::Users, .payload = ""};
  auto result = service->process(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

}  // namespace
}  // namespace flowforge::services
