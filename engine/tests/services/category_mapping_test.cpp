#include "flowforge/services/category_mapping.hpp"

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

TEST(CategoryMappingTest, MapsExactLowercaseColumnNames) {
  auto mapped = map_structured_records_to_categories({record_from(
      {{"name", "Electronics"}, {"slug", "electronics"}, {"description", "Gadgets"}, {"parent_slug", ""}})});
  EXPECT_EQ(mapped.total_records, 1u);
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Electronics");
  EXPECT_EQ(mapped.valid_records[0].slug, "electronics");
  EXPECT_TRUE(mapped.rejected_records.empty());
}

TEST(CategoryMappingTest, MapsCaseInsensitiveOcrHeaderText) {
  auto mapped = map_structured_records_to_categories({record_from({{"Name", "Electronics"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Electronics");
}

TEST(CategoryMappingTest, MapsCommonHeaderAliases) {
  auto mapped = map_structured_records_to_categories(
      {record_from({{"Category Name", "Home & Kitchen"}, {"Parent Category", "Home"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].name, "Home & Kitchen");
  EXPECT_EQ(mapped.valid_records[0].slug, "home-kitchen");
  ASSERT_TRUE(mapped.valid_records[0].parent_slug.has_value());
  EXPECT_EQ(*mapped.valid_records[0].parent_slug, "home");
}

TEST(CategoryMappingTest, SlugDescriptionParentSlugAreOptional) {
  auto mapped = map_structured_records_to_categories({record_from({{"name", "Electronics"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].slug, "electronics");
  EXPECT_FALSE(mapped.valid_records[0].description.has_value());
  EXPECT_FALSE(mapped.valid_records[0].parent_slug.has_value());
}

TEST(CategoryMappingTest, RejectsRecordWithNoNameColumn) {
  auto mapped = map_structured_records_to_categories({record_from({{"description", "no name here"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("name"), std::string::npos);
}

TEST(CategoryMappingTest, PassesThroughBusinessRuleValidationFailures) {
  auto mapped = map_structured_records_to_categories({record_from({{"name", "###"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
}

TEST(CategoryMappingTest, PreservesOriginalPositionAcrossAMixOfValidAndInvalidRecords) {
  auto mapped = map_structured_records_to_categories({
      record_from({{"name", "Electronics"}}),
      record_from({{"description", "no-name-here"}}),
      record_from({{"name", "Home"}}),
  });
  EXPECT_EQ(mapped.total_records, 3u);
  EXPECT_EQ(mapped.valid_records.size(), 2u);
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 2u);
}

TEST(CategoryMappingTest, EmptyInputProducesEmptyOutput) {
  auto mapped = map_structured_records_to_categories({});
  EXPECT_EQ(mapped.total_records, 0u);
  EXPECT_TRUE(mapped.valid_records.empty());
  EXPECT_TRUE(mapped.rejected_records.empty());
}

}  // namespace
}  // namespace flowforge::services
