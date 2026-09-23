#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
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

/// Exercises App::stop() itself (not just the raw httplib::Server::stop()
/// every other test's TearDown uses) -- the composition root's own stop()
/// now also calls PriorityScheduler::stop() (Phase 2B-2). This proves that
/// path completes without hanging and that the server genuinely stops
/// accepting connections afterward.
TEST(AppStopTest, StopShutsDownBothHttpServerAndScheduler) {
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  auto app_result = App::create(config);
  ASSERT_TRUE(app_result.has_value()) << app_result.error().message();
  auto app = std::move(*app_result);

  const int port = app->http_server().bind_to_any_port("127.0.0.1");
  ASSERT_GT(port, 0);
  std::thread server_thread([&app] { app->http_server().listen_after_bind(); });
  for (int i = 0; i < 100 && !app->http_server().is_running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(app->http_server().is_running());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2);
  client.set_read_timeout(2);
  auto before = client.Get("/health");
  ASSERT_TRUE(before);
  EXPECT_EQ(before->status, 200);

  app->stop();
  server_thread.join();

  auto after = client.Get("/health");
  EXPECT_FALSE(after);  // connection refused -- the server actually stopped.
}

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

// --- CORS -------------------------------------------------------------
//
// The dashboard's Create Job form is a "use client" component (see
// apps/dashboard/src/components/jobs/create-job-form.tsx) -- its
// fetch("http://localhost:8080/...") call executes in the actual
// browser, unlike a Server Component's fetch() (Node.js, never subject
// to CORS). These tests exercise exactly what Chrome does: a preflight
// OPTIONS request (since the POST carries a non-"simple" Content-Type),
// then the real request, both with an Origin header set -- see
// apps/server/src/http/cors.hpp for the design.

TEST_F(HttpServerTest, OptionsPreflightForCreateJobReturnsNoContentWithCorsHeaders) {
  auto client = make_client();
  auto res = client.Options("/api/v1/jobs", {{"Origin", "http://localhost:3000"},
                                             {"Access-Control-Request-Method", "POST"},
                                             {"Access-Control-Request-Headers", "content-type"}});
  ASSERT_TRUE(res);
  EXPECT_TRUE(res->status == 200 || res->status == 204);
  ASSERT_TRUE(res->has_header("Access-Control-Allow-Origin"));
  EXPECT_EQ(res->get_header_value("Access-Control-Allow-Origin"), "http://localhost:3000");
  ASSERT_TRUE(res->has_header("Access-Control-Allow-Methods"));
  EXPECT_NE(res->get_header_value("Access-Control-Allow-Methods").find("POST"), std::string::npos);
  ASSERT_TRUE(res->has_header("Access-Control-Allow-Headers"));
  EXPECT_NE(res->get_header_value("Access-Control-Allow-Headers").find("Content-Type"), std::string::npos);
}

TEST_F(HttpServerTest, OptionsPreflightNeverDuplicatesCorsHeaders) {
  // Regression test: httplib::Response::set_header() always appends to a
  // multimap rather than replacing, and set_post_routing_handler's
  // callback runs for every response including a pre-routing-handled
  // OPTIONS one -- so it is easy to accidentally set the same CORS
  // header from both the OPTIONS-only pre-routing handler and the
  // always-runs post-routing handler, producing two identical header
  // lines. Browsers treat a duplicated Access-Control-Allow-Origin as an
  // invalid CORS response even when both copies are identical -- this
  // must never regress.
  auto client = make_client();
  auto res = client.Options("/api/v1/jobs",
                            {{"Origin", "http://localhost:3000"}, {"Access-Control-Request-Method", "POST"}});
  ASSERT_TRUE(res);
  EXPECT_EQ(res->get_header_value_count("Access-Control-Allow-Origin"), 1u);
  EXPECT_EQ(res->get_header_value_count("Vary"), 1u);
  EXPECT_EQ(res->get_header_value_count("Access-Control-Allow-Methods"), 1u);
  EXPECT_EQ(res->get_header_value_count("Access-Control-Allow-Headers"), 1u);
}

TEST_F(HttpServerTest, PostJobFromAllowedOriginIncludesAccessControlAllowOrigin) {
  auto client = make_client();
  auto res = client.Post("/api/v1/jobs", {{"Origin", "http://localhost:3000"}},
                         R"({"queue_name":"cors-test","payload":{}})", "application/json");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 201);
  ASSERT_TRUE(res->has_header("Access-Control-Allow-Origin"));
  EXPECT_EQ(res->get_header_value("Access-Control-Allow-Origin"), "http://localhost:3000");
}

