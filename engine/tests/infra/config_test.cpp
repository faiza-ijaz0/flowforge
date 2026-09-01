#include "flowforge/infra/config.hpp"

#include <gtest/gtest.h>

#include <map>

namespace flowforge::infra {
namespace {

AppConfig::GetenvFn env_from_map(std::map<std::string, std::string> values) {
  return [values = std::move(values)](std::string_view key) -> std::optional<std::string> {
    auto it = values.find(std::string(key));
    if (it == values.end())
      return std::nullopt;
    return it->second;
  };
}

TEST(AppConfigTest, DefaultsAreDevelopmentFriendly) {
  auto result = AppConfig::load(env_from_map({}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->environment, Environment::Development);
  EXPECT_EQ(result->server_host, "0.0.0.0");
  EXPECT_EQ(result->server_port, 8080);
  EXPECT_EQ(result->worker_count, 4u);
  EXPECT_FALSE(result->structured_logging);
  EXPECT_TRUE(result->database_url.empty());
}

TEST(AppConfigTest, ProductionRequiresDatabaseUrl) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_ENV", "production"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, ProductionWithDatabaseUrlSucceeds) {
  auto result = AppConfig::load(env_from_map({
      {"FLOWFORGE_ENV", "production"},
      {"FLOWFORGE_DATABASE_URL", "postgres://user:pass@localhost:5432/flowforge"},
  }));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->environment, Environment::Production);
  EXPECT_TRUE(result->structured_logging);
}

TEST(AppConfigTest, InvalidEnvironmentRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_ENV", "nonsense"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, InvalidPortRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_SERVER_PORT", "70000"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, NonNumericPortRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_SERVER_PORT", "not-a-number"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, LogLevelParsedCaseInsensitively) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_LOG_LEVEL", "DEBUG"}}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->log_level, LogLevel::Debug);
}

TEST(AppConfigTest, StructuredLoggingCanBeOverriddenInDevelopment) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_STRUCTURED_LOGGING", "true"}}));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->structured_logging);
}

TEST(AppConfigTest, SchedulerSettingsDefaultToSaneValues) {
  auto result = AppConfig::load(env_from_map({}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheduler_queue_capacity, 1024u);
  EXPECT_EQ(result->scheduler_dispatch_workers, 2u);
}

TEST(AppConfigTest, SchedulerSettingsCanBeOverridden) {
  auto result = AppConfig::load(env_from_map(
      {{"FLOWFORGE_SCHEDULER_QUEUE_CAPACITY", "256"}, {"FLOWFORGE_SCHEDULER_DISPATCH_WORKERS", "8"}}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheduler_queue_capacity, 256u);
  EXPECT_EQ(result->scheduler_dispatch_workers, 8u);
}

TEST(AppConfigTest, InvalidSchedulerQueueCapacityRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_SCHEDULER_QUEUE_CAPACITY", "0"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, InvalidSchedulerDispatchWorkersRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_SCHEDULER_DISPATCH_WORKERS", "not-a-number"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, WorkerPoolAndExecutionSettingsDefaultToSaneValues) {
  auto result = AppConfig::load(env_from_map({}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->worker_pool_size, 4u);
  EXPECT_EQ(result->worker_pool_queue_capacity, 1024u);
  EXPECT_EQ(result->execution_timeout_ms, 60'000u);
}

TEST(AppConfigTest, WorkerPoolAndExecutionSettingsCanBeOverridden) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_WORKER_POOL_SIZE", "8"},
                                              {"FLOWFORGE_WORKER_POOL_QUEUE_CAPACITY", "256"},
                                              {"FLOWFORGE_EXECUTION_TIMEOUT_MS", "5000"}}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->worker_pool_size, 8u);
  EXPECT_EQ(result->worker_pool_queue_capacity, 256u);
  EXPECT_EQ(result->execution_timeout_ms, 5000u);
}

TEST(AppConfigTest, InvalidWorkerPoolSizeRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_WORKER_POOL_SIZE", "0"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, InvalidExecutionTimeoutRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_EXECUTION_TIMEOUT_MS", "not-a-number"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, RetryDispatcherSettingsDefaultToSaneValues) {
  auto result = AppConfig::load(env_from_map({}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->retry_poll_interval_ms, std::chrono::milliseconds{500});
  EXPECT_EQ(result->retry_batch_size, 50u);
}

TEST(AppConfigTest, RetryDispatcherSettingsCanBeOverridden) {
  auto result = AppConfig::load(
      env_from_map({{"FLOWFORGE_RETRY_POLL_INTERVAL_MS", "250"}, {"FLOWFORGE_RETRY_BATCH_SIZE", "10"}}));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->retry_poll_interval_ms, std::chrono::milliseconds{250});
  EXPECT_EQ(result->retry_batch_size, 10u);
}

TEST(AppConfigTest, InvalidRetryPollIntervalRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_RETRY_POLL_INTERVAL_MS", "0"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

TEST(AppConfigTest, InvalidRetryBatchSizeRejected) {
  auto result = AppConfig::load(env_from_map({{"FLOWFORGE_RETRY_BATCH_SIZE", "not-a-number"}}));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Configuration);
}

}  // namespace
}  // namespace flowforge::infra
