#include "flowforge/services/input_processing_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/extractors/image_extractor.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::services {
namespace {

std::string png_signature_bytes() {
  return std::string("\x89PNG\r\n\x1a\n") + "fake-pixel-data";
}

domain::StructuredRecord user_record(std::string name, std::string email,
                                     std::optional<std::string> phone = std::nullopt) {
  domain::StructuredRecord record;
  record.fields.emplace("name", std::move(name));
  record.fields.emplace("email", std::move(email));
  if (phone) {
    record.fields.emplace("phone", std::move(*phone));
  }
  return record;
}

engine::OcrWord word(std::string text, int left, int top, int width, int height, double conf, int block,
                     int par, int line) {
  return engine::OcrWord{.text = std::move(text),
                         .left = left,
                         .top = top,
                         .width = width,
                         .height = height,
                         .confidence = conf,
                         .block_num = block,
                         .par_num = par,
                         .line_num = line};
}

/// Same deterministic `engine::IOcrProvider` double as
/// engine/tests/extractors/image_extractor_test.cpp -- kept local rather
/// than shared, since each test file's fixture data is independent and
/// small enough that a shared test-support header would be more
/// indirection than it saves.
class FakeOcrProvider final : public engine::IOcrProvider {
 public:
  explicit FakeOcrProvider(std::vector<engine::OcrWord> words) : words_(std::move(words)) {}

  [[nodiscard]] Result<engine::OcrResult> recognize(std::string_view) const override {
    return engine::OcrResult{.words = words_};
  }

 private:
  std::vector<engine::OcrWord> words_;
};

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

  /// Rebuilds `service` with a real `extractors::ImageExtractor` backed
  /// by a `FakeOcrProvider` returning `words` -- used only by the
  /// preview()-specific tests below; every other test in this file
  /// intentionally leaves the image extractor unset (nullptr), matching
  /// how `App` behaves when no Tesseract binary is available.
  void enable_image_extractor(std::vector<engine::OcrWord> words) {
    auto image_extractor =
        std::make_shared<extractors::ImageExtractor>(std::make_shared<FakeOcrProvider>(std::move(words)));
    service = std::make_unique<InputProcessingService>(workload_service, logger, metrics, image_extractor);
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

TEST_F(InputProcessingServiceTest, DirectProcessNeverAcceptsImageEvenWithAnExtractorWired) {
  // The confirmation-required rule (docs/architecture/input-processing.md,
  // "Why process() never accepts Image/Screenshot") must hold even once a
  // real image extractor exists -- process() must still never persist an
  // image's records without a separate confirm() call.
  enable_image_extractor(
      {word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1), word("Email", 200, 20, 50, 20, 96.0, 1, 1, 1),
       word("Ali", 20, 70, 30, 20, 95.0, 1, 2, 1), word("ali@example.com", 200, 70, 200, 20, 95.0, 1, 2, 1)});
  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = "x"};
  auto result = service->process(request);
  ASSERT_FALSE(result.has_value());
  auto listed = workload_service->list_workloads(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_TRUE(listed->empty());
}

TEST_F(InputProcessingServiceTest, PreviewWithoutAnImageExtractorWiredIsRejected) {
  // Mirrors a deployment with no Tesseract binary available -- see
  // `App::create`'s startup-probe.
  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = png_signature_bytes()};
  auto result = service->preview(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(InputProcessingServiceTest, PreviewRejectsCsvSourceEvenThoughProcessSupportsIt) {
  enable_image_extractor({word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1)});
  ProcessRequest request{.source_type = domain::InputSourceType::Csv,
                         .target = domain::ProcessingTarget::Users,
                         .payload = "name,email\nAlice,alice@example.com\n"};
  auto result = service->preview(request);
  ASSERT_FALSE(result.has_value());
}

TEST_F(InputProcessingServiceTest, PreviewRejectsUnimplementedSources) {
  // As of Phase 3F, every ProcessingTarget (Users, Products, Categories)
  // supports preview() for at least one source -- there is no longer a
  // target-level "unimplemented" case (see ConfirmSupportsEveryDeclaredTarget
  // below for the confirm()-side equivalent observation). What remains
  // genuinely unimplemented is Text/Url as a *source*, for any target --
  // preview()'s extractor selection never wires either one up.
  enable_image_extractor({word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1)});
  for (auto source : {domain::InputSourceType::Text, domain::InputSourceType::Url}) {
    ProcessRequest request{
        .source_type = source, .target = domain::ProcessingTarget::Categories, .payload = "irrelevant"};
    auto result = service->preview(request);
    ASSERT_FALSE(result.has_value()) << "source " << domain::to_string(source) << " unexpectedly succeeded";
  }
}

TEST_F(InputProcessingServiceTest, PreviewExtractsAndMapsRecordsWithoutCreatingAWorkload) {
  enable_image_extractor(
      {word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1), word("Email", 200, 20, 50, 20, 96.0, 1, 1, 1),
       word("Ali", 20, 70, 30, 20, 95.0, 1, 2, 1), word("ali@example.com", 200, 70, 200, 20, 95.0, 1, 2, 1),
       word("Sara", 20, 120, 35, 20, 95.0, 1, 2, 2),
       word("sara@example.com", 200, 120, 210, 20, 95.0, 1, 2, 2)});

  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = png_signature_bytes()};
  auto result = service->preview(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  ASSERT_EQ(result->records.size(), 2u);
  EXPECT_EQ(result->records[0].field("name"), "Ali");
  EXPECT_EQ(result->records[0].field("email"), "ali@example.com");
  EXPECT_TRUE(result->rejected_records.empty());

  auto listed = workload_service->list_workloads(10, 0);
  ASSERT_TRUE(listed.has_value());
  EXPECT_TRUE(listed->empty());
}

