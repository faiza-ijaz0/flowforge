#include "flowforge/services/user_mapping.hpp"

#include <gtest/gtest.h>

namespace flowforge::services {
namespace {

domain::StructuredRecord record_from(
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
  domain::StructuredRecord record;
  for (const auto& [key, value] : fields) {
    record.fields.emplace(key, value);
  }
  return record;
}

TEST(UserMappingTest, MapsExactLowercaseColumnNames) {
  auto mapped = map_structured_records_to_users(
      {record_from({{"name", "Alice"}, {"email", "alice@example.com"}, {"phone", "555-1234"}})});
  EXPECT_EQ(mapped.total_records, 1u);
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Alice");
  EXPECT_EQ(mapped.valid_records[0].email, "alice@example.com");
  ASSERT_TRUE(mapped.valid_records[0].phone.has_value());
  EXPECT_EQ(*mapped.valid_records[0].phone, "555-1234");
  EXPECT_TRUE(mapped.rejected_records.empty());
}

TEST(UserMappingTest, MapsCaseInsensitiveOcrHeaderText) {
  // Realistic OCR'd header cell text -- capitalized, exactly as a human
  // would label a table column, unlike a machine-typed CSV header.
  auto mapped = map_structured_records_to_users(
      {record_from({{"Name", "Ali"}, {"Email", "ali@example.com"}, {"Phone", "0300-123"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Ali");
  EXPECT_EQ(mapped.valid_records[0].email, "ali@example.com");
}

TEST(UserMappingTest, MapsCommonHeaderAliases) {
  auto mapped = map_structured_records_to_users(
      {record_from({{"Full Name", "Bob"}, {"Email Address", "bob@example.com"}, {"Mobile", "0311"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Bob");
  EXPECT_EQ(mapped.valid_records[0].email, "bob@example.com");
  ASSERT_TRUE(mapped.valid_records[0].phone.has_value());
  EXPECT_EQ(*mapped.valid_records[0].phone, "0311");
}

TEST(UserMappingTest, PhoneIsOptional) {
  auto mapped =
      map_structured_records_to_users({record_from({{"name", "Cara"}, {"email", "cara@example.com"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_FALSE(mapped.valid_records[0].phone.has_value());
}

TEST(UserMappingTest, RejectsRecordWithNoNameColumn) {
  auto mapped =
      map_structured_records_to_users({record_from({{"email", "x@example.com"}, {"sku", "WID-1"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("name"), std::string::npos);
}

TEST(UserMappingTest, RejectsRecordWithNoEmailColumn) {
  auto mapped = map_structured_records_to_users({record_from({{"name", "Dan"}})});
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("email"), std::string::npos);
}

TEST(UserMappingTest, PassesThroughBusinessRuleValidationFailures) {
  // A recognizable name/email column, but a malformed email -- the same
  // validation domain::validate_and_normalize_user_record already
  // enforces for CSV import must reject this too.
  auto mapped = map_structured_records_to_users({record_from({{"name", "Eve"}, {"email", "not-an-email"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
}

TEST(UserMappingTest, PreservesOriginalPositionAcrossAMixOfValidAndInvalidRecords) {
  auto mapped = map_structured_records_to_users({
      record_from({{"name", "Ali"}, {"email", "ali@example.com"}}),
      record_from({{"sku", "no-name-or-email"}}),
      record_from({{"name", "Sara"}, {"email", "sara@example.com"}}),
  });
  EXPECT_EQ(mapped.total_records, 3u);
  EXPECT_EQ(mapped.valid_records.size(), 2u);
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 2u);
}

TEST(UserMappingTest, EmptyInputProducesEmptyOutput) {
  auto mapped = map_structured_records_to_users({});
  EXPECT_EQ(mapped.total_records, 0u);
  EXPECT_TRUE(mapped.valid_records.empty());
  EXPECT_TRUE(mapped.rejected_records.empty());
}

TEST(UserMappingTest, RejectsDuplicateEmailWithinOneSubmissionKeepingTheFirst) {
  auto mapped = map_structured_records_to_users(
      {record_from({{"name", "Alice"}, {"email", "alice@example.com"}}),
       record_from({{"name", "Alice Again"}, {"email", " ALICE@example.com "}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Alice");
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 2u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("duplicate email"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::services
