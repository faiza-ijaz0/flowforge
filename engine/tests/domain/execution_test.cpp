#include "flowforge/domain/execution.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(ExecutionOutcomeTest, ToStringRoundTripsThroughFromString) {
  for (auto outcome : {ExecutionOutcome::Running, ExecutionOutcome::Succeeded, ExecutionOutcome::Failed,
                       ExecutionOutcome::TimedOut, ExecutionOutcome::Cancelled}) {
    auto parsed = execution_outcome_from_string(to_string(outcome));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, outcome);
  }
}

TEST(ExecutionOutcomeTest, FromStringRejectsUnknownValue) {
  EXPECT_FALSE(execution_outcome_from_string("not-a-real-outcome").has_value());
}

TEST(ExecutionTest, WorkerIdDefaultsToEmptyOptional) {
  Execution execution;
  EXPECT_FALSE(execution.worker_id.has_value());
}

TEST(ExecutionTest, WorkerIdCanBeSet) {
  Execution execution;
  execution.worker_id = infra::WorkerId::generate();
  EXPECT_TRUE(execution.worker_id.has_value());
}

}  // namespace
}  // namespace flowforge::domain
