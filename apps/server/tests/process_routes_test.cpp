#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
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

// Phase 3H: closes the persistence gap docs/architecture/phase-3g-audit.md
// §2.4 flagged -- UserProcessHandler now upserts into a real `users` table,
// mirroring ProductProcessHandler/CategoryProcessHandler. This proves it
// end-to-end: CSV -> direct process() -> real execution -> GET /api/v1/users
// reads back what the handler actually persisted, not what the job response
// claims.
TEST_F(ProcessRoutesTest, CsvUsersPersistsRealUsersAfterExecution) {
  auto client = make_client();
  auto res =
      client.Post("/api/v1/process",
                  process_request("csv", "users",
                                  "name,email,phone\nAlice,alice@example.com,555-1\nBob,bob@example.com,\n"));
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201);
  const std::string workload_id = nlohmann::json::parse(res->body)["id"].get<std::string>();

  std::string final_status;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    ASSERT_TRUE(get_res);
    auto workload = nlohmann::json::parse(get_res->body);
    final_status = workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "succeeded");

  // The job handler (handlers::UserProcessHandler), not this route,
  // persisted these -- GET /api/v1/users reads real repository state.
  auto users_res = client.Get("/api/v1/users?limit=50");
  ASSERT_TRUE(users_res);
  ASSERT_EQ(users_res->status, 200);
  auto users_parsed = nlohmann::json::parse(users_res->body);
  EXPECT_EQ(users_parsed["total"], 2u);
  ASSERT_EQ(users_parsed["users"].size(), 2u);
  bool found_alice = false;
  for (const auto& user : users_parsed["users"]) {
    if (user["email"] == "alice@example.com") {
      found_alice = true;
      EXPECT_EQ(user["name"], "Alice");
      EXPECT_EQ(user["phone"], "555-1");
      EXPECT_FALSE(user["job_id"].is_null());
    }
  }
  EXPECT_TRUE(found_alice);
}

TEST_F(ProcessRoutesTest, GetUsersReturnsEmptyListInitially) {
  auto client = make_client();
  auto res = client.Get("/api/v1/users");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total"], 0u);
  EXPECT_TRUE(parsed["users"].empty());
}

