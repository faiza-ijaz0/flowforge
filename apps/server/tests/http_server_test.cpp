#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#include "flowforge/infra/config.hpp"
#include "http/app.hpp"

namespace flowforge::server {
namespace {

/// End-to-end integration test: boots a real App on an OS-assigned
/// ephemeral port and drives it with a real httplib::Client, exercising
/// the full HTTP -> route -> JobService -> InMemoryJobRepository stack.
class HttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    infra::AppConfig config;
    config.log_level = infra::LogLevel::Off;
    app_ = std::make_unique<App>(config);

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

TEST_F(HttpServerTest, HealthReturnsOk) {
  auto client = make_client();
  auto res = client.Get("/health");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("\"ok\""), std::string::npos);
}

TEST_F(HttpServerTest, ReadyReturnsEnvironmentAndUptime) {
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("uptime_seconds"), std::string::npos);
}

TEST_F(HttpServerTest, MetricsReturnsPlainText) {
  auto client = make_client();
  auto res = client.Get("/metrics");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
}

TEST_F(HttpServerTest, CreateJobThenFetchIt) {
  auto client = make_client();
  const std::string body = R"({"queue_name":"emails","payload":{"to":"a@example.com"}})";
  auto create_res = client.Post("/api/v1/jobs", body, "application/json");
  ASSERT_TRUE(create_res);
  EXPECT_EQ(create_res->status, 201);

  auto created_json = nlohmann::json::parse(create_res->body);
  const std::string id = created_json.at("id").get<std::string>();
  EXPECT_EQ(created_json.at("status"), "pending");

  auto get_res = client.Get("/api/v1/jobs/" + id);
  ASSERT_TRUE(get_res);
  EXPECT_EQ(get_res->status, 200);
  auto fetched_json = nlohmann::json::parse(get_res->body);
  EXPECT_EQ(fetched_json.at("id"), id);
}

TEST_F(HttpServerTest, CreateJobRejectsMissingQueueName) {
  auto client = make_client();
  auto res = client.Post("/api/v1/jobs", R"({"payload":{}})", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 400);
}

TEST_F(HttpServerTest, GetUnknownJobReturns404) {
  auto client = make_client();
  auto res = client.Get("/api/v1/jobs/does-not-exist");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 404);
}

TEST_F(HttpServerTest, CancelJobTransitionsStatus) {
  auto client = make_client();
  auto create_res = client.Post("/api/v1/jobs", R"({"queue_name":"q","payload":{}})", "application/json");
  ASSERT_TRUE(create_res);
  const std::string id = nlohmann::json::parse(create_res->body).at("id").get<std::string>();

  auto cancel_res = client.Post("/api/v1/jobs/" + id + "/cancel", "", "application/json");
  ASSERT_TRUE(cancel_res);
  EXPECT_EQ(cancel_res->status, 200);
  EXPECT_EQ(nlohmann::json::parse(cancel_res->body).at("status"), "cancelled");
}

TEST_F(HttpServerTest, ListJobsReturnsCreatedJobs) {
  auto client = make_client();
  client.Post("/api/v1/jobs", R"({"queue_name":"q","payload":{}})", "application/json");
  client.Post("/api/v1/jobs", R"({"queue_name":"q","payload":{}})", "application/json");

  auto res = client.Get("/api/v1/jobs");
  ASSERT_TRUE(res);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_GE(body.at("jobs").size(), 2u);
}

TEST_F(HttpServerTest, ListWorkflowsReturnsEmptyArray) {
  auto client = make_client();
  auto res = client.Get("/api/v1/workflows");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.at("workflows").empty());
}

TEST_F(HttpServerTest, ListWorkersReturnsEmptyArray) {
  auto client = make_client();
  auto res = client.Get("/api/v1/workers");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.at("workers").empty());
}

}  // namespace
}  // namespace flowforge::server
