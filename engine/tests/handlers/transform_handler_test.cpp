#include "flowforge/handlers/transform_handler.hpp"

#include <gtest/gtest.h>

#include <string>

#include "flowforge/infra/logger.hpp"

namespace flowforge::handlers {
namespace {

engine::ExecutionContext make_context() {
  return {infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
          infra::make_logger(infra::LogLevel::Off, false)};
}

TEST(TransformHandlerTest, JobTypeIsTransform) {
  TransformHandler handler;
  EXPECT_EQ(handler.job_type(), "transform");
}

TEST(TransformHandlerTest, UppercasesPayload) {
  TransformHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, "hello world");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), "HELLO WORLD");
}

TEST(TransformHandlerTest, LeavesAlreadyUppercaseUnchanged) {
  TransformHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, "ALREADY UPPER");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->output(), "ALREADY UPPER");
}

TEST(TransformHandlerTest, OversizedPayloadIsRejected) {
  TransformHandler handler;
  auto context = make_context();
  const std::string oversized(std::size_t{300} * 1024, 'a');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

}  // namespace
}  // namespace flowforge::handlers
