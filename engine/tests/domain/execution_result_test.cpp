#include "flowforge/domain/execution_result.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(ExecutionResultTest, SuccessCarriesOutputDurationAndMetadata) {
  auto result = ExecutionResult::success("output-value", std::chrono::milliseconds{42}, {{"k", "v"}});
  EXPECT_TRUE(result.succeeded());
  EXPECT_EQ(result.status(), ExecutionResultStatus::Succeeded);
  EXPECT_EQ(result.output(), "output-value");
  EXPECT_EQ(result.duration().count(), 42);
  EXPECT_FALSE(result.error_code().has_value());
  EXPECT_FALSE(result.error_message().has_value());
  EXPECT_FALSE(result.retryable());
  ASSERT_TRUE(result.metadata().contains("k"));
  EXPECT_EQ(result.metadata().at("k"), "v");
}

TEST(ExecutionResultTest, SuccessDefaultsToEmptyMetadata) {
  auto result = ExecutionResult::success("out", std::chrono::milliseconds{0});
  EXPECT_TRUE(result.metadata().empty());
}

TEST(ExecutionResultTest, FailureCarriesErrorCodeMessageAndRetryability) {
  auto result = ExecutionResult::failure(ErrorCode::JobExecution, "boom", /*retryable=*/true,
                                         std::chrono::milliseconds{7});
  EXPECT_FALSE(result.succeeded());
  EXPECT_EQ(result.status(), ExecutionResultStatus::Failed);
  ASSERT_TRUE(result.error_code().has_value());
  EXPECT_EQ(*result.error_code(), ErrorCode::JobExecution);
  ASSERT_TRUE(result.error_message().has_value());
  EXPECT_EQ(*result.error_message(), "boom");
  EXPECT_TRUE(result.retryable());
  EXPECT_EQ(result.duration().count(), 7);
  EXPECT_TRUE(result.output().empty());
}

TEST(ExecutionResultTest, FailureDefaultsRetryableFalseWhenRequested) {
  auto result = ExecutionResult::failure(ErrorCode::Validation, "bad input", /*retryable=*/false,
                                         std::chrono::milliseconds{1});
  EXPECT_FALSE(result.retryable());
}

TEST(ExecutionResultTest, StatusToStringRoundTrips) {
  EXPECT_EQ(to_string(ExecutionResultStatus::Succeeded), "succeeded");
  EXPECT_EQ(to_string(ExecutionResultStatus::Failed), "failed");
}

}  // namespace
}  // namespace flowforge::domain
