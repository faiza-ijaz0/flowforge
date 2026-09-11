#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>

#include "flowforge/infra/config.hpp"
#include "flowforge/providers/tesseract_ocr_provider.hpp"
#include "flowforge/test_support/fixtures_path.hpp"
#include "http/app.hpp"

namespace flowforge::server {
namespace {

[[nodiscard]] std::string read_fixture_file(const std::string& filename) {
  std::ifstream in(std::string(flowforge::test::kFixturesDir) + "/" + filename, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::optional<std::string> postgres_test_database_url() {
  if (const char* url = std::getenv("FLOWFORGE_TEST_DATABASE_URL")) {
    return std::string(url);
  }
  if (const char* url = std::getenv("FLOWFORGE_DATABASE_URL")) {
    return std::string(url);
  }
  return std::nullopt;
}

/// Exercises POST /api/v1/process end-to-end against a real App -- mirrors
/// WorkloadRoutesTest/UserImportRoutesTest's fixture.
class ProcessRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    infra::AppConfig config;
    config.log_level = infra::LogLevel::Off;
    auto app_result = App::create(config);
    ASSERT_TRUE(app_result.has_value()) << app_result.error().message();
    app_ = std::move(*app_result);

    port_ = app_->http_server().bind_to_any_port("127.0.0.1");
    ASSERT_GT(port_, 0);
    server_thread_ = std::thread([this] { app_->http_server().listen_after_bind(); });

    for (int i = 0; i < 100 && !app_->http_server().is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(app_->http_server().is_running());
  }

  void TearDown() override {
    app_->http_server().stop();
    server_thread_.join();
  }

  [[nodiscard]] httplib::Client make_client() const {
    httplib::Client client("127.0.0.1", port_);
    client.set_connection_timeout(2);
    client.set_read_timeout(5);
    return client;
  }

  static httplib::MultipartFormDataItems process_request(const std::string& source, const std::string& target,
                                                         std::string file_content) {
    return {
        {.name = "source", .content = source, .filename = "", .content_type = ""},
        {.name = "target", .content = target, .filename = "", .content_type = ""},
        {.name = "file",
         .content = std::move(file_content),
         .filename = "input.csv",
         .content_type = "text/csv"},
    };
  }

  static httplib::MultipartFormDataItems image_request(const std::string& source, const std::string& target,
                                                       std::string file_content) {
    return {
        {.name = "source", .content = source, .filename = "", .content_type = ""},
        {.name = "target", .content = target, .filename = "", .content_type = ""},
        {.name = "file",
         .content = std::move(file_content),
         .filename = "input.png",
         .content_type = "image/png"},
    };
  }

  [[nodiscard]] static std::string read_fixture(const std::string& filename) {
    std::ifstream in(std::string(flowforge::test::kFixturesDir) + "/" + filename, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }

  std::unique_ptr<App> app_;
  std::thread server_thread_;
  int port_ = 0;
};

TEST_F(ProcessRoutesTest, CsvPlusUsersIsFeatureCompleteAndCreatesARealWorkload) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process",
                         process_request("csv", "users", "name,email\nAlice,alice@example.com\n"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);

  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["type"], "user.process");
  EXPECT_EQ(parsed["total_records"], 1);
  EXPECT_EQ(parsed["valid_records"], 1);
  EXPECT_EQ(parsed["invalid_records"], 0);
  EXPECT_EQ(parsed["total_items"], 1);
  ASSERT_TRUE(parsed.contains("items"));
  EXPECT_EQ(parsed["items"].size(), 1u);
}

TEST_F(ProcessRoutesTest, ImageSourceReturnsClearUnsupportedErrorNotFakeSuccess) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process", process_request("image", "users", "not-real-image-bytes"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["error"]["code"], "validation_error");
  EXPECT_NE(parsed["error"]["message"].get<std::string>().find("not yet supported"), std::string::npos);
}

TEST_F(ProcessRoutesTest, ScreenshotTextAndUrlAllReturnUnsupportedNotSuccess) {
  auto client = make_client();
  for (const char* source : {"screenshot", "text", "url"}) {
    auto res = client.Post("/api/v1/process", process_request(source, "users", "irrelevant"));
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400) << "source=" << source;
  }
}

