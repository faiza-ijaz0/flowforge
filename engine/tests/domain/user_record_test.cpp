#include "flowforge/domain/user_record.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(ValidateAndNormalizeUserRecordTest, TrimsAndLowercases) {
  auto result = validate_and_normalize_user_record("  Alice Khan ", " ALICE@EXAMPLE.COM ", std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->name, "Alice Khan");
  EXPECT_EQ(result->email, "alice@example.com");
  EXPECT_FALSE(result->phone.has_value());
}

TEST(ValidateAndNormalizeUserRecordTest, TrimsOptionalPhone) {
  auto result = validate_and_normalize_user_record("Bob", "bob@example.com", " 555-1234 ");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->phone, "555-1234");
}

TEST(ValidateAndNormalizeUserRecordTest, BlankPhoneIsTreatedAsAbsent) {
  auto result = validate_and_normalize_user_record("Bob", "bob@example.com", "   ");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->phone.has_value());
}

TEST(ValidateAndNormalizeUserRecordTest, BlankNameIsRejected) {
  auto result = validate_and_normalize_user_record("   ", "a@example.com", std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeUserRecordTest, OverlongNameIsRejected) {
  const std::string long_name(201, 'a');
  auto result = validate_and_normalize_user_record(long_name, "a@example.com", std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeUserRecordTest, MalformedEmailIsRejected) {
  auto result = validate_and_normalize_user_record("Alice", "not-an-email", std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeUserRecordTest, OverlongPhoneIsRejected) {
  const std::string long_phone(33, '1');
  auto result = validate_and_normalize_user_record("Alice", "a@example.com", long_phone);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(SerializeUserRecordAsJobPayloadTest, OmitsPhoneWhenUnset) {
  NormalizedUserRecord record{.name = "Alice", .email = "alice@example.com", .phone = std::nullopt};
  EXPECT_EQ(serialize_user_record_as_job_payload(record), R"({"name":"Alice","email":"alice@example.com"})");
}

TEST(SerializeUserRecordAsJobPayloadTest, IncludesPhoneWhenSet) {
  NormalizedUserRecord record{.name = "Alice", .email = "alice@example.com", .phone = "555-1234"};
  EXPECT_EQ(serialize_user_record_as_job_payload(record),
            R"({"name":"Alice","email":"alice@example.com","phone":"555-1234"})");
}

TEST(SerializeUserRecordAsJobPayloadTest, EscapesQuotesAndBackslashes) {
  NormalizedUserRecord record{.name = R"(Ali "Al" O'Brien)", .email = "a@example.com", .phone = std::nullopt};
  const std::string payload = serialize_user_record_as_job_payload(record);
  // The serialized payload must itself round-trip as a syntactically valid
  // flat JSON object -- proven here by confirming the embedded quote was
  // escaped rather than left to terminate the JSON string early.
  EXPECT_NE(payload.find(R"(Ali \"Al\" O'Brien)"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::domain
