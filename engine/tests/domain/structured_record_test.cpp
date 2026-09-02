#include "flowforge/domain/structured_record.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(StructuredRecordTest, FieldReturnsValueWhenPresent) {
  StructuredRecord record;
  record.fields.emplace("name", "Alice");
  record.fields.emplace("email", "alice@example.com");

  auto name = record.field("name");
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, "Alice");
}

TEST(StructuredRecordTest, FieldReturnsNulloptWhenAbsent) {
  StructuredRecord record;
  record.fields.emplace("name", "Alice");
  EXPECT_FALSE(record.field("phone").has_value());
}

TEST(StructuredRecordTest, FieldLookupAcceptsStringViewWithoutRequiringAllocation) {
  StructuredRecord record;
  record.fields.emplace("sku", "WID-1");
  // Heterogeneous lookup via std::less<> -- proves field() doesn't need an
  // implicit std::string conversion to find the entry.
  const std::string_view key = "sku";
  EXPECT_EQ(record.field(key), "WID-1");
}

TEST(ExtractionResultTest, DefaultsToEmptyWithNoRejections) {
  ExtractionResult result;
  EXPECT_EQ(result.total_records, 0u);
  EXPECT_TRUE(result.records.empty());
  EXPECT_TRUE(result.rejected_records.empty());
  EXPECT_EQ(result.rejected_record_count, 0u);
  EXPECT_FALSE(result.rejected_records_truncated);
}

}  // namespace
}  // namespace flowforge::domain
