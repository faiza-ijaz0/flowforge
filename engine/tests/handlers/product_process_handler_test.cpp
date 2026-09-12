#include "flowforge/handlers/product_process_handler.hpp"

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

TEST(ProductProcessHandlerTest, JobTypeIsProductProcess) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  EXPECT_EQ(handler.job_type(), "product.process");
}

TEST(ProductProcessHandlerTest, NormalizesAndPersistsAValidProduct) {
  auto repository = std::make_shared<persistence::InMemoryProductRepository>();
  ProductProcessHandler handler(repository);
  auto context = make_context();
  auto result = handler.execute(
      context, R"({"sku": " wid-1 ", "name": " Widget ", "price": "19.99", "currency": "usd"})");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_TRUE(result->succeeded());
  EXPECT_NE(result->output().find(R"("sku":"WID-1")"), std::string::npos);
  EXPECT_NE(result->output().find(R"("price":"19.99")"), std::string::npos);

  auto found = repository->find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->name, "Widget");
  EXPECT_EQ((*found)->currency, "USD");
  EXPECT_EQ((*found)->job_id, context.job_id());
}

TEST(ProductProcessHandlerTest, ReimportingTheSameSkuUpdatesTheExistingRow) {
  auto repository = std::make_shared<persistence::InMemoryProductRepository>();
  ProductProcessHandler handler(repository);

  auto first = handler.execute(
      make_context(), R"({"sku": "WID-1", "name": "Widget", "price": "10.00", "stock_quantity": "5"})");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(first->succeeded());

  auto second = handler.execute(
      make_context(), R"({"sku": "WID-1", "name": "Widget v2", "price": "12.50", "stock_quantity": "8"})");
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(second->succeeded());

  auto count = repository->count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "re-importing the same SKU must update in place, not create a duplicate row";

  auto found = repository->find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->name, "Widget v2");
  EXPECT_EQ((*found)->stock_quantity, 8);
}

TEST(ProductProcessHandlerTest, MissingSkuIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"name": "Widget", "price": "5"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, MissingNameIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"sku": "WID-1", "price": "5"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, MissingPriceIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"sku": "WID-1", "name": "Widget"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, InvalidPriceIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  auto result = handler.execute(context, R"({"sku": "WID-1", "name": "Widget", "price": "-5"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, NegativeStockQuantityIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  auto result =
      handler.execute(context, R"({"sku": "WID-1", "name": "Widget", "price": "5", "stock_quantity": "-3"})");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, OversizedPayloadIsRejected) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto context = make_context();
  const std::string oversized(std::size_t{20} * 1024, 'a');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ProductProcessHandlerTest, CancelledBeforeExecutionReturnsNonRetryableFailure) {
  ProductProcessHandler handler(std::make_shared<persistence::InMemoryProductRepository>());
  auto cancelled = std::make_shared<std::atomic<bool>>(true);
  auto context = make_context(cancelled);
  auto result = handler.execute(context, R"({"sku": "WID-1", "name": "Widget", "price": "5"})");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->succeeded());
  EXPECT_FALSE(result->retryable());
}

}  // namespace
}  // namespace flowforge::handlers
