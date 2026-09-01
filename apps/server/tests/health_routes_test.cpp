// Phase 2B-5: exercises GET /health and GET /ready in isolation from a
// full App -- register_health_routes() takes ReadinessChecks as plain
// std::function<bool()> callbacks, so the readiness contract itself
// (honest 503 on a failed dependency, safe defaulting when a check is
// omitted) can be tested deterministically with hand-crafted callbacks
// instead of trying to simulate a real PostgreSQL outage mid-test.

#include "http/routes/health_routes.hpp"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#include "flowforge/infra/config.hpp"
#include "flowforge/infra/metrics.hpp"

namespace flowforge::server {
namespace {

class HealthRoutesTest : public ::testing::Test {
 protected:
  void start_with(ReadinessChecks readiness) {
    metrics_ = infra::make_in_memory_metrics_registry();
    register_health_routes(server_, metrics_, config_, std::chrono::steady_clock::now(),
                           std::move(readiness));

    port_ = server_.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port_, 0);
    server_thread_ = std::thread([this] { server_.listen_after_bind(); });
    for (int i = 0; i < 100 && !server_.is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(server_.is_running());
  }

  void TearDown() override {
    if (server_.is_running()) {
      server_.stop();
      server_thread_.join();
    }
  }

  [[nodiscard]] httplib::Client make_client() const {
    httplib::Client client("127.0.0.1", port_);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    return client;
  }

  infra::AppConfig config_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  httplib::Server server_;
  std::thread server_thread_;
  int port_ = 0;
};

TEST_F(HealthRoutesTest, HealthNeverConsultsReadinessChecks) {
  // A liveness probe that depends on the same checks as readiness would
  // let an unhealthy PostgreSQL take the load balancer's liveness check
  // (and therefore the whole process) down with it -- /health must stay
  // "ok" regardless of what readiness would say.
  start_with(ReadinessChecks{.database_healthy = [] { return false; },
                             .scheduler_running = [] { return false; },
                             .worker_pool_running = [] { return false; },
                             .retry_dispatcher_running = [] { return false; }});
  auto client = make_client();
  auto res = client.Get("/health");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("\"ok\""), std::string::npos);
}

TEST_F(HealthRoutesTest, ReadyReturns200WhenEveryDependencyIsHealthy) {
  start_with(ReadinessChecks{.database_healthy = [] { return true; },
                             .scheduler_running = [] { return true; },
                             .worker_pool_running = [] { return true; },
                             .retry_dispatcher_running = [] { return true; }});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body.at("status"), "ok");
  EXPECT_EQ(body.at("checks").at("database"), "ok");
}

TEST_F(HealthRoutesTest, ReadyReturns503WhenDatabaseIsUnavailable) {
  start_with(ReadinessChecks{.database_healthy = [] { return false; },
                             .scheduler_running = [] { return true; },
                             .worker_pool_running = [] { return true; },
                             .retry_dispatcher_running = [] { return true; }});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  // The single most important assertion in this file: a caller that only
  // checks the HTTP status code (the common case for a load balancer)
  // must be told "not ready", never 200.
  EXPECT_EQ(res->status, 503);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body.at("status"), "unavailable");
  EXPECT_EQ(body.at("checks").at("database"), "unavailable");
  EXPECT_EQ(body.at("checks").at("scheduler"), "ok");
}

TEST_F(HealthRoutesTest, ReadyReturns503WhenSchedulerIsNotRunning) {
  start_with(ReadinessChecks{.database_healthy = [] { return true; },
                             .scheduler_running = [] { return false; },
                             .worker_pool_running = [] { return true; },
                             .retry_dispatcher_running = [] { return true; }});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);
}

TEST_F(HealthRoutesTest, ReadyReturns503WhenWorkerPoolIsNotRunning) {
  start_with(ReadinessChecks{.database_healthy = [] { return true; },
                             .scheduler_running = [] { return true; },
                             .worker_pool_running = [] { return false; },
                             .retry_dispatcher_running = [] { return true; }});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);
}

TEST_F(HealthRoutesTest, ReadyReturns503WhenRetryDispatcherIsNotRunning) {
  start_with(ReadinessChecks{.database_healthy = [] { return true; },
                             .scheduler_running = [] { return true; },
                             .worker_pool_running = [] { return true; },
                             .retry_dispatcher_running = [] { return false; }});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);
}

TEST_F(HealthRoutesTest, OmittedChecksDefaultToHealthy) {
  // An empty std::function is a safe "no opinion" default, not a crash --
  // exercised because ReadinessChecks is an aggregate a caller could
  // partially initialize.
  start_with(ReadinessChecks{});
  auto client = make_client();
  auto res = client.Get("/ready");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
}

TEST_F(HealthRoutesTest, MetricsEndpointStillRendersRegisteredCounters) {
  start_with(ReadinessChecks{.database_healthy = [] { return true; },
                             .scheduler_running = [] { return true; },
                             .worker_pool_running = [] { return true; },
                             .retry_dispatcher_running = [] { return true; }});
  metrics_->increment_counter("flowforge_test_probe_total");
  auto client = make_client();
  auto res = client.Get("/metrics");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_NE(res->body.find("flowforge_test_probe_total"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::server
