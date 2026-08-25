#include "flowforge/infra/clock.hpp"

#include <gtest/gtest.h>

namespace flowforge::infra {
namespace {

TEST(ManualClockTest, StartsAtGivenTime) {
  const TimePoint start = std::chrono::system_clock::now();
  ManualClock clock(start);
  EXPECT_EQ(clock.now(), start);
}

TEST(ManualClockTest, AdvanceMovesTimeForward) {
  const TimePoint start = std::chrono::system_clock::now();
  ManualClock clock(start);
  clock.advance(std::chrono::seconds(30));
  EXPECT_EQ(clock.now(), start + std::chrono::seconds(30));
}

TEST(ManualClockTest, SetOverridesCurrentTime) {
  ManualClock clock;
  const TimePoint target = std::chrono::system_clock::now() + std::chrono::hours(1);
  clock.set(target);
  EXPECT_EQ(clock.now(), target);
}

TEST(SystemClockTest, NowIsMonotonicallyNonDecreasing) {
  SystemClock clock;
  const TimePoint first = clock.now();
  const TimePoint second = clock.now();
  EXPECT_LE(first, second);
}

}  // namespace
}  // namespace flowforge::infra
