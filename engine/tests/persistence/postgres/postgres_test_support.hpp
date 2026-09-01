#pragma once

// Shared support for the PostgreSQL integration test suites in this
// directory. These tests are opt-in: they only run against a real
// PostgreSQL instance (never a mock), selected via
// FLOWFORGE_TEST_DATABASE_URL (falling back to FLOWFORGE_DATABASE_URL).
// If neither is set, every TEST_F using PostgresIntegrationTest calls
// GTEST_SKIP() -- reported honestly by GoogleTest/CTest as "SKIPPED", not
// silently treated as a pass. See docs/development/getting-started.md,
// "Running PostgreSQL integration tests".

#include <pqxx/pqxx>

#include <gtest/gtest.h>
#include <cstdlib>
#include <optional>
#include <string>

#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"

namespace flowforge::persistence::postgres::test {

[[nodiscard]] inline std::optional<std::string> test_database_url() {
  if (const char* url = std::getenv("FLOWFORGE_TEST_DATABASE_URL")) {
    return std::string(url);
  }
  if (const char* url = std::getenv("FLOWFORGE_DATABASE_URL")) {
    return std::string(url);
  }
  return std::nullopt;
}

/// Wipes every table these repositories touch. Integration tests must run
/// against a dedicated, disposable test database (see the getting-started
/// doc) -- never point FLOWFORGE_TEST_DATABASE_URL at anything with data
/// worth keeping.
inline void truncate_all(pqxx::connection& connection) {
  pqxx::work txn(connection);
  txn.exec(
      "TRUNCATE workflow_step_dependencies, workflow_steps, workflows, job_attempts, jobs, workers, "
      "queues, audit_logs RESTART IDENTITY CASCADE");
  txn.commit();
}

/// Base fixture: skips the whole suite when no test database is
/// configured, otherwise opens a real connection pool against it and
/// truncates all tables before each test so tests are independent and
/// deterministic regardless of execution order.
class PostgresIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto url = test_database_url();
    if (!url) {
      GTEST_SKIP() << "FLOWFORGE_TEST_DATABASE_URL (or FLOWFORGE_DATABASE_URL) is not set -- skipping "
                      "PostgreSQL integration tests. See docs/development/getting-started.md.";
    }
    logger_ = infra::make_logger(infra::LogLevel::Off, false);
    auto pool_result =
        PgConnectionPool::create(PgPoolConfig{.connection_string = *url, .pool_size = 2}, logger_);
    ASSERT_TRUE(pool_result.has_value()) << pool_result.error().message();
    pool_ = *pool_result;

    auto conn = pool_->acquire();
    ASSERT_TRUE(conn.has_value()) << conn.error().message();
    truncate_all(**conn);
  }

  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<PgConnectionPool> pool_;
};

}  // namespace flowforge::persistence::postgres::test