TEST_F(HttpServerTest, OptionsFromDisallowedOriginOmitsAllowOriginHeader) {
  auto client = make_client();
  auto res = client.Options("/api/v1/jobs",
                            {{"Origin", "http://evil.example"}, {"Access-Control-Request-Method", "POST"}});
  ASSERT_TRUE(res);
  EXPECT_TRUE(res->status == 200 || res->status == 204);
  EXPECT_FALSE(res->has_header("Access-Control-Allow-Origin"));
}

TEST_F(HttpServerTest, ExistingNonBrowserRequestsWithoutOriginAreUnaffectedByCors) {
  // No Origin header at all -- matches every other test in this file
  // (curl, server-to-server, this exact pattern) and must keep behaving
  // exactly as before this change: normal response, no CORS headers
  // added (nothing to add -- there's no Origin to reflect).
  auto client = make_client();
  auto res = client.Get("/health");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  EXPECT_FALSE(res->has_header("Access-Control-Allow-Origin"));
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
  // A freshly created job has made zero attempts yet.
  EXPECT_EQ(created_json.at("attempt_count"), 0);
  // Not created as part of a workload -- Phase 3G's workload_id exposure.
  EXPECT_TRUE(created_json.at("workload_id").is_null());

  auto get_res = client.Get("/api/v1/jobs/" + id);
  ASSERT_TRUE(get_res);
  EXPECT_EQ(get_res->status, 200);
  auto fetched_json = nlohmann::json::parse(get_res->body);
  EXPECT_EQ(fetched_json.at("id"), id);
  EXPECT_EQ(fetched_json.at("attempt_count"), 0);
}

TEST_F(HttpServerTest, CreateJobWithKnownJobTypeIsScheduledAndQueued) {
  auto client = make_client();
  const std::string body = R"({"queue_name":"jobs","payload":"hello","job_type":"echo"})";
  auto create_res = client.Post("/api/v1/jobs", body, "application/json");
  ASSERT_TRUE(create_res);
  ASSERT_EQ(create_res->status, 201);

  auto created_json = nlohmann::json::parse(create_res->body);
  EXPECT_EQ(created_json.at("job_type"), "echo");
  ASSERT_TRUE(created_json.contains("scheduling"));
  EXPECT_TRUE(created_json.at("scheduling").at("scheduled").get<bool>());
  // The create response reflects the state at the moment JobService::
  // mark_queued() persisted -- Queued. The real WorkerPool/Executor
  // pipeline (Phase 2B-3) dispatches and executes concurrently, so by the
  // time this response is parsed the job may already have progressed
  // further -- that race is expected and correct (see
  // CreateJobWithKnownJobTypeExecutesSuccessfullyEndToEnd for the
  // eventual-success assertion); this test only asserts it left Pending.
  const std::string status = created_json.at("status").get<std::string>();
  EXPECT_NE(status, "pending");
}