TEST_F(ProcessRoutesTest, GetUsersSupportsPagination) {
  auto client = make_client();
  std::string csv = "name,email\n";
  for (int i = 0; i < 5; ++i) {
    csv += "User " + std::to_string(i) + ",user" + std::to_string(i) + "@example.com\n";
  }
  auto process_res = client.Post("/api/v1/process", process_request("csv", "users", csv));
  ASSERT_TRUE(process_res);
  ASSERT_EQ(process_res->status, 201);
  const std::string workload_id = nlohmann::json::parse(process_res->body)["id"].get<std::string>();

  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    auto workload = nlohmann::json::parse(get_res->body);
    if (workload["status"] == "succeeded") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  auto page1 = client.Get("/api/v1/users?limit=2&offset=0");
  ASSERT_TRUE(page1);
  auto page1_parsed = nlohmann::json::parse(page1->body);
  EXPECT_EQ(page1_parsed["total"], 5u);
  EXPECT_EQ(page1_parsed["users"].size(), 2u);

  auto page2 = client.Get("/api/v1/users?limit=2&offset=4");
  ASSERT_TRUE(page2);
  EXPECT_EQ(nlohmann::json::parse(page2->body)["users"].size(), 1u);
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

TEST_F(ProcessRoutesTest, ConfirmRejectsAnUnrecognizedTargetString) {
  // As of Phase 3F, confirm() supports all three declared
  // domain::ProcessingTarget values (Users, Products, Categories) -- there
  // is no longer a declared-but-unimplemented target to exercise here
  // (contrast with Phase 3E, where this test asserted Categories was
  // rejected). What remains genuinely rejected is a target string that
  // doesn't parse to any ProcessingTarget at all -- rejected at the JSON
  // shape layer (process_json.cpp's parse_confirm_request), before
  // InputProcessingService is ever reached.
  auto client = make_client();
  nlohmann::json body{{"target", "orders"}, {"records", nlohmann::json::array({{{"name", "Ali"}}})}};
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

  // Phase 3H: UserProcessHandler now persists into a real `users` table
  // (closing the gap docs/architecture/phase-3g-audit.md §2.4 flagged) --
  // GET /api/v1/users reads real repository state, not the job response.
  auto users_res = client.Get("/api/v1/users?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(users_res);
  auto users_parsed = nlohmann::json::parse(users_res->body);
  EXPECT_GE(users_parsed["total"].get<std::size_t>(), valid_records);

  app->http_server().stop();
  server_thread.join();
}

/// The CSV-source counterpart of the image test above (Phase 3H): CSV+Users
/// is a direct-submit flow (POST /api/v1/process, no preview/confirm step
/// -- see ProcessingUploadPanel's class comment for why), so this exercises
/// that path specifically rather than reusing run_bulk_products_acceptance
/// (which is preview/confirm-shaped). users_bulk_100.csv deterministically
/// has exactly 5 invalid (malformed-email) rows out of 100, at the same
/// positions (17/34/51/68/85) as products_bulk_100.csv's deterministic
/// negative-price rows, for the same reconciliation-testing reason.
TEST(ProcessRoutesBulkPostgresTest, HundredUserCsvFlowReconcilesAgainstRealPostgres) {
  if (!postgres_test_database_url()) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk user CSV acceptance test.";
  }
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *postgres_test_database_url();
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

  const std::string csv = read_fixture_file("users_bulk_100.csv");
  ASSERT_FALSE(csv.empty());

  auto before = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(before);
  const std::size_t workloads_before = nlohmann::json::parse(before->body)["workloads"].size();

  httplib::MultipartFormDataItems items{
      {.name = "source", .content = "csv", .filename = "", .content_type = ""},
      {.name = "target", .content = "users", .filename = "", .content_type = ""},
      {.name = "file", .content = csv, .filename = "users.csv", .content_type = "text/csv"},
  };
  auto process_res = client.Post("/api/v1/process", items);
  ASSERT_TRUE(process_res);
  ASSERT_EQ(process_res->status, 201);
  auto process_parsed = nlohmann::json::parse(process_res->body);
  EXPECT_EQ(process_parsed["type"], "user.process");
  EXPECT_EQ(process_parsed["total_records"], 100u);
  EXPECT_EQ(process_parsed["valid_records"], 95u);
  EXPECT_EQ(process_parsed["invalid_records"], 5u);
  const std::size_t valid_records = process_parsed["valid_records"].get<std::size_t>();
  const std::string workload_id = process_parsed["id"].get<std::string>();
  ASSERT_EQ(process_parsed["items"].size(), valid_records);
  for (const auto& item : process_parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
  }

  auto after = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after);
  EXPECT_EQ(nlohmann::json::parse(after->body)["workloads"].size(), workloads_before + 1)
      << "confirm must create exactly one workload";

  std::string final_status;
  nlohmann::json final_workload;
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

  auto items_res =
      client.Get("/api/v1/workloads/" + workload_id + "/items?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(items_res);
  auto items_parsed = nlohmann::json::parse(items_res->body);
  EXPECT_EQ(items_parsed["total"], valid_records);
  std::unordered_set<std::string> job_ids;
  for (const auto& item : items_parsed["items"]) {
    EXPECT_EQ(item["status"], "succeeded");
    job_ids.insert(item["job_id"].get<std::string>());
  }
  EXPECT_EQ(job_ids.size(), valid_records) << "no duplicate jobs may exist for this workload";

  auto users_res = client.Get("/api/v1/users?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(users_res);
  auto users_parsed = nlohmann::json::parse(users_res->body);
  EXPECT_GE(users_parsed["total"].get<std::size_t>(), valid_records);

  app->http_server().stop();
  server_thread.join();
}

// --- Phase 3E: Products -------------------------------------------------

TEST_F(ProcessRoutesTest, PreviewCsvProductsExtractsRecordsAndCreatesNoWorkload) {
  auto client = make_client();
  auto before = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(before);
  const auto before_count = nlohmann::json::parse(before->body)["workloads"].size();

  const std::string csv =
      "sku,name,price,currency,category,description,stock_quantity\n"
      "WID-1,Widget,19.99,USD,Tools,A fine widget,10\n"
      "WID-2,Gadget,5.50,,Toys,,3\n"
      "BAD-1,Bad Product,-5.00,,,,\n";
  auto res = client.Post("/api/v1/process/preview", process_request("csv", "products", csv));
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["source"], "csv");
  EXPECT_EQ(parsed["target"], "products");
  EXPECT_EQ(parsed["total_records"], 3);
  EXPECT_EQ(parsed["valid_records"], 2);
  EXPECT_EQ(parsed["invalid_records"], 1);
  ASSERT_EQ(parsed["records"].size(), 2u);
  EXPECT_EQ(parsed["records"][0]["sku"], "WID-1");
  EXPECT_EQ(parsed["records"][0]["price"], "19.99");
  EXPECT_EQ(parsed["records"][1]["currency"], "USD")
      << "currency must default to USD when the column is blank";
  ASSERT_EQ(parsed["rejected_records"].size(), 1u);
  EXPECT_EQ(parsed["rejected_records"][0]["index"], 3);

  auto after = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(after);
  EXPECT_EQ(nlohmann::json::parse(after->body)["workloads"].size(), before_count)
      << "preview must never create a workload";
}

TEST_F(ProcessRoutesTest, ConfirmCsvProductsCreatesOneWorkloadAndPersistsRealProducts) {
  auto client = make_client();
  const std::string csv = "sku,name,price\nWID-1,Widget,19.99\nWID-2,Gadget,5.50\n";
  auto preview_res = client.Post("/api/v1/process/preview", process_request("csv", "products", csv));
  ASSERT_TRUE(preview_res);
  ASSERT_EQ(preview_res->status, 200);
  auto preview_parsed = nlohmann::json::parse(preview_res->body);

  nlohmann::json confirm_body{{"target", "products"}, {"records", preview_parsed["records"]}};
  auto confirm_res = client.Post("/api/v1/process/confirm", confirm_body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  EXPECT_EQ(confirm_parsed["type"], "product.process");
  EXPECT_EQ(confirm_parsed["total_records"], 2);
  EXPECT_EQ(confirm_parsed["valid_records"], 2);
  EXPECT_EQ(confirm_parsed["total_items"], 2);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();

  std::string final_status;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    ASSERT_TRUE(get_res);
    auto workload = nlohmann::json::parse(get_res->body);
    final_status = workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "succeeded");

  // The job handler (handlers::ProductProcessHandler), not this route,
  // persisted these -- GET /api/v1/products reads real repository state.
  auto products_res = client.Get("/api/v1/products?limit=50");
  ASSERT_TRUE(products_res);
  ASSERT_EQ(products_res->status, 200);
  auto products_parsed = nlohmann::json::parse(products_res->body);
  EXPECT_EQ(products_parsed["total"], 2u);
  ASSERT_EQ(products_parsed["products"].size(), 2u);
  bool found_widget = false;
  for (const auto& product : products_parsed["products"]) {
    if (product["sku"] == "WID-1") {
      found_widget = true;
      EXPECT_EQ(product["name"], "Widget");
      EXPECT_DOUBLE_EQ(product["price"].get<double>(), 19.99);
      EXPECT_FALSE(product["job_id"].is_null());
    }
  }
  EXPECT_TRUE(found_widget);
}

TEST_F(ProcessRoutesTest, ConfirmNeverSubmitsAnInvalidProductAsAJob) {
  auto client = make_client();
  nlohmann::json body{
      {"target", "products"},
      {"records", nlohmann::json::array({{{"sku", "GOOD-1"}, {"name", "Good"}, {"price", "10.00"}},
                                         {{"sku", "BAD-1"}, {"name", "Bad"}, {"price", "-5.00"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["valid_records"], 1);
  EXPECT_EQ(parsed["invalid_records"], 1);
  EXPECT_EQ(parsed["total_items"], 1);
  ASSERT_EQ(parsed["items"].size(), 1u);
}

TEST_F(ProcessRoutesTest, ConfirmProductsRevalidatesPriceRatherThanTrustingTheClient) {
  auto client = make_client();
  nlohmann::json body{
      {"target", "products"},
      {"records", nlohmann::json::array({{{"sku", "X"}, {"name", "X"}, {"price", "not-a-number"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["valid_records"], 0);
  EXPECT_EQ(parsed["invalid_records"], 1);
}

TEST_F(ProcessRoutesTest, GetProductsReturnsEmptyListInitially) {
  auto client = make_client();
  auto res = client.Get("/api/v1/products");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total"], 0u);
  EXPECT_TRUE(parsed["products"].empty());
}

TEST_F(ProcessRoutesTest, GetProductsSupportsPagination) {
  auto client = make_client();
  nlohmann::json records = nlohmann::json::array();
  for (int i = 0; i < 5; ++i) {
    records.push_back({{"sku", "SKU-" + std::to_string(i)}, {"name", "Item"}, {"price", "1.00"}});
  }
  nlohmann::json body{{"target", "products"}, {"records", records}};
  auto confirm_res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  const std::string workload_id = nlohmann::json::parse(confirm_res->body)["id"].get<std::string>();

  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    auto workload = nlohmann::json::parse(get_res->body);
    if (workload["status"] == "succeeded") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  auto page1 = client.Get("/api/v1/products?limit=2&offset=0");
  ASSERT_TRUE(page1);
  auto page1_parsed = nlohmann::json::parse(page1->body);
  EXPECT_EQ(page1_parsed["total"], 5u);
  EXPECT_EQ(page1_parsed["products"].size(), 2u);

  auto page2 = client.Get("/api/v1/products?limit=2&offset=4");
  ASSERT_TRUE(page2);
  EXPECT_EQ(nlohmann::json::parse(page2->body)["products"].size(), 1u);
}

// --- Phase 3F: Categories -------------------------------------------------

TEST_F(ProcessRoutesTest, PreviewCsvCategoriesExtractsRecordsAndCreatesNoWorkload) {
  auto client = make_client();
  auto before = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(before);
  const auto before_count = nlohmann::json::parse(before->body)["workloads"].size();

  const std::string csv =
      "name,slug,description,parent_slug\n"
      "Electronics,,Gadgets and gizmos,\n"
      "Home & Kitchen,,,\n"
      "###,,this name has no letters or digits,\n";
  auto res = client.Post("/api/v1/process/preview", process_request("csv", "categories", csv));
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["source"], "csv");
  EXPECT_EQ(parsed["target"], "categories");
  EXPECT_EQ(parsed["total_records"], 3);
  EXPECT_EQ(parsed["valid_records"], 2);
  EXPECT_EQ(parsed["invalid_records"], 1);
  ASSERT_EQ(parsed["records"].size(), 2u);
  EXPECT_EQ(parsed["records"][0]["name"], "Electronics");
  EXPECT_EQ(parsed["records"][0]["slug"], "electronics");
  EXPECT_EQ(parsed["records"][1]["slug"], "home-kitchen")
      << "slug must be derived deterministically from name when the slug column is blank";
  ASSERT_EQ(parsed["rejected_records"].size(), 1u);
  EXPECT_EQ(parsed["rejected_records"][0]["index"], 3);

  auto after = client.Get("/api/v1/workloads?limit=200");
  ASSERT_TRUE(after);
  EXPECT_EQ(nlohmann::json::parse(after->body)["workloads"].size(), before_count)
      << "preview must never create a workload";
}

TEST_F(ProcessRoutesTest, ConfirmCsvCategoriesCreatesOneWorkloadAndPersistsRealCategories) {
  auto client = make_client();
  const std::string csv = "name\nElectronics\nHome & Kitchen\n";
  auto preview_res = client.Post("/api/v1/process/preview", process_request("csv", "categories", csv));
  ASSERT_TRUE(preview_res);
  ASSERT_EQ(preview_res->status, 200);
  auto preview_parsed = nlohmann::json::parse(preview_res->body);

  nlohmann::json confirm_body{{"target", "categories"}, {"records", preview_parsed["records"]}};
  auto confirm_res = client.Post("/api/v1/process/confirm", confirm_body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  EXPECT_EQ(confirm_parsed["type"], "category.process");
  EXPECT_EQ(confirm_parsed["total_records"], 2);
  EXPECT_EQ(confirm_parsed["valid_records"], 2);
  EXPECT_EQ(confirm_parsed["total_items"], 2);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();

  std::string final_status;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    ASSERT_TRUE(get_res);
    auto workload = nlohmann::json::parse(get_res->body);
    final_status = workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "succeeded");

  // The job handler (handlers::CategoryProcessHandler), not this route,
  // persisted these -- GET /api/v1/categories reads real repository state.
  auto categories_res = client.Get("/api/v1/categories?limit=50");
  ASSERT_TRUE(categories_res);
  ASSERT_EQ(categories_res->status, 200);
  auto categories_parsed = nlohmann::json::parse(categories_res->body);
  EXPECT_EQ(categories_parsed["total"], 2u);
  ASSERT_EQ(categories_parsed["categories"].size(), 2u);
  bool found_electronics = false;
  for (const auto& category : categories_parsed["categories"]) {
    if (category["slug"] == "electronics") {
      found_electronics = true;
      EXPECT_EQ(category["name"], "Electronics");
      EXPECT_FALSE(category["job_id"].is_null());
    }
  }
  EXPECT_TRUE(found_electronics);
}

TEST_F(ProcessRoutesTest, ConfirmNeverSubmitsAnInvalidCategoryAsAJob) {
  auto client = make_client();
  nlohmann::json body{{"target", "categories"},
                      {"records", nlohmann::json::array({{{"name", "Electronics"}}, {{"name", "###"}}})}};
  auto res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["valid_records"], 1);
  EXPECT_EQ(parsed["invalid_records"], 1);
  EXPECT_EQ(parsed["total_items"], 1);
  ASSERT_EQ(parsed["items"].size(), 1u);
}

TEST_F(ProcessRoutesTest, ConfirmCategoryWithValidPreexistingParentSucceeds) {
  auto client = make_client();

  // Two-stage import (see docs/architecture/category-processing.md,
  // "Parent semantics"): the parent must already be a persisted category
  // before a child job referencing it can succeed.
  nlohmann::json parent_body{{"target", "categories"},
                             {"records", nlohmann::json::array({{{"name", "Electronics"}}})}};
  auto parent_res = client.Post("/api/v1/process/confirm", parent_body.dump(), "application/json");
  ASSERT_TRUE(parent_res);
  ASSERT_EQ(parent_res->status, 201);
  const std::string parent_workload_id = nlohmann::json::parse(parent_res->body)["id"].get<std::string>();
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + parent_workload_id);
    if (nlohmann::json::parse(get_res->body)["status"] == "succeeded")
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  nlohmann::json child_body{
      {"target", "categories"},
      {"records", nlohmann::json::array({{{"name", "Laptops"}, {"parent_slug", "electronics"}}})}};
  auto child_res = client.Post("/api/v1/process/confirm", child_body.dump(), "application/json");
  ASSERT_TRUE(child_res);
  ASSERT_EQ(child_res->status, 201);
  const std::string child_workload_id = nlohmann::json::parse(child_res->body)["id"].get<std::string>();

  std::string final_status;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + child_workload_id);
    auto workload = nlohmann::json::parse(get_res->body);
    final_status = workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed")
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "succeeded");

  auto categories_res = client.Get("/api/v1/categories?limit=50");
  auto categories_parsed = nlohmann::json::parse(categories_res->body);
  bool found_laptops = false;
  for (const auto& category : categories_parsed["categories"]) {
    if (category["slug"] == "laptops") {
      found_laptops = true;
      EXPECT_EQ(category["parent_slug"], "electronics");
    }
  }
  EXPECT_TRUE(found_laptops);
}

TEST_F(ProcessRoutesTest, MultiLevelHierarchyInOneSubmissionSucceedsRegardlessOfRecordOrder) {
  // Phase 3H follow-up regression: sibling jobs execute in parallel, so a
  // child's job could run before its same-submission parent's job had
  // committed and fail permanently with "parent does not exist". Records
  // are deliberately ordered child-first here. The child is retried
  // through the real retry engine until the parent exists.
  auto client = make_client();
  nlohmann::json body{
      {"target", "categories"},
      {"records", nlohmann::json::array({{{"name", "Gaming Laptops"}, {"parent_slug", "laptops"}},
                                         {{"name", "Laptops"}, {"parent_slug", "electronics"}},
                                         {{"name", "Electronics"}}})}};
  auto confirm_res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  ASSERT_EQ(confirm_parsed["valid_records"], 3);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();

  // Worst case is two retries (default backoff 1s then 2s) for the
  // grandchild, so allow well beyond that.
  nlohmann::json workload;
  for (int i = 0; i < 300; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    workload = nlohmann::json::parse(get_res->body);
    if (workload["status"] == "succeeded" || workload["status"] == "failed")
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_EQ(workload["status"], "succeeded") << workload.dump();
  EXPECT_EQ(workload["completed_items"], 3);
  EXPECT_EQ(workload["failed_items"], 0);

  auto categories_parsed = nlohmann::json::parse(client.Get("/api/v1/categories?limit=50")->body);
  std::map<std::string, nlohmann::json> by_slug;
  for (const auto& category : categories_parsed["categories"]) {
    by_slug[category["slug"].get<std::string>()] = category["parent_slug"];
  }
  ASSERT_EQ(by_slug.size(), 3u);
  EXPECT_TRUE(by_slug["electronics"].is_null());
  EXPECT_EQ(by_slug["laptops"], "electronics");
  EXPECT_EQ(by_slug["gaming-laptops"], "laptops");
}

TEST_F(ProcessRoutesTest, ConfirmCategoryWithMissingParentIsAcceptedAtConfirmButFailsAsAJob) {
  // Parent-existence is a database-dependent check that only
  // CategoryProcessHandler (which owns a repository) can perform -- see
  // its class comment. confirm() itself has no database access beyond
  // WorkloadService, so a record referencing a nonexistent parent passes
  // confirm()'s structural revalidation and becomes a real job, which then
  // fails at execution time -- a deliberate, documented tradeoff, not a
  // bug.
  auto client = make_client();
  nlohmann::json body{
      {"target", "categories"},
      {"records", nlohmann::json::array({{{"name", "Laptops"}, {"parent_slug", "does-not-exist"}}})}};
  auto confirm_res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  EXPECT_EQ(confirm_parsed["valid_records"], 1)
      << "structurally valid at confirm time -- parent existence is "
         "checked later, at job execution";
  const std::string workload_id = confirm_parsed["id"].get<std::string>();

  std::string final_status;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    auto workload = nlohmann::json::parse(get_res->body);
    final_status = workload["status"].get<std::string>();
    if (final_status == "succeeded" || final_status == "failed")
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "failed");

  auto items_res = client.Get("/api/v1/workloads/" + workload_id + "/items?limit=10");
  auto items_parsed = nlohmann::json::parse(items_res->body);
  ASSERT_EQ(items_parsed["items"].size(), 1u);
  EXPECT_EQ(items_parsed["items"][0]["status"], "failed");
  EXPECT_NE(items_parsed["items"][0]["last_error"].get<std::string>().find("does not exist"),
            std::string::npos);
}

TEST_F(ProcessRoutesTest, GetCategoriesReturnsEmptyListInitially) {
  auto client = make_client();
  auto res = client.Get("/api/v1/categories");
  ASSERT_TRUE(res);
  ASSERT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total"], 0u);
  EXPECT_TRUE(parsed["categories"].empty());
}

TEST_F(ProcessRoutesTest, GetCategoriesSupportsPagination) {
  auto client = make_client();
  nlohmann::json records = nlohmann::json::array();
  for (int i = 0; i < 5; ++i) {
    records.push_back({{"name", "Category " + std::to_string(i)}});
  }
  nlohmann::json body{{"target", "categories"}, {"records", records}};
  auto confirm_res = client.Post("/api/v1/process/confirm", body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  const std::string workload_id = nlohmann::json::parse(confirm_res->body)["id"].get<std::string>();

  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/workloads/" + workload_id);
    auto workload = nlohmann::json::parse(get_res->body);
    if (workload["status"] == "succeeded") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  auto page1 = client.Get("/api/v1/categories?limit=2&offset=0");
  ASSERT_TRUE(page1);
  auto page1_parsed = nlohmann::json::parse(page1->body);
  EXPECT_EQ(page1_parsed["total"], 5u);
  EXPECT_EQ(page1_parsed["categories"].size(), 2u);

  auto page2 = client.Get("/api/v1/categories?limit=2&offset=4");
  ASSERT_TRUE(page2);
  EXPECT_EQ(nlohmann::json::parse(page2->body)["categories"].size(), 1u);
}

// --- Phase 3E: 100+ product acceptance (CSV and image) ------------------

/// Shared by both the CSV and image 100+ product acceptance tests below:
/// preview -> assert DB-write-free -> confirm -> assert exactly one
/// workload/one job per valid record -> poll to a genuine terminal state
/// -> assert every job succeeded and every persisted `products` row is
/// real, reconciled data. `min_valid_records` differs between CSV
/// (deterministic, exact expected count) and image (real OCR, "at
/// least" this many) -- see each caller.
void run_bulk_products_acceptance(httplib::Client& client,
                                  const httplib::MultipartFormDataItems& preview_items,
                                  std::size_t expected_total, std::size_t min_valid_records) {
  auto before = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(before);
  const std::size_t workloads_before = nlohmann::json::parse(before->body)["workloads"].size();

  auto preview_res = client.Post("/api/v1/process/preview", preview_items);
  ASSERT_TRUE(preview_res);
  ASSERT_EQ(preview_res->status, 200);
  auto preview_parsed = nlohmann::json::parse(preview_res->body);
  const std::size_t total_records = preview_parsed["total_records"].get<std::size_t>();
  const std::size_t valid_records = preview_parsed["valid_records"].get<std::size_t>();
  ASSERT_EQ(total_records, expected_total);
  ASSERT_GE(valid_records, min_valid_records);

  auto after_preview = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_preview);
  EXPECT_EQ(nlohmann::json::parse(after_preview->body)["workloads"].size(), workloads_before)
      << "preview must never create a workload";

  nlohmann::json confirm_body{{"target", "products"}, {"records", preview_parsed["records"]}};
  auto confirm_res = client.Post("/api/v1/process/confirm", confirm_body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();
  EXPECT_EQ(confirm_parsed["type"], "product.process");
  EXPECT_EQ(confirm_parsed["total_items"], valid_records);
  for (const auto& item : confirm_parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
  }

  auto after_confirm = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_confirm);
  EXPECT_EQ(nlohmann::json::parse(after_confirm->body)["workloads"].size(), workloads_before + 1)
      << "confirm must create exactly one workload";

  std::string final_status;
  nlohmann::json final_workload;
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
  EXPECT_EQ(final_workload["completed_items"], valid_records);
  EXPECT_EQ(final_workload["failed_items"], 0u);
  EXPECT_EQ(final_workload["queued_items"], 0u);
  EXPECT_EQ(final_workload["running_items"], 0u);

  auto items_res =
      client.Get("/api/v1/workloads/" + workload_id + "/items?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(items_res);
  auto items_parsed = nlohmann::json::parse(items_res->body);
  EXPECT_EQ(items_parsed["total"], valid_records);
  std::unordered_set<std::string> job_ids;
  for (const auto& item : items_parsed["items"]) {
    EXPECT_EQ(item["status"], "succeeded");
    job_ids.insert(item["job_id"].get<std::string>());
  }
  EXPECT_EQ(job_ids.size(), valid_records) << "no duplicate jobs may exist for this workload";

  // Every job persisted a real products row -- GET /api/v1/products
  // reads actual repository state, not the job response.
  auto products_res = client.Get("/api/v1/products?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(products_res);
  auto products_parsed = nlohmann::json::parse(products_res->body);
  EXPECT_GE(products_parsed["total"].get<std::size_t>(), valid_records);
}

TEST(ProcessRoutesBulkPostgresTest, HundredProductCsvFlowReconcilesAgainstRealPostgres) {
  if (!postgres_test_database_url()) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk product acceptance test.";
  }
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *postgres_test_database_url();
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

  const std::string csv = read_fixture_file("products_bulk_100.csv");
  ASSERT_FALSE(csv.empty());
  httplib::MultipartFormDataItems preview_items{
      {.name = "source", .content = "csv", .filename = "", .content_type = ""},
      {.name = "target", .content = "products", .filename = "", .content_type = ""},
      {.name = "file", .content = csv, .filename = "products.csv", .content_type = "text/csv"},
  };
  // The fixture deterministically has exactly 5 invalid (negative-price)
  // rows out of 100 -- see docs/architecture/product-processing.md,
  // "100+ product fixture".
  run_bulk_products_acceptance(client, preview_items, /*expected_total=*/100, /*min_valid_records=*/95);

  app->http_server().stop();
  server_thread.join();
}

TEST(ProcessRoutesBulkPostgresTest, HundredProductImageFlowReconcilesAgainstRealPostgres) {
  if (!providers::TesseractCliOcrProvider::discover_executable()) {
    GTEST_SKIP() << "No usable Tesseract OCR binary found -- skipping real-OCR bulk product test.";
  }
  if (!postgres_test_database_url()) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk product acceptance test.";
  }
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *postgres_test_database_url();
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

  const std::string image_bytes = read_fixture_file("products_bulk_100.png");
  ASSERT_FALSE(image_bytes.empty());
  httplib::MultipartFormDataItems preview_items{
      {.name = "source", .content = "image", .filename = "", .content_type = ""},
      {.name = "target", .content = "products", .filename = "", .content_type = ""},
      {.name = "file", .content = image_bytes, .filename = "products.png", .content_type = "image/png"},
  };
  // Real OCR: "at least" a large majority, not an exact count -- see
  // docs/architecture/product-processing.md, "100+ product fixture".
  run_bulk_products_acceptance(client, preview_items, /*expected_total=*/100, /*min_valid_records=*/80);

  app->http_server().stop();
  server_thread.join();
}

// --- Phase 3F: 100+ category acceptance (CSV and image) -----------------

/// Shared by both the CSV and image 100+ category acceptance tests below
/// -- mirrors run_bulk_products_acceptance's structure exactly (preview ->
/// assert DB-write-free -> confirm -> assert exactly one workload/one job
/// per valid record -> poll to a genuine terminal state -> assert every
/// job succeeded and every persisted `categories` row is real, reconciled
/// data). The fixture is deliberately flat (no parent_slug references) --
/// parent semantics have their own focused tests
/// (ConfirmCategoryWithValidPreexistingParentSucceeds /
/// ConfirmCategoryWithMissingParentIsAcceptedAtConfirmButFailsAsAJob)
/// rather than being mixed into this scale/reconciliation test.
void run_bulk_categories_acceptance(httplib::Client& client,
                                    const httplib::MultipartFormDataItems& preview_items,
                                    std::size_t expected_total, std::size_t min_valid_records) {
  auto before = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(before);
  const std::size_t workloads_before = nlohmann::json::parse(before->body)["workloads"].size();

  auto preview_res = client.Post("/api/v1/process/preview", preview_items);
  ASSERT_TRUE(preview_res);
  ASSERT_EQ(preview_res->status, 200);
  auto preview_parsed = nlohmann::json::parse(preview_res->body);
  const std::size_t total_records = preview_parsed["total_records"].get<std::size_t>();
  const std::size_t valid_records = preview_parsed["valid_records"].get<std::size_t>();
  ASSERT_EQ(total_records, expected_total);
  ASSERT_GE(valid_records, min_valid_records);

  auto after_preview = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_preview);
  EXPECT_EQ(nlohmann::json::parse(after_preview->body)["workloads"].size(), workloads_before)
      << "preview must never create a workload";

  nlohmann::json confirm_body{{"target", "categories"}, {"records", preview_parsed["records"]}};
  auto confirm_res = client.Post("/api/v1/process/confirm", confirm_body.dump(), "application/json");
  ASSERT_TRUE(confirm_res);
  ASSERT_EQ(confirm_res->status, 201);
  auto confirm_parsed = nlohmann::json::parse(confirm_res->body);
  const std::string workload_id = confirm_parsed["id"].get<std::string>();
  EXPECT_EQ(confirm_parsed["type"], "category.process");
  EXPECT_EQ(confirm_parsed["total_items"], valid_records);
  for (const auto& item : confirm_parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
  }

  auto after_confirm = client.Get("/api/v1/workloads?limit=500");
  ASSERT_TRUE(after_confirm);
  EXPECT_EQ(nlohmann::json::parse(after_confirm->body)["workloads"].size(), workloads_before + 1)
      << "confirm must create exactly one workload";

  std::string final_status;
  nlohmann::json final_workload;
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
  EXPECT_EQ(final_workload["completed_items"], valid_records);
  EXPECT_EQ(final_workload["failed_items"], 0u);
  EXPECT_EQ(final_workload["queued_items"], 0u);
  EXPECT_EQ(final_workload["running_items"], 0u);

  auto items_res =
      client.Get("/api/v1/workloads/" + workload_id + "/items?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(items_res);
  auto items_parsed = nlohmann::json::parse(items_res->body);
  EXPECT_EQ(items_parsed["total"], valid_records);
  std::unordered_set<std::string> job_ids;
  for (const auto& item : items_parsed["items"]) {
    EXPECT_EQ(item["status"], "succeeded");
    job_ids.insert(item["job_id"].get<std::string>());
  }
  EXPECT_EQ(job_ids.size(), valid_records) << "no duplicate jobs may exist for this workload";

  // Every job persisted a real categories row -- GET /api/v1/categories
  // reads actual repository state, not the job response.
  auto categories_res = client.Get("/api/v1/categories?limit=" + std::to_string(valid_records));
  ASSERT_TRUE(categories_res);
  auto categories_parsed = nlohmann::json::parse(categories_res->body);
  EXPECT_GE(categories_parsed["total"].get<std::size_t>(), valid_records);
}

TEST(ProcessRoutesBulkPostgresTest, HundredCategoryCsvFlowReconcilesAgainstRealPostgres) {
  if (!postgres_test_database_url()) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk category acceptance test.";
  }
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *postgres_test_database_url();
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

  const std::string csv = read_fixture_file("categories_bulk_100.csv");
  ASSERT_FALSE(csv.empty());
  httplib::MultipartFormDataItems preview_items{
      {.name = "source", .content = "csv", .filename = "", .content_type = ""},
      {.name = "target", .content = "categories", .filename = "", .content_type = ""},
      {.name = "file", .content = csv, .filename = "categories.csv", .content_type = "text/csv"},
  };
  // The fixture deterministically has exactly 5 invalid ("###" name, no
  // letters or digits to derive a slug from) rows out of 100 -- see
  // docs/architecture/category-processing.md, "100+ category fixture".
  run_bulk_categories_acceptance(client, preview_items, /*expected_total=*/100, /*min_valid_records=*/95);

  app->http_server().stop();
  server_thread.join();
}

TEST(ProcessRoutesBulkPostgresTest, HundredCategoryImageFlowReconcilesAgainstRealPostgres) {
  if (!providers::TesseractCliOcrProvider::discover_executable()) {
    GTEST_SKIP() << "No usable Tesseract OCR binary found -- skipping real-OCR bulk category test.";
  }
  if (!postgres_test_database_url()) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                    "bulk category acceptance test.";
  }
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *postgres_test_database_url();
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

  const std::string image_bytes = read_fixture_file("categories_bulk_100.png");
  ASSERT_FALSE(image_bytes.empty());
  httplib::MultipartFormDataItems preview_items{
      {.name = "source", .content = "image", .filename = "", .content_type = ""},
      {.name = "target", .content = "categories", .filename = "", .content_type = ""},
      {.name = "file", .content = image_bytes, .filename = "categories.png", .content_type = "image/png"},
  };
  // Real OCR: "at least" a large majority, not an exact count -- see
  // docs/architecture/category-processing.md, "100+ category fixture".
  run_bulk_categories_acceptance(client, preview_items, /*expected_total=*/100, /*min_valid_records=*/80);

  app->http_server().stop();
  server_thread.join();
}

}  // namespace
}  // namespace flowforge::server
