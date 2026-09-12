#include "flowforge/services/product_mapping.hpp"

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

TEST(ProductMappingTest, MapsExactLowercaseColumnNames) {
  auto mapped = map_structured_records_to_products({record_from({{"sku", "WID-1"},
                                                                 {"name", "Widget"},
                                                                 {"price", "19.99"},
                                                                 {"currency", "USD"},
                                                                 {"category", "Tools"},
                                                                 {"description", "A fine widget"},
                                                                 {"stock_quantity", "5"}})});
  EXPECT_EQ(mapped.total_records, 1u);
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].sku, "WID-1");
  EXPECT_EQ(mapped.valid_records[0].name, "Widget");
  EXPECT_DOUBLE_EQ(mapped.valid_records[0].price, 19.99);
  EXPECT_EQ(mapped.valid_records[0].stock_quantity, 5);
  EXPECT_TRUE(mapped.rejected_records.empty());
}

TEST(ProductMappingTest, MapsCaseInsensitiveOcrHeaderText) {
  // Realistic OCR'd header cell text -- capitalized, exactly as a human
  // would label a table column.
  auto mapped = map_structured_records_to_products(
      {record_from({{"SKU", "WID-2"}, {"Name", "Gadget"}, {"Price", "5.50"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].sku, "WID-2");
  EXPECT_EQ(mapped.valid_records[0].name, "Gadget");
}

TEST(ProductMappingTest, MapsCommonHeaderAliases) {
  auto mapped = map_structured_records_to_products({record_from(
      {{"Product Code", "WID-3"}, {"Title", "Doohickey"}, {"Unit Price", "1.00"}, {"Stock", "100"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].sku, "WID-3");
  EXPECT_EQ(mapped.valid_records[0].name, "Doohickey");
  EXPECT_EQ(mapped.valid_records[0].stock_quantity, 100);
}

TEST(ProductMappingTest, CurrencyCategoryDescriptionStockAreOptional) {
  auto mapped = map_structured_records_to_products(
      {record_from({{"sku", "WID-4"}, {"name", "Thing"}, {"price", "2"}})});
  ASSERT_EQ(mapped.valid_records.size(), 1u);
  EXPECT_EQ(mapped.valid_records[0].currency, "USD");
  EXPECT_FALSE(mapped.valid_records[0].category.has_value());
  EXPECT_EQ(mapped.valid_records[0].stock_quantity, 0);
}

TEST(ProductMappingTest, RejectsRecordWithNoSkuColumn) {
  auto mapped = map_structured_records_to_products({record_from({{"name", "Widget"}, {"price", "5"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("sku"), std::string::npos);
}

TEST(ProductMappingTest, RejectsRecordWithNoNameColumn) {
  auto mapped = map_structured_records_to_products({record_from({{"sku", "WID-1"}, {"price", "5"}})});
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("name"), std::string::npos);
}

TEST(ProductMappingTest, RejectsRecordWithNoPriceColumn) {
  auto mapped = map_structured_records_to_products({record_from({{"sku", "WID-1"}, {"name", "Widget"}})});
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_NE(mapped.rejected_records[0].reason.find("price"), std::string::npos);
}

TEST(ProductMappingTest, PassesThroughBusinessRuleValidationFailures) {
  auto mapped = map_structured_records_to_products(
      {record_from({{"sku", "WID-1"}, {"name", "Widget"}, {"price", "not-a-number"}})});
  EXPECT_TRUE(mapped.valid_records.empty());
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 1u);
}

TEST(ProductMappingTest, PreservesOriginalPositionAcrossAMixOfValidAndInvalidRecords) {
  auto mapped = map_structured_records_to_products({
      record_from({{"sku", "WID-1"}, {"name", "Widget"}, {"price", "5"}}),
      record_from({{"category", "no-sku-name-or-price"}}),
      record_from({{"sku", "WID-2"}, {"name", "Gadget"}, {"price", "10"}}),
  });
  EXPECT_EQ(mapped.total_records, 3u);
  EXPECT_EQ(mapped.valid_records.size(), 2u);
  ASSERT_EQ(mapped.rejected_records.size(), 1u);
  EXPECT_EQ(mapped.rejected_records[0].index, 2u);
}

TEST(ProductMappingTest, EmptyInputProducesEmptyOutput) {
  auto mapped = map_structured_records_to_products({});
  EXPECT_EQ(mapped.total_records, 0u);
  EXPECT_TRUE(mapped.valid_records.empty());
  EXPECT_TRUE(mapped.rejected_records.empty());
}

}  // namespace
}  // namespace flowforge::services
