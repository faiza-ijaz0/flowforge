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

}  // namespace
}  // namespace flowforge::server
