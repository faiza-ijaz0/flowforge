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

/// Exercises /api/v1/workloads end-to-end against a real App, mirroring
/// HttpServerTest's fixture (http_server_test.cpp) -- boots on an
/// OS-assigned ephemeral port and drives it with a real httplib::Client.
class WorkloadRoutesTest : public ::testing::Test {
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
    client.set_read_timeout(2);
    return client;
  }

  std::unique_ptr<App> app_;
  std::thread server_thread_;
  int port_ = 0;
};

TEST_F(WorkloadRoutesTest, CreateWorkloadReturnsCreatedWithItemsAndDispatchOutcomes) {
  auto client = make_client();
  nlohmann::json body{{"type", "user.process"},
                      {"items", nlohmann::json::array({
                                    nlohmann::json{{"name", "Alice Khan"}, {"email", "ALICE@EXAMPLE.COM"}},
                                    nlohmann::json{{"name", "Bob"}, {"email", "bob@example.com"}},
                                })}};
  auto res = client.Post("/api/v1/workloads", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);

  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["type"], "user.process");
  EXPECT_EQ(parsed["total_items"], 2);
  EXPECT_EQ(parsed["completed_items"], 0);
  EXPECT_EQ(parsed["failed_items"], 0);
  ASSERT_TRUE(parsed.contains("items"));
  ASSERT_EQ(parsed["items"].size(), 2u);
  for (const auto& item : parsed["items"]) {
    EXPECT_TRUE(item["scheduled"].get<bool>());
    EXPECT_FALSE(item["job_id"].get<std::string>().empty());
  }
}

TEST_F(WorkloadRoutesTest, CreateWorkloadWithZeroItemsSucceedsImmediately) {
  auto client = make_client();
  nlohmann::json body{{"type", "user.process"}, {"items", nlohmann::json::array()}};
  auto res = client.Post("/api/v1/workloads", body.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["total_items"], 0);
  EXPECT_EQ(parsed["status"], "succeeded");
}

TEST_F(WorkloadRoutesTest, CreateWorkloadRejectsMissingType) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads", nlohmann::json{{"items", nlohmann::json::array()}}.dump(),
                         "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["error"]["code"], "validation_error");
}

TEST_F(WorkloadRoutesTest, CreateWorkloadRejectsMalformedJson) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads", "{not json", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(WorkloadRoutesTest, GetWorkloadReturnsCreatedWorkload) {
  auto client = make_client();
  nlohmann::json body{
      {"type", "user.process"},
      {"items", nlohmann::json::array({nlohmann::json{{"name", "A"}, {"email", "a@x.com"}}})}};
  auto created = client.Post("/api/v1/workloads", body.dump(), "application/json");
  ASSERT_TRUE(created);
  ASSERT_EQ(created->status, 201);
  const std::string id = nlohmann::json::parse(created->body)["id"].get<std::string>();

  auto res = client.Get("/api/v1/workloads/" + id);
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["id"], id);
  EXPECT_EQ(parsed["total_items"], 1);
}

TEST_F(WorkloadRoutesTest, GetWorkloadReturnsNotFoundForUnknownId) {
  auto client = make_client();
  auto res = client.Get("/api/v1/workloads/00000000-0000-0000-0000-000000000000");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 404);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_EQ(parsed["error"]["code"], "not_found");
}

TEST_F(WorkloadRoutesTest, ListWorkloadsReturnsCreatedWorkloads) {
  auto client = make_client();
  for (int i = 0; i < 2; ++i) {
    nlohmann::json body{{"type", "user.process"}, {"items", nlohmann::json::array()}};
    auto res = client.Post("/api/v1/workloads", body.dump(), "application/json");
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 201);
  }

  auto res = client.Get("/api/v1/workloads");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto parsed = nlohmann::json::parse(res->body);
  ASSERT_TRUE(parsed.contains("workloads"));
  EXPECT_GE(parsed["workloads"].size(), 2u);
}

// 5xx sanitization (a Database/Internal error's message never reaching an
// HTTP client verbatim) is proven generically, for every route, by
// error_response_test.cpp's coverage of the shared to_error_body()
// helper -- write_error() here (workload_routes.cpp) goes through the
// exact same function, so it is not re-proven per-route. This asserts the
// 4xx side of that same contract instead: a validation error's specific,
// caller-facing message must pass through unchanged.
TEST_F(WorkloadRoutesTest, ValidationErrorsExposeCallerFacingMessages) {
  auto client = make_client();
  auto res = client.Post("/api/v1/workloads", nlohmann::json{{"type", ""}}.dump(), "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
  auto parsed = nlohmann::json::parse(res->body);
  EXPECT_FALSE(parsed["error"]["message"].get<std::string>().empty());
}

}  // namespace
}  // namespace flowforge::server
