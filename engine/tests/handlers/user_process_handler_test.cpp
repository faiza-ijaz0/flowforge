#include "flowforge/handlers/user_process_handler.hpp"

#include <gtest/gtest.h>

#include <string>

#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

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
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  EXPECT_EQ(handler.job_type(), "user.process");
}

TEST(UserProcessHandlerTest, NormalizesWhitespaceAndEmailCase) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "  Alice Khan ", "email": " ALICE@EXAMPLE.COM "})");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), R"({"name":"Alice Khan","email":"alice@example.com","valid":true})");
}

TEST(UserProcessHandlerTest, IncludesOptionalPhoneWhenPresent) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result =
      handler.execute(context, R"({"name": "Bob", "email": "bob@example.com", "phone": " 555-1234 "})");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), R"({"name":"Bob","email":"bob@example.com","phone":"555-1234","valid":true})");
}

TEST(UserProcessHandlerTest, MissingNameIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"email": "a@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, BlankNameIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "   ", "email": "a@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, MissingEmailIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, MalformedEmailIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice", "email": "not-an-email"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, EmailWithMultipleAtsIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Alice", "email": "a@b@example.com"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, OversizedPayloadIsRejected) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto context = make_context();
  const std::string oversized(std::size_t{20} * 1024, 'a');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(UserProcessHandlerTest, CancelledBeforeExecutionReturnsNonRetryableFailure) {
  UserProcessHandler handler(std::make_shared<persistence::InMemoryUserRepository>());
  auto cancelled = std::make_shared<std::atomic<bool>>(true);
  auto context = make_context(cancelled);
  auto result = handler.execute(context, R"({"name": "Alice", "email": "a@example.com"})");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->succeeded());
  EXPECT_FALSE(result->retryable());
}

// --- Phase 3H: persistence -------------------------------------------

TEST(UserProcessHandlerTest, NormalizesAndPersistsAValidUser) {
  auto repository = std::make_shared<persistence::InMemoryUserRepository>();
  UserProcessHandler handler(repository);
  auto context = make_context();
  auto result = handler.execute(
      context, R"({"name": " Alice Khan ", "email": " ALICE@EXAMPLE.COM ", "phone": "555-1"})");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_TRUE(result->succeeded());

  auto found = repository->find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->name, "Alice Khan");
  ASSERT_TRUE((*found)->phone.has_value());
  EXPECT_EQ(*(*found)->phone, "555-1");
  EXPECT_EQ((*found)->job_id, context.job_id());
}

TEST(UserProcessHandlerTest, ReimportingTheSameEmailUpdatesTheExistingRow) {
  auto repository = std::make_shared<persistence::InMemoryUserRepository>();
  UserProcessHandler handler(repository);

  auto first = handler.execute(make_context(), R"({"name": "Alice", "email": "alice@example.com"})");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(first->succeeded());

  auto second = handler.execute(make_context(),
                                R"({"name": "Alice K.", "email": "alice@example.com", "phone": "555-2"})");
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(second->succeeded());

  auto count = repository->count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "re-importing the same email must update in place, not create a second row";

  auto found = repository->find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->name, "Alice K.");
}

}  // namespace
}  // namespace flowforge::handlers
