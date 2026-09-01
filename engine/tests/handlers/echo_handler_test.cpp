#include "flowforge/handlers/echo_handler.hpp"

#include <gtest/gtest.h>

#include <string>

#include "flowforge/infra/logger.hpp"

namespace flowforge::handlers {
namespace {

engine::ExecutionContext make_context() {
  return {infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
          infra::make_logger(infra::LogLevel::Off, false)};
}

TEST(EchoHandlerTest, JobTypeIsEcho) {
  EchoHandler handler;
  EXPECT_EQ(handler.job_type(), "echo");
}

TEST(EchoHandlerTest, ReturnsPayloadUnchanged) {
  EchoHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, R"({"hello":"world"})");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), R"({"hello":"world"})");
}

TEST(EchoHandlerTest, EmptyPayloadSucceeds) {
  EchoHandler handler;
  auto context = make_context();
  auto result = handler.execute(context, "");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->output(), "");
}

TEST(EchoHandlerTest, OversizedPayloadIsRejected) {
  EchoHandler handler;
  auto context = make_context();
  const std::string oversized(std::size_t{300} * 1024, 'x');
  auto result = handler.execute(context, oversized);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

}  // namespace
}  // namespace flowforge::handlers
