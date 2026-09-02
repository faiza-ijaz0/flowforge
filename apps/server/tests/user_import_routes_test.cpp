#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "flowforge/infra/config.hpp"
#include "http/app.hpp"

namespace flowforge::server {
namespace {

/// Exercises POST /api/v1/workloads/user-imports and GET
/// /api/v1/workloads/{id}/items end-to-end against a real App -- mirrors
/// WorkloadRoutesTest's fixture (workload_routes_test.cpp).
class UserImportRoutesTest : public ::testing::Test {
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

  static httplib::MultipartFormDataItems csv_upload(std::string content) {
    return {
        {.name = "file", .content = std::move(content), .filename = "users.csv", .content_type = "text/csv"}};
  }

  std::unique_ptr<App> app_;
  std::thread server_thread_;
  int port_ = 0;
};

TEST_F(UserImportRoutesTest, SuccessfulImportCreatesWorkloadAndReportsSummary) {
  auto client = make_client();
  const std::string csv =
      "name,email\n"
      "Alice Khan,ALICE@example.com\n"
      "Bob,bob@example.com\n";
  auto res = client.Post("/api/v1/workloads/user-imports", csv_upload(csv));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);

  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["type"], "user.process");
  EXPECT_EQ(parsed["total_rows"], 2);
  EXPECT_EQ(parsed["valid_rows"], 2);
  EXPECT_EQ(parsed["invalid_rows"], 0);
  EXPECT_EQ(parsed["total_items"], 2);
  ASSERT_TRUE(parsed.contains("items"));
  EXPECT_EQ(parsed["items"].size(), 2u);
  for (const auto& item : parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
  }
}

TEST_F(UserImportRoutesTest, PartialValidationFailureReportsRejectedRows) {
  auto client = make_client();
  const std::string csv =
      "name,email\n"
      "Alice,alice@example.com\n"
      ",blank-name@example.com\n"
      "Bob,not-an-email\n";
  auto res = client.Post("/api/v1/workloads/user-imports", csv_upload(csv));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);

  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_rows"], 3);
  EXPECT_EQ(parsed["valid_rows"], 1);
  EXPECT_EQ(parsed["invalid_rows"], 2);
  ASSERT_TRUE(parsed.contains("rejected_rows"));
  EXPECT_EQ(parsed["rejected_rows"].size(), 2u);
  EXPECT_EQ(parsed["total_items"], 1);
}

TEST_F(UserImportRoutesTest, MalformedCsvIsRejectedWithoutCreatingAWorkload) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads/user-imports", csv_upload("not,a,valid,header\n"));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["error"]["code"], "validation_error");

  auto listed = client.Get("/api/v1/workloads");
  ASSERT_TRUE(listed);
  EXPECT_EQ(nlohmann::json::parse(listed->body)["workloads"].size(), 0u);
}

TEST_F(UserImportRoutesTest, MissingFileFieldIsRejected) {
  auto client = make_client();
  httplib::MultipartFormDataItems items{
      {.name = "not_file", .content = "irrelevant", .filename = "", .content_type = "text/plain"}};
  auto res = client.Post("/api/v1/workloads/user-imports", items);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(UserImportRoutesTest, NonMultipartRequestIsRejected) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads/user-imports", "name,email\nAlice,a@example.com\n", "text/csv");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(UserImportRoutesTest, EmptyFileIsRejected) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads/user-imports", csv_upload(""));
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(UserImportRoutesTest, ItemsEndpointReturnsBoundedPageWithNameAndEmail) {
  auto client = make_client();
  const std::string csv =
      "name,email\n"
      "Alice,alice@example.com\n"
      "Bob,bob@example.com\n"
      "Carol,carol@example.com\n";
  auto created = client.Post("/api/v1/workloads/user-imports", csv_upload(csv));
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);
  const std::string workload_id = nlohmann::json::parse(created->body)["id"].get<std::string>();

  auto page = client.Get("/api/v1/workloads/" + workload_id + "/items?limit=2&offset=0");
  ASSERT_TRUE(page);
  EXPECT_EQ(page->status, 200);
  auto parsed = nlohmann::json::parse(page->body);
  EXPECT_EQ(parsed["total"], 3);
  EXPECT_EQ(parsed["limit"], 2);
  EXPECT_EQ(parsed["offset"], 0);
  ASSERT_EQ(parsed["items"].size(), 2u);
  for (const auto& item : parsed["items"]) {
    EXPECT_FALSE(item["name"].is_null());
    EXPECT_FALSE(item["email"].is_null());
    EXPECT_TRUE(item.contains("status"));
    EXPECT_TRUE(item.contains("attempt_count"));
  }

  auto page2 = client.Get("/api/v1/workloads/" + workload_id + "/items?limit=2&offset=2");
  ASSERT_TRUE(page2);
  EXPECT_EQ(nlohmann::json::parse(page2->body)["items"].size(), 1u);
}

TEST_F(UserImportRoutesTest, ItemsEndpointReturnsNotFoundForUnknownWorkload) {
  auto client = make_client();
  auto res = client.Get("/api/v1/workloads/00000000-0000-0000-0000-000000000000/items");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 404);
}

}  // namespace
}  // namespace flowforge::server