TEST_F(ProcessRoutesTest, ProductsAndCategoriesTargetsReturnUnsupportedEvenForCsv) {
  auto client = make_client();
  for (const char* target : {"products", "categories"}) {
    auto res = client.Post("/api/v1/process", process_request("csv", target, "name\nfoo\n"));
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400) << "target=" << target;
  }
}

TEST_F(ProcessRoutesTest, UnrecognizedSourceStringIsRejectedAtTheShapeLayer) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process", process_request("pdf", "users", "irrelevant"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_NE(parsed["error"]["message"].get<std::string>().find("unrecognized"), std::string::npos);
}

TEST_F(ProcessRoutesTest, UnrecognizedTargetStringIsRejectedAtTheShapeLayer) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process", process_request("csv", "orders", "name\nfoo\n"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, MissingFileFieldIsRejected) {
  auto client = make_client();
  httplib::MultipartFormDataItems items{
      {.name = "source", .content = "csv", .filename = "", .content_type = ""},
      {.name = "target", .content = "users", .filename = "", .content_type = ""},
  };
  auto res = client.Post("/api/v1/process", items);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, NonMultipartRequestIsRejected) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process", "not multipart", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

// --- Phase 3D-1: preview/confirm --------------------------------------

TEST_F(ProcessRoutesTest, PreviewRejectsCsvSourceEvenThoughProcessSupportsIt) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process/preview",
                         process_request("csv", "users", "name,email\nAlice,alice@example.com\n"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, PreviewRejectsNonUsersTargets) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process/preview", image_request("image", "products", "irrelevant"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, PreviewRejectsMalformedImageBytes) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process/preview", image_request("image", "users", "not a real image"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, PreviewFailurePathCreatesNoWorkload) {
  // A rejected preview call must never create a workload -- covers this
  // without depending on Tesseract being installed (see the gated
  // real-OCR test below for the success-path equivalent).
  auto client = make_client();
  auto before = client.Get("/api/v1/workloads?limit=100");
  ASSERT_TRUE(before);
  const auto before_count = nlohmann::json::parse(before->body)["workloads"].size();

  client.Post("/api/v1/process/preview", image_request("image", "users", "not a real image"));

  auto after = client.Get("/api/v1/workloads?limit=100");
  ASSERT_TRUE(after);
  EXPECT_EQ(nlohmann::json::parse(after->body)["workloads"].size(), before_count);
}

TEST_F(ProcessRoutesTest, PreviewOfARealFixtureImageExtractsRecordsAndCreatesNoWorkload) {
  if (!providers::TesseractCliOcrProvider::discover_executable()) {
    GTEST_SKIP() << "No usable Tesseract OCR binary found -- skipping real-OCR HTTP test. See "
                    "docs/architecture/input-processing.md, \"Image extraction\".";
  }
  auto client = make_client();
  const std::string image_bytes = read_fixture("user_table.png");
  ASSERT_FALSE(image_bytes.empty());

  auto res = client.Post("/api/v1/process/preview", image_request("image", "users", image_bytes));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_records"], 3);
  ASSERT_TRUE(parsed.contains("records"));
  EXPECT_EQ(parsed["records"].size(), 3u);
  EXPECT_EQ(parsed["records"][0]["email"], "ali@example.com");

  auto workloads = client.Get("/api/v1/workloads?limit=100");
  ASSERT_TRUE(workloads);
  EXPECT_TRUE(nlohmann::json::parse(workloads->body)["workloads"].empty());
}