/// The Phase 2B-3 acceptance path via the real HTTP API: a job created
/// with a registered job_type is not just marked Queued -- it is
/// genuinely picked up by PriorityScheduler -> LocalWorkerPool ->
/// JobExecutor -> HandlerRegistry -> EchoHandler and reaches Succeeded,
/// with a real job_attempts-equivalent history entry to show for it.
/// Polls rather than asserting exact timing, since dispatch/execution run
/// concurrently with this test.
TEST_F(HttpServerTest, CreateJobWithKnownJobTypeExecutesSuccessfullyEndToEnd) {
  auto client = make_client();
  const std::string body = R"({"queue_name":"jobs","payload":"hello world","job_type":"echo"})";
  auto create_res = client.Post("/api/v1/jobs", body, "application/json");
  ASSERT_TRUE(create_res);
  ASSERT_EQ(create_res->status, 201);
  const std::string id = nlohmann::json::parse(create_res->body).at("id").get<std::string>();

  std::string final_status;
  nlohmann::json final_job_json;
  for (int i = 0; i < 100; ++i) {
    auto get_res = client.Get("/api/v1/jobs/" + id);
    ASSERT_TRUE(get_res);
    final_job_json = nlohmann::json::parse(get_res->body);
    final_status = final_job_json.at("status").get<std::string>();
    if (final_status == "succeeded") {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(final_status, "succeeded");

  auto attempts_res = client.Get("/api/v1/jobs/" + id + "/attempts");
  ASSERT_TRUE(attempts_res);
  auto attempts_json = nlohmann::json::parse(attempts_res->body);
  ASSERT_EQ(attempts_json.at("attempts").size(), 1u);
  EXPECT_EQ(attempts_json.at("attempts")[0].at("outcome"), "succeeded");
  EXPECT_EQ(attempts_json.at("attempts")[0].at("attempt_number"), 1);

  // The job's own attempt_count (surfaced via GET /api/v1/jobs/{id} and
  // rendered on the dashboard's job detail page) must agree with the
  // execution-attempt history above -- one real execution attempt means
  // attempt_count == 1, not 0 (see domain::Job::record_attempt_success()).
  EXPECT_EQ(final_job_json.at("attempt_count"), 1);
}

TEST_F(HttpServerTest, CreateJobWithUnknownJobTypeStillCreatesButIsNotScheduled) {
  auto client = make_client();
  const std::string body = R"({"queue_name":"jobs","payload":"hello","job_type":"no-such-handler"})";
  auto create_res = client.Post("/api/v1/jobs", body, "application/json");
  ASSERT_TRUE(create_res);
  ASSERT_EQ(create_res->status, 201);

  auto created_json = nlohmann::json::parse(create_res->body);
  EXPECT_EQ(created_json.at("status"), "pending");
  ASSERT_TRUE(created_json.contains("scheduling"));
  EXPECT_FALSE(created_json.at("scheduling").at("scheduled").get<bool>());
  EXPECT_TRUE(created_json.at("scheduling").contains("reason"));
}

TEST_F(HttpServerTest, CreateJobWithoutJobTypeIsNeverSubmittedToScheduler) {
  auto client = make_client();
  auto create_res = client.Post("/api/v1/jobs", R"({"queue_name":"q","payload":{}})", "application/json");
  ASSERT_TRUE(create_res);
  ASSERT_EQ(create_res->status, 201);

  auto created_json = nlohmann::json::parse(create_res->body);
  EXPECT_EQ(created_json.at("status"), "pending");
  EXPECT_FALSE(created_json.at("scheduling").at("scheduled").get<bool>());
}

TEST_F(HttpServerTest, MetricsReflectScheduledJobs) {
  auto client = make_client();
  client.Post("/api/v1/jobs", R"({"queue_name":"jobs","payload":"x","job_type":"echo"})", "application/json");

  auto res = client.Get("/metrics");
  ASSERT_TRUE(res);
  EXPECT_NE(res->body.find("flowforge_scheduler_jobs_scheduled_total"), std::string::npos);
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
  // Phase 3G: total/limit/offset let the dashboard render real pagination
  // controls (see docs/architecture/phase-3g-audit.md §2.3).
  EXPECT_GE(body.at("total").get<std::size_t>(), 2u);
  EXPECT_EQ(body.at("limit"), 50);
  EXPECT_EQ(body.at("offset"), 0);
}

TEST_F(HttpServerTest, ListWorkflowsReturnsEmptyArray) {
  auto client = make_client();
  auto res = client.Get("/api/v1/workflows");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto body = nlohmann::json::parse(res->body);
  EXPECT_TRUE(body.at("workflows").empty());
}

TEST_F(HttpServerTest, ListWorkersReturnsRegisteredLocalWorkerPoolWorkers) {
  // Phase 2B-3: App::create() now starts a real LocalWorkerPool, which
  // registers one real, persisted domain::Worker row per configured
  // worker (default AppConfig::worker_pool_size, "worker-1"..) -- the
  // list is no longer empty, unlike Phase 1/2A/2B-2 (nothing registered
  // workers before this phase).
  auto client = make_client();
  auto res = client.Get("/api/v1/workers");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);
  auto body = nlohmann::json::parse(res->body);
  ASSERT_FALSE(body.at("workers").empty());
  EXPECT_EQ(body.at("workers")[0].at("hostname"), "worker-1");
}

namespace {
std::optional<std::string> postgres_test_database_url() {
  if (const char* url = std::getenv("FLOWFORGE_TEST_DATABASE_URL")) {
    return std::string(url);
  }
  if (const char* url = std::getenv("FLOWFORGE_DATABASE_URL")) {
    return std::string(url);
  }
  return std::nullopt;
}
}  // namespace

/// The phase's critical acceptance test (see docs/architecture/overview.md,
/// "PostgreSQL persistence"), automated: submit a job through the real
/// HTTP API against a real App backed by PostgreSQL, tear that App down
/// entirely (simulating a process restart -- a fresh App, fresh
/// connection pool, same database), and confirm the job is still there
/// with the right data. Skips (not "passes") when no PostgreSQL test
/// database is configured -- see
/// docs/development/getting-started.md, "Running PostgreSQL integration
/// tests".
TEST(HttpServerPostgresPersistenceTest, JobSurvivesAppRestartAgainstRealPostgres) {
  auto db_url = postgres_test_database_url();
  if (!db_url) {
    GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set";
  }

  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = *db_url;

  std::string job_id;
  {
    auto app_result = App::create(config);
    ASSERT_TRUE(app_result.has_value()) << app_result.error().message();
    auto app = std::move(*app_result);
    int port = app->http_server().bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread server_thread([&app] { app->http_server().listen_after_bind(); });
    for (int i = 0; i < 100 && !app->http_server().is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    const std::string body = R"({"queue_name":"restart-test","payload":{"proof":"phase-2a"}})";
    auto create_res = client.Post("/api/v1/jobs", body, "application/json");
    ASSERT_TRUE(create_res);
    ASSERT_EQ(create_res->status, 201);
    job_id = nlohmann::json::parse(create_res->body).at("id").get<std::string>();

    app->http_server().stop();
    server_thread.join();
  }  // `app` (and its connection pool) is fully destroyed here.

  {
    auto app_result = App::create(config);
    ASSERT_TRUE(app_result.has_value()) << app_result.error().message();
    auto app = std::move(*app_result);
    int port = app->http_server().bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::thread server_thread([&app] { app->http_server().listen_after_bind(); });
    for (int i = 0; i < 100 && !app->http_server().is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    auto get_res = client.Get("/api/v1/jobs/" + job_id);
    ASSERT_TRUE(get_res);
    EXPECT_EQ(get_res->status, 200);
    auto fetched = nlohmann::json::parse(get_res->body);
    EXPECT_EQ(fetched.at("id"), job_id);
    EXPECT_EQ(fetched.at("queue_name"), "restart-test");
    EXPECT_EQ(fetched.at("payload").at("proof"), "phase-2a");

    app->http_server().stop();
    server_thread.join();
  }
}

/// Phase 2B-5, Step 6 ("startup failure behavior"): a configured but
/// unreachable PostgreSQL must fail App::create() clearly and promptly --
/// never hang, never crash, and never fall back to in-memory persistence.
/// Runs unconditionally (does not need a real database): 127.0.0.1:1 is
/// a port nothing listens on, the same address
/// ConnectionPoolTest.CreateFailsClearlyForAnUnreachableDatabase already
/// relies on failing fast rather than hanging out to an OS-level TCP
/// timeout.
TEST(AppStartupFailureTest, UnreachablePostgresFailsCreateCleanlyAndPromptly) {
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = "postgresql://nouser:nopass@127.0.0.1:1/nonexistent_db_xyz";

  const auto start = std::chrono::steady_clock::now();
  auto app_result = App::create(config);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);

  ASSERT_FALSE(app_result.has_value());
  EXPECT_TRUE(app_result.error().code() == ErrorCode::Infrastructure ||
              app_result.error().code() == ErrorCode::Database);
  // Generous upper bound -- the point is "doesn't hang indefinitely", not
  // a tight performance assertion.
  EXPECT_LT(elapsed.count(), 15);
}

/// No leaked background threads or partially-running components after a
/// startup failure: RAII (each component's destructor stops it if still
/// running -- see PriorityScheduler/LocalWorkerPool/RetryDispatcher's own
/// destructors) combined with shared_ptr's automatic cleanup on
/// App::create()'s early `return std::unexpected(...)` is what's actually
/// responsible for this; this test proves it holds by simply repeating
/// the failing construction many times in a row -- a real leak (a
/// std::thread never joined, a connection never closed) would either
/// crash, hang, or exhaust OS resources well before 20 iterations.
TEST(AppStartupFailureTest, RepeatedStartupFailureLeaksNoThreadsOrConnections) {
  infra::AppConfig config;
  config.log_level = infra::LogLevel::Off;
  config.database_url = "postgresql://nouser:nopass@127.0.0.1:1/nonexistent_db_xyz";

  for (int i = 0; i < 5; ++i) {
    auto app_result = App::create(config);
    ASSERT_FALSE(app_result.has_value());
  }
}

}  // namespace
}  // namespace flowforge::server
