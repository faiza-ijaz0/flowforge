#include "flowforge/domain/product_record.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(ValidateAndNormalizeProductRecordTest, TrimsAndUppercasesSku) {
  auto result = validate_and_normalize_product_record("  wid-1 ", " Widget ", "19.99", std::nullopt,
                                                      std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->sku, "WID-1");
  EXPECT_EQ(result->name, "Widget");
  EXPECT_DOUBLE_EQ(result->price, 19.99);
}

TEST(ValidateAndNormalizeProductRecordTest, DefaultsCurrencyToUsdWhenAbsent) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->currency, "USD");
}

TEST(ValidateAndNormalizeProductRecordTest, DefaultsStockQuantityToZeroWhenAbsent) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->stock_quantity, 0);
}

TEST(ValidateAndNormalizeProductRecordTest, UppercasesExplicitCurrency) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", "eur", std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->currency, "EUR");
}

TEST(ValidateAndNormalizeProductRecordTest, KeepsOptionalCategoryAndDescription) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, " Tools ",
                                                      " A fine widget. ", "42");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->category.has_value());
  EXPECT_EQ(*result->category, "Tools");
  ASSERT_TRUE(result->description.has_value());
  EXPECT_EQ(*result->description, "A fine widget.");
  EXPECT_EQ(result->stock_quantity, 42);
}

TEST(ValidateAndNormalizeProductRecordTest, BlankCategoryIsTreatedAsAbsent) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, "   ",
                                                      std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->category.has_value());
}

TEST(ValidateAndNormalizeProductRecordTest, BlankSkuIsRejected) {
  auto result = validate_and_normalize_product_record("   ", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, SkuWithInvalidCharactersIsRejected) {
  auto result = validate_and_normalize_product_record("WID 1!", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, OverlongSkuIsRejected) {
  const std::string long_sku(65, 'A');
  auto result = validate_and_normalize_product_record(long_sku, "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, BlankNameIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "   ", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, BlankPriceIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "  ", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, NonNumericPriceIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "free", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, NegativePriceIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "-5.00", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, PriceWithTooManyDecimalPlacesIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "19.999", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, ExcessivePriceIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "99999999.00", std::nullopt,
                                                      std::nullopt, std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, ZeroPriceIsAccepted) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "0", std::nullopt, std::nullopt,
                                                      std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->price, 0.0);
}

TEST(ValidateAndNormalizeProductRecordTest, InvalidCurrencyCodeIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", "US", std::nullopt, std::nullopt,
                                                      std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, NegativeStockQuantityIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, "-1");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, NonIntegerStockQuantityIsRejected) {
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, std::nullopt,
                                                      std::nullopt, "3.5");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeProductRecordTest, OverlongDescriptionIsRejected) {
  const std::string long_description(2001, 'a');
  auto result = validate_and_normalize_product_record("SKU1", "Widget", "5", std::nullopt, std::nullopt,
                                                      long_description, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(SerializeProductRecordAsJobPayloadTest, OmitsCategoryAndDescriptionWhenUnset) {
  NormalizedProductRecord record{.sku = "SKU1",
                                 .name = "Widget",
                                 .price = 19.99,
                                 .currency = "USD",
                                 .category = std::nullopt,
                                 .description = std::nullopt,
                                 .stock_quantity = 0};
  EXPECT_EQ(serialize_product_record_as_job_payload(record),
            R"({"sku":"SKU1","name":"Widget","price":"19.99","currency":"USD","stock_quantity":"0"})");
}

TEST(SerializeProductRecordAsJobPayloadTest, IncludesCategoryAndDescriptionWhenSet) {
  NormalizedProductRecord record{.sku = "SKU1",
                                 .name = "Widget",
                                 .price = 19.99,
                                 .currency = "USD",
                                 .category = "Tools",
                                 .description = "A fine widget.",
                                 .stock_quantity = 7};
  const std::string payload = serialize_product_record_as_job_payload(record);
  EXPECT_NE(payload.find(R"("category":"Tools")"), std::string::npos);
  EXPECT_NE(payload.find(R"("description":"A fine widget.")"), std::string::npos);
  EXPECT_NE(payload.find(R"("stock_quantity":"7")"), std::string::npos);
}

TEST(SerializeProductRecordAsJobPayloadTest, FormatsPriceWithTwoDecimalPlaces) {
  NormalizedProductRecord record{.sku = "SKU1",
                                 .name = "Widget",
                                 .price = 5.0,
                                 .currency = "USD",
                                 .category = std::nullopt,
                                 .description = std::nullopt,
                                 .stock_quantity = 0};
  EXPECT_NE(serialize_product_record_as_job_payload(record).find(R"("price":"5.00")"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::domain