TEST_F(ProcessRoutesTest, ConfirmCreatesARealWorkloadFromSubmittedRecords) {
  auto client = make_client();
  nlohmann::json body{
      {"target", "users"},
      {"records",
       nlohmann::json::array({{{"name", "Ali"}, {"email", "ali@example.com"}},
                              {{"name", "Sara"}, {"email", "sara@example.com"}, {"phone", "0311"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["type"], "user.process");
  EXPECT_EQ(parsed["total_records"], 2);
  EXPECT_EQ(parsed["valid_records"], 2);
  EXPECT_EQ(parsed["total_items"], 2);

  auto workloads = client.Get("/api/v1/workloads?limit=100");
  ASSERT_TRUE(workloads);
  EXPECT_EQ(nlohmann::json::parse(workloads->body)["workloads"].size(), 1u);
}

TEST_F(ProcessRoutesTest, ConfirmReportsInvalidRecordsRatherThanSilentlyDroppingThem) {
  auto client = make_client();
  nlohmann::json body{{"target", "users"},
                      {"records", nlohmann::json::array({{{"name", "Ali"}, {"email", "ali@example.com"}},
                                                         {{"name", "Bad"}, {"email", "not-an-email"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["valid_records"], 1);
  EXPECT_EQ(parsed["invalid_records"], 1);
  ASSERT_EQ(parsed["rejected_records"].size(), 1u);
}

TEST_F(ProcessRoutesTest, ConfirmRejectsEmptyRecordsArray) {
  auto client = make_client();
  nlohmann::json body{{"target", "users"}, {"records", nlohmann::json::array()}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, ConfirmRejectsMalformedJsonBody) {
  auto client = make_client();
  auto res = client.Post("/api/v1/process/confirm", "not json", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, ConfirmRejectsNonUsersTarget) {
  auto client = make_client();
  nlohmann::json body{{"target", "products"},
                      {"records", nlohmann::json::array({{{"name", "Ali"}, {"email", "ali@example.com"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(ProcessRoutesTest, InvalidRowsAreReportedNotSilentlySucceeded) {
  auto client = make_client();
  auto res = client.Post(
      "/api/v1/process",
      process_request("csv", "users", "name,email\n,blank-name@example.com\nBob,bob@example.com\n"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_records"], 2);
  EXPECT_EQ(parsed["valid_records"], 1);
  EXPECT_EQ(parsed["invalid_records"], 1);
  ASSERT_TRUE(parsed.contains("rejected_records"));
  EXPECT_EQ(parsed["rejected_records"].size(), 1u);
}

// --- Phase 3D-2: 100+ record bulk image acceptance ---------------------

/// Real OCR against `user_table_bulk_100.png` (100 data rows -- see
/// docs/architecture/input-processing.md, "100+ record fixture"). Proves
/// no row is silently lost at scale: `total_records` must be exactly 100,
/// and no workload is created by preview regardless of how many records
/// it extracts. In-memory persistence is sufficient here (preview never
/// touches the database either way) -- the separate,
/// Postgres-and-Tesseract-gated test below covers confirm + real
/// persistence.
TEST_F(ProcessRoutesTest, PreviewOfTheHundredRowFixtureExtractsEveryRowAndCreatesNoWorkload) {
  if (!providers::TesseractCliOcrProvider::discover_executable()) {
    GTEST_SKIP() << "No usable Tesseract OCR binary found -- skipping real-OCR HTTP test.";
  }
  // make_client()'s 5s read timeout is generous for the CSV/JSON-only
  // tests it was designed for, but real OCR across 100 rows genuinely
  // takes several seconds -- a bespoke, longer timeout here (mirrors
  // ProcessRoutesBulkPostgresTest's client below) avoids a spurious
  // client-side timeout under load, not a server-side problem.
  httplib::Client client("127.0.0.1", port_);
  client.set_connection_timeout(2);
  client.set_read_timeout(30);
  const std::string image_bytes = read_fixture("user_table_bulk_100.png");
  ASSERT_FALSE(image_bytes.empty());

  auto before = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(before);
  const auto before_count = nlohmann::json::parse(before->body)["workloads"].size();

  auto res = client.Post("/api/v1/process/preview", image_request("image", "users", image_bytes));
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_records"], 100);
  EXPECT_EQ(parsed["valid_records"].get<std::size_t>() + parsed["invalid_records"].get<std::size_t>(), 100u);
  EXPECT_EQ(parsed["records"].size(), parsed["valid_records"].get<std::size_t>());
  EXPECT_EQ(parsed["rejected_records"].size(), parsed["invalid_records"].get<std::size_t>());
  // At least the large majority of 100 real, OCR'd rows must come through
  // as usable records -- a near-total failure would indicate a real
  // extraction regression, not just ordinary OCR noise.
  EXPECT_GE(parsed["valid_records"].get<std::size_t>(), 80u);

  auto after = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(after);
  EXPECT_EQ(nlohmann::json::parse(after->body)["workloads"].size(), before_count);
}

/// A confirm request mixing one deliberately invalid record among valid
/// ones must never let the invalid one become a job -- exercised directly
/// (no OCR/image involved) since this is confirm()'s own re-validation
/// responsibility, not extraction's.
TEST_F(ProcessRoutesTest, ConfirmNeverSubmitsAnInvalidRecordAsAJob) {
  auto client = make_client();
  nlohmann::json body{
      {"target", "users"},
      {"records", nlohmann::json::array({{{"name", "Good"}, {"email", "good@example.com"}},
                                         {{"name", "Bad"}, {"email", "not-an-email"}},
                                         {{"name", "AlsoGood"}, {"email", "alsogood@example.com"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_records"], 3);
  EXPECT_EQ(parsed["valid_records"], 2);
  EXPECT_EQ(parsed["invalid_records"], 1);
  EXPECT_EQ(parsed["total_items"], 2);
  ASSERT_EQ(parsed["items"].size(), 2u);

  const std::string workload_id = parsed["id"].get<std::string>();
  auto items_res = client.Get("/api/v1/workloads/" + workload_id + "/items?limit=10");
  ASSERT_TRUE(items_res);
  auto items_parsed = nlohmann::json::parse(items_res->body);
  EXPECT_EQ(items_parsed["total"], 2u);
  for (const auto& item : items_parsed["items"]) {
    EXPECT_NE(item["email"], "not-an-email");
  }
}

/// The full acceptance chain for 100+ records against a real PostgreSQL
/// database: real OCR extraction -> preview (zero DB writes, verified) ->
/// confirm (exactly one Workload row, one Job row per accepted record) ->
/// real PriorityScheduler/LocalWorkerPool/JobExecutor/UserProcessHandler
/// execution -> polled to a genuine terminal state -> every count
/// reconciled against what the database actually persisted. Skips
/// (reported honestly, never silently passed) if either a real Tesseract
/// binary or a real test database isn't available -- see
/// docs/architecture/input-processing.md, "100+ record acceptance".
TEST(ProcessRoutesBulkPostgresTest, HundredRecordImageFlowReconcilesAgainstRealPostgres) {
  if (!providers::TesseractCliOcrProvider::discover_executable()) {
    GTEST_SKIP() << "No usable Tesseract OCR binary found -- skipping real-OCR bulk acceptance test.";
  }
  auto db_url = postgres_test_database_url();
  if (!db_url) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk acceptance test.";
  }

  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *db_url;
  auto app_result = App::create(config);
  ASSERT_TRUE(app_result.has_value()) << app_result.error().message();
  auto app = std::move(*app_result);
  int port = app->http_server().bind_to_any_port("127.0.0.1");
  ASSERT_GT(port, 0);
  std::thread server_thread([&app] { app->http_server().listen_after_bind(); });
  for (int i = 0; i < 100 && !app->http_server().is_running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(app->http_server().is_running());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2);
  client.set_read_timeout(30);

  const std::string image_bytes = read_fixture_file("user_table_bulk_100.png");
  ASSERT_FALSE(image_bytes.empty());

  auto before = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(before);
  const std::size_t workloads_before = nlohmann::json::parse(before->body)["workloads"].size();

  // --- Preview: zero database writes, whatever the extraction yields ---
  httplib::MultipartFormDataItems preview_items{
      {.name = "source", .content = "image", .filename = "", .content_type = ""},
      {.name = "target", .content = "users", .filename = "", .content_type = ""},
      {.name = "file", .content = image_bytes, .filename = "bulk.png", .content_type = "image/png"},
  };
  auto preview_res = client.Post("/api/v1/process/preview", preview_items);
  ASSERT_TRUE(preview_res);
  ASSERT_EQ(preview_res->status, 200);
  auto preview_parsed = nlohmann::json::parse(preview_res->body);
  const std::size_t total_records = preview_parsed["total_records"].get<std::size_t>();
  const std::size_t valid_records = preview_parsed["valid_records"].get<std::size_t>();
  ASSERT_EQ(total_records, 100u);
  ASSERT_GE(valid_records, 80u) << "unexpectedly low OCR yield for the committed fixture";

  auto after_preview = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_preview);
  EXPECT_EQ(nlohmann::json::parse(after_preview->body)["workloads"].size(), workloads_before)
      << "preview must never create a workload";

  // --- Confirm: exactly one workload, one job per valid record ---------
  nlohmann::json confirm_body{{"target", "users"}, {"records", preview_parsed["records"]}};
  auto confirm_res = client.Post("/api/v1/process/confirm", confirm_body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();
  EXPECT_EQ(confirm_parsed["type"], "user.process");
  EXPECT_EQ(confirm_parsed["total_records"], valid_records);
  EXPECT_EQ(confirm_parsed["valid_records"], valid_records);
  EXPECT_EQ(confirm_parsed["invalid_records"], 0u);
  EXPECT_EQ(confirm_parsed["total_items"], valid_records);
  ASSERT_EQ(confirm_parsed["items"].size(), valid_records);
  for (const auto& item : confirm_parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
  }

  auto after_confirm = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_confirm);
  EXPECT_EQ(nlohmann::json::parse(after_confirm->body)["workloads"].size(), workloads_before + 1)
      << "confirm must create exactly one workload";

  // --- Poll to a genuine terminal state (no shortcut) -------------------
  nlohmann::json final_workload;
  std::string final_status;
  for (int i = 0; i < 300; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    ASSERT_TRUE(get_res);
    final_workload = nlohmann::json::parse(get_res->body);
    final_status = final_workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT_EQ(final_status, "succeeded");
  EXPECT_EQ(final_workload["total_items"], valid_records);
  EXPECT_EQ(final_workload["completed_items"], valid_records);
  EXPECT_EQ(final_workload["failed_items"], 0u);
  EXPECT_EQ(final_workload["queued_items"], 0u);
  EXPECT_EQ(final_workload["running_items"], 0u);

  // --- Every job is genuinely associated with this workload, and the ---
  // --- count reconciles exactly with what was submitted -----------------
  auto items_res =
      client.Get("/api/v1/workloads/" + workload_id + "/items?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(items_res);
  auto items_parsed = nlohmann::json::parse(items_res->body);
  EXPECT_EQ(items_parsed["total"], valid_records);
  EXPECT_EQ(items_parsed["items"].size(), valid_records);
  std::unordered_set<std::string> job_ids;
  for (const auto& item : items_parsed["items"]) {
    EXPECT_EQ(item["status"], "succeeded");
    EXPECT_EQ(item["attempt_count"], 1);
    job_ids.insert(item["job_id"].get<std::string>());
  }
  EXPECT_EQ(job_ids.size(), valid_records) << "no duplicate jobs may exist for this workload";

  app->http_server().stop();
  server_thread.join();
}

}  // namespace
}  // namespace flowforge::server
