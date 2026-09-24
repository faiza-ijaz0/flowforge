#include "flowforge/handlers/category_process_handler.hpp"

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

TEST(CategoryProcessHandlerTest, JobTypeIsCategoryProcess) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  EXPECT_EQ(handler.job_type(), "category.process");
}

TEST(CategoryProcessHandlerTest, NormalizesAndPersistsAValidCategory) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "  Electronics & Gadgets  "})");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_TRUE(result->succeeded());
  EXPECT_NE(result->output().find(R"("slug":"electronics-gadgets")"), std::string::npos);

  auto found = repository->find_by_slug("electronics-gadgets");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->name, "Electronics & Gadgets");
  EXPECT_EQ((*found)->job_id, context.job_id());
}

TEST(CategoryProcessHandlerTest, ReimportingTheSameSlugUpdatesTheExistingRow) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);

  auto first = handler.execute(make_context(), R"({"name": "Electronics", "description": "v1"})");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(first->succeeded());

  auto second = handler.execute(make_context(), R"({"name": "Electronics", "description": "v2"})");
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(second->succeeded());

  auto count = repository->count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "re-importing the same slug must update in place, not create a duplicate row";

  auto found = repository->find_by_slug("electronics");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->description, "v2");
}

TEST(CategoryProcessHandlerTest, MissingNameIsRejected) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"description": "no name"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CategoryProcessHandlerTest, OversizedPayloadIsRejected) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto context = make_context();
  const std::string oversized(std::size_t{20} * 1024, 'a');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CategoryProcessHandlerTest, CancelledBeforeExecutionReturnsNonRetryableFailure) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto cancelled = std::make_shared<std::atomic<bool>>(true);
  auto context = make_context(cancelled);
  auto result = handler.execute(context, R"({"name": "Electronics"})");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->succeeded());
  EXPECT_FALSE(result->retryable());
}

// --- Parent semantics -------------------------------------------------

TEST(CategoryProcessHandlerTest, ValidParentIsAccepted) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);

  auto parent = handler.execute(make_context(), R"({"name": "Electronics"})");
  ASSERT_TRUE(parent.has_value() && parent->succeeded());

  auto child = handler.execute(make_context(), R"({"name": "Laptops", "parent_slug": "electronics"})");
  ASSERT_TRUE(child.has_value()) << child.error().message();
  EXPECT_TRUE(child->succeeded());

  auto found = repository->find_by_slug("laptops");
  ASSERT_TRUE(found.has_value() && found->has_value());
  ASSERT_TRUE((*found)->parent_slug.has_value());
  EXPECT_EQ(*(*found)->parent_slug, "electronics");
}

TEST(CategoryProcessHandlerTest, MissingParentIsRejectedNonRetryably) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Laptops", "parent_slug": "does-not-exist"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
  EXPECT_NE(result.error().message().find("does not exist"), std::string::npos);
}

TEST(CategoryProcessHandlerTest, MissingParentFromTheSameSubmissionIsRetryable) {
  // Set by InputProcessingService::confirm() when the parent is another
  // record of the same submission, whose job may not have committed yet.
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto result = handler.execute(
      make_context(), R"({"name": "Laptops", "parent_slug": "electronics", "parent_in_submission": "true"})");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_FALSE(result->succeeded());
  EXPECT_TRUE(result->retryable());
}

TEST(CategoryProcessHandlerTest, SameSubmissionParentSucceedsOnceThePersistedParentExists) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);
  const auto child_payload =
      R"({"name": "Laptops", "parent_slug": "electronics", "parent_in_submission": "true"})";

  auto first_attempt = handler.execute(make_context(), child_payload);
  ASSERT_TRUE(first_attempt.has_value());
  ASSERT_TRUE(first_attempt->retryable());

  ASSERT_TRUE(handler.execute(make_context(), R"({"name": "Electronics"})")->succeeded());
  auto retry = handler.execute(make_context(), child_payload);
  ASSERT_TRUE(retry.has_value()) << retry.error().message();
  EXPECT_TRUE(retry->succeeded());
}

TEST(CategoryProcessHandlerTest, SelfParentIsRejected) {
  CategoryProcessHandler handler(std::make_shared<persistence::InMemoryCategoryRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Electronics", "parent_slug": "Electronics"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CategoryProcessHandlerTest, DeeperCyclicParentChainIsRejected) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);

  // A (no parent), then B with parent A -- both accepted.
  auto a = handler.execute(make_context(), R"({"name": "A"})");
  ASSERT_TRUE(a.has_value() && a->succeeded());
  auto b = handler.execute(make_context(), R"({"name": "B", "parent_slug": "a"})");
  ASSERT_TRUE(b.has_value() && b->succeeded());

  // Re-importing A with parent B would create a cycle A -> B -> A.
  auto cyclic = handler.execute(make_context(), R"({"name": "A", "parent_slug": "b"})");
  ASSERT_FALSE(cyclic.has_value());
  EXPECT_EQ(cyclic.error().code(), ErrorCode::Validation);
  EXPECT_NE(cyclic.error().message().find("cyclic"), std::string::npos);

  // The original, valid A row must be untouched by the rejected attempt.
  auto found = repository->find_by_slug("a");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_FALSE((*found)->parent_slug.has_value());
}

TEST(CategoryProcessHandlerTest, MultiLevelParentChainIsAccepted) {
  auto repository = std::make_shared<persistence::InMemoryCategoryRepository>();
  CategoryProcessHandler handler(repository);

  ASSERT_TRUE(handler.execute(make_context(), R"({"name": "Electronics"})")->succeeded());
  ASSERT_TRUE(
      handler.execute(make_context(), R"({"name": "Computers", "parent_slug": "electronics"})")->succeeded());
  auto grandchild = handler.execute(make_context(), R"({"name": "Laptops", "parent_slug": "computers"})");
  ASSERT_TRUE(grandchild.has_value()) << grandchild.error().message();
  EXPECT_TRUE(grandchild->succeeded());
}

}  // namespace
}  // namespace flowforge::handlers
