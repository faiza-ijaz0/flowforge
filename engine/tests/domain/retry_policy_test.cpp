#include "flowforge/domain/retry_policy.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(RetryPolicyTest, ZeroAttemptsMeansNoDelay) {
  RetryPolicy policy;
  EXPECT_EQ(policy.compute_backoff(0), std::chrono::milliseconds{0});
}

TEST(RetryPolicyTest, FirstRetryUsesInitialBackoff) {
  RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{1000};
  EXPECT_EQ(policy.compute_backoff(1), std::chrono::milliseconds{1000});
}

TEST(RetryPolicyTest, BackoffGrowsExponentially) {
  RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{1000};
  policy.backoff_multiplier = 2.0;
  policy.max_backoff = std::chrono::milliseconds{1'000'000};

  EXPECT_EQ(policy.compute_backoff(1), std::chrono::milliseconds{1000});
  EXPECT_EQ(policy.compute_backoff(2), std::chrono::milliseconds{2000});
  EXPECT_EQ(policy.compute_backoff(3), std::chrono::milliseconds{4000});
  EXPECT_EQ(policy.compute_backoff(4), std::chrono::milliseconds{8000});
}

TEST(RetryPolicyTest, BackoffIsCappedAtMaxBackoff) {
  RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{1000};
  policy.backoff_multiplier = 2.0;
  policy.max_backoff = std::chrono::milliseconds{5000};

  EXPECT_LE(policy.compute_backoff(10), policy.max_backoff);
  EXPECT_EQ(policy.compute_backoff(10), policy.max_backoff);
}

TEST(RetryPolicyTest, ExhaustedReflectsMaxAttempts) {
  RetryPolicy policy;
  policy.max_attempts = 3;
  EXPECT_FALSE(policy.exhausted(0));
  EXPECT_FALSE(policy.exhausted(2));
  EXPECT_TRUE(policy.exhausted(3));
  EXPECT_TRUE(policy.exhausted(4));
}

}  // namespace
}  // namespace flowforge::domain
