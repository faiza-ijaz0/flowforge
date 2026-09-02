#include "flowforge/handlers/user_process_handler.hpp"

#include <gtest/gtest.h>

#include <string>

#include "flowforge/infra/logger.hpp"

namespace flowforge::handlers {
namespace {

engine::ExecutionContext make_context(std::shared_ptr<std::atomic<bool>> cancelled = nullptr) {
  return {infra::JobId::generate(),
          infra::ExecutionId::generate(),
          "worker-1",
          infra::make_logger(infra::LogLevel::Off, false),
          nullptr,
          std::move(cancelled)};
}

TEST(UserProcessHandlerTest, JobTypeIsUserProcess) {
  UserProcessHandler handler;
  EXPECT_EQ(handler.job_type(), "user.process");
}

TEST(UserProcessHandlerTest, NormalizesWhitespaceAndEmailCase) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "  Alice Khan ", "email": " ALICE@EXAMPLE.COM "})");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), R"({"name":"Alice Khan","email":"alice@example.com","valid":true})");
}

TEST(UserProcessHandlerTest, IncludesOptionalPhoneWhenPresent) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result =
      handler.execute(context, R"({"name": "Bob", "email": "bob@example.com", "phone": " 555-1234 "})");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), R"({"name":"Bob","email":"bob@example.com","phone":"555-1234","valid":true})");
}

TEST(UserProcessHandlerTest, MissingNameIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"email": "a@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, BlankNameIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "   ", "email": "a@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, MissingEmailIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, MalformedEmailIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice", "email": "not-an-email"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, EmailWithMultipleAtsIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice", "email": "a@b@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, OversizedPayloadIsRejected) {
  UserProcessHandler handler;
  auto context = make_context();
  const std::string oversized(std::size_t{20} * 1024, 'a');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, CancelledBeforeExecutionReturnsNonRetryableFailure) {
  UserProcessHandler handler;
  auto cancelled = std::make_shared<std::atomic<bool>>(true);
  auto context = make_context(cancelled);
  auto result = handler.execute(context, R"({"name": "Alice", "email": "a@example.com"})");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->succeeded());
  EXPECT_FALSE(result->retryable());
}

}  // namespace
}  // namespace flowforge::handlers
