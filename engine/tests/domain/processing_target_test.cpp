#include "flowforge/domain/processing_target.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(ProcessingTargetStringTest, RoundTripsThroughAllValues) {
  for (auto target : {ProcessingTarget::Users, ProcessingTarget::Products, ProcessingTarget::Categories}) {
    auto parsed = processing_target_from_string(to_string(target));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, target);
  }
}

TEST(ProcessingTargetStringTest, KnownStringValues) {
  EXPECT_EQ(to_string(ProcessingTarget::Users), "users");
  EXPECT_EQ(to_string(ProcessingTarget::Products), "products");
  EXPECT_EQ(to_string(ProcessingTarget::Categories), "categories");
}

TEST(ProcessingTargetStringTest, UnrecognizedStringReturnsNullopt) {
  EXPECT_FALSE(processing_target_from_string("orders").has_value());
  EXPECT_FALSE(processing_target_from_string("").has_value());
}

TEST(JobTypeForProcessingTargetTest, MapsEveryTargetToAJobType) {
  // "Future mappings should be possible" -- every target has a job_type
  // string even though only "user.process" has a registered handler this
  // phase (see docs/architecture/input-processing.md).
  EXPECT_EQ(job_type_for_processing_target(ProcessingTarget::Users), "user.process");
  EXPECT_EQ(job_type_for_processing_target(ProcessingTarget::Products), "product.process");
  EXPECT_EQ(job_type_for_processing_target(ProcessingTarget::Categories), "category.process");
}

}  // namespace
}  // namespace flowforge::domain