TEST_F(InputProcessingServiceTest, PreviewReportsMappingRejectionAgainstTheOriginalRowPosition) {
  // Row 1 is structurally fine but fails Users-mapping (no email column);
  // row 2 is a real, valid Ali record. The reported rejection must point
  // at original row 1, not row 1-of-the-structurally-valid-subset (which
  // would coincidentally also be 1 here -- see the harder case below).
  enable_image_extractor(
      {word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1), word("Other", 200, 20, 50, 20, 96.0, 1, 1, 1),
       word("Nobody", 20, 70, 50, 20, 95.0, 1, 2, 1), word("nothing-here", 200, 70, 100, 20, 95.0, 1, 2, 1),
       word("Ali", 20, 120, 30, 20, 95.0, 1, 2, 2),
       word("ali@example.com", 200, 120, 200, 20, 95.0, 1, 2, 2)});
  // Rename the header's second column away from "email" by using a fake
  // OCR provider whose header cell literally reads "Other" -- above.

  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = png_signature_bytes()};
  auto result = service->preview(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  // Both rows are structurally fine (2 columns each) but neither has a
  // recognizable 'email' header -- every row is a mapping-level
  // rejection, at its real original position.
  EXPECT_TRUE(result->records.empty());
  ASSERT_EQ(result->rejected_records.size(), 2u);
  EXPECT_EQ(result->rejected_records[0].index, 1u);
  EXPECT_EQ(result->rejected_records[1].index, 2u);
}

TEST_F(InputProcessingServiceTest, PreviewRenumbersMappingRejectionsPastStructuralRejections) {
  // Row 1: structurally mismatched (only 1 column) -- extraction itself
  // rejects it as index 1. Row 2: structurally fine (2 columns) but no
  // email column -- must be reported as original index 2, not 1 (its
  // position within extraction's own, shorter "structurally valid"
  // list).
  enable_image_extractor(
      {word("Name", 20, 20, 40, 20, 96.0, 1, 1, 1), word("Other", 200, 20, 50, 20, 96.0, 1, 1, 1),
       word("SoloCell", 20, 70, 400, 20, 95.0, 1, 2, 1),  // one wide word -> 1 column -> structural reject
       word("Ali", 20, 120, 30, 20, 95.0, 1, 2, 2),
       word("no-email-header", 200, 120, 150, 20, 95.0, 1, 2, 2)});

  ProcessRequest request{.source_type = domain::InputSourceType::Image,
                         .target = domain::ProcessingTarget::Users,
                         .payload = png_signature_bytes()};
  auto result = service->preview(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->rejected_records.size(), 2u);
  // Extraction-level rejection (row 1) and mapping-level rejection (row
  // 2, correctly renumbered past row 1) both present, each at its real
  // original position.
  std::vector<std::size_t> indices = {result->rejected_records[0].index, result->rejected_records[1].index};
  std::sort(indices.begin(), indices.end());
  EXPECT_EQ(indices[0], 1u);
  EXPECT_EQ(indices[1], 2u);
}

TEST_F(InputProcessingServiceTest, ConfirmCreatesAWorkloadFromPreviouslyPreviewedRecords) {
  ConfirmRequest request{
      .target = domain::ProcessingTarget::Users,
      .records = {user_record("Ali", "ali@example.com"), user_record("Sara", "sara@example.com", "0311")}};
  auto result = service->confirm(request);
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

TEST_F(InputProcessingServiceTest, ConfirmRevalidatesRecordsRatherThanTrustingThemBlindly) {
  ConfirmRequest request{
      .target = domain::ProcessingTarget::Users,
      .records = {user_record("Ali", "ali@example.com"), user_record("Bad", "not-an-email")}};
  auto result = service->confirm(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  EXPECT_EQ(result->valid_records, 1u);
  EXPECT_EQ(result->invalid_records, 1u);
  ASSERT_EQ(result->rejected_records.size(), 1u);
  EXPECT_EQ(result->rejected_records[0].index, 2u);
  EXPECT_EQ(result->workload.total_items(), 1u);
}

TEST_F(InputProcessingServiceTest, ConfirmSupportsEveryDeclaredProcessingTarget) {
  // As of Phase 3F, confirm() supports all three declared
  // domain::ProcessingTarget values (Users, Products, Categories) -- there
  // is no longer a "this target is unimplemented" case to exercise at
  // this layer (contrast with Phase 3E, where this test asserted
  // Categories was rejected). A genuinely malformed/unrecognized target
  // string is instead rejected earlier, at the HTTP JSON-parsing boundary
  // -- see apps/server/tests/process_routes_test.cpp,
  // UnrecognizedTargetStringIsRejectedAtTheShapeLayer.
  ConfirmRequest request{.target = domain::ProcessingTarget::Categories, .records = {[] {
                                                                           domain::StructuredRecord record;
                                                                           record.fields.emplace(
                                                                               "name", "Electronics");
                                                                           return record;
                                                                         }()}};
  auto result = service->confirm(request);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->valid_records, 1u);
  EXPECT_EQ(result->workload.type(), "category.process");
}

TEST_F(InputProcessingServiceTest, ConfirmRejectsEmptyRecordList) {
  ConfirmRequest request{.target = domain::ProcessingTarget::Users, .records = {}};
  auto result = service->confirm(request);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

}  // namespace
}  // namespace flowforge::services
