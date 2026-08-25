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

}  // namespace
}  // namespace flowforge::infra
