#include "flowforge/domain/category_record.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

// --- slugify() ------------------------------------------------------------

TEST(SlugifyTest, LowercasesAndCollapsesPunctuationAndWhitespace) {
  EXPECT_EQ(slugify("  Electronics & Gadgets  "), "electronics-gadgets");
}

TEST(SlugifyTest, CollapsesRepeatedSeparators) {
  EXPECT_EQ(slugify("Home   ---  Kitchen"), "home-kitchen");
}

TEST(SlugifyTest, IsIdempotentOnAnAlreadyValidSlug) {
  EXPECT_EQ(slugify("already-a-slug"), "already-a-slug");
}

TEST(SlugifyTest, NeverProducesLeadingOrTrailingHyphens) {
  EXPECT_EQ(slugify("***Sale Items***"), "sale-items");
}

TEST(SlugifyTest, ReturnsEmptyForInputWithNoLettersOrDigits) {
  EXPECT_EQ(slugify("---"), "");
  EXPECT_EQ(slugify("###"), "");
  EXPECT_EQ(slugify("   "), "");
}

// --- validate_and_normalize_category_record() ------------------------------

TEST(ValidateAndNormalizeCategoryRecordTest, TrimsName) {
  auto result =
      validate_and_normalize_category_record("  Electronics  ", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->name, "Electronics");
}

TEST(ValidateAndNormalizeCategoryRecordTest, DerivesSlugFromNameWhenAbsent) {
  auto result = validate_and_normalize_category_record("Electronics & Gadgets", std::nullopt, std::nullopt,
                                                       std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->slug, "electronics-gadgets");
}

TEST(ValidateAndNormalizeCategoryRecordTest, DerivesSlugFromNameWhenSlugColumnIsBlank) {
  auto result = validate_and_normalize_category_record("Electronics", "   ", std::nullopt, std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->slug, "electronics");
}

TEST(ValidateAndNormalizeCategoryRecordTest, NormalizesAnExplicitlyProvidedSlug) {
  auto result = validate_and_normalize_category_record("Electronics", "  Home Electronics!  ", std::nullopt,
                                                       std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->slug, "home-electronics");
}

TEST(ValidateAndNormalizeCategoryRecordTest, BlankNameIsRejected) {
  auto result = validate_and_normalize_category_record("   ", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeCategoryRecordTest, OverlongNameIsRejected) {
  const std::string long_name(201, 'a');
  auto result = validate_and_normalize_category_record(long_name, std::nullopt, std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeCategoryRecordTest, NameWithOnlyPunctuationCannotDeriveASlug) {
  auto result = validate_and_normalize_category_record("---", std::nullopt, std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeCategoryRecordTest, ExplicitSlugWithOnlyPunctuationIsRejected) {
  auto result = validate_and_normalize_category_record("Electronics", "###", std::nullopt, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeCategoryRecordTest, KeepsOptionalDescription) {
  auto result = validate_and_normalize_category_record("Electronics", std::nullopt, " Gadgets and gizmos. ",
                                                       std::nullopt);
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->description.has_value());
  EXPECT_EQ(*result->description, "Gadgets and gizmos.");
}

TEST(ValidateAndNormalizeCategoryRecordTest, BlankDescriptionIsTreatedAsAbsent) {
  auto result = validate_and_normalize_category_record("Electronics", std::nullopt, "   ", std::nullopt);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->description.has_value());
}

TEST(ValidateAndNormalizeCategoryRecordTest, OverlongDescriptionIsRejected) {
  const std::string long_description(2001, 'a');
  auto result =
      validate_and_normalize_category_record("Electronics", std::nullopt, long_description, std::nullopt);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ValidateAndNormalizeCategoryRecordTest, NormalizesParentSlug) {
  auto result =
      validate_and_normalize_category_record("Laptops", std::nullopt, std::nullopt, "  Electronics  ");
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->parent_slug.has_value());
  EXPECT_EQ(*result->parent_slug, "electronics");
}

TEST(ValidateAndNormalizeCategoryRecordTest, BlankParentSlugIsTreatedAsAbsent) {
  auto result = validate_and_normalize_category_record("Laptops", std::nullopt, std::nullopt, "   ");
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->parent_slug.has_value());
}

TEST(ValidateAndNormalizeCategoryRecordTest, SelfReferencingParentSlugIsRejected) {
  auto result =
      validate_and_normalize_category_record("Electronics", "electronics", std::nullopt, "Electronics");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
  EXPECT_NE(result.error().message().find("own parent"), std::string::npos);
}

TEST(ValidateAndNormalizeCategoryRecordTest, SelfReferencingParentSlugIsRejectedEvenWhenSlugIsDerived) {
  // "Electronics" derives to slug "electronics"; parent_slug normalizes
  // to the same value via a different input string -- still a
  // self-reference once both are slugified.
  auto result =
      validate_and_normalize_category_record("Electronics", std::nullopt, std::nullopt, "ELECTRONICS!!");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

// --- serialize_category_record_as_job_payload() ----------------------------

TEST(SerializeCategoryRecordAsJobPayloadTest, OmitsDescriptionAndParentSlugWhenUnset) {
  NormalizedCategoryRecord record{
      .name = "Electronics", .slug = "electronics", .description = std::nullopt, .parent_slug = std::nullopt};
  EXPECT_EQ(serialize_category_record_as_job_payload(record),
            R"({"name":"Electronics","slug":"electronics"})");
}

TEST(SerializeCategoryRecordAsJobPayloadTest, IncludesDescriptionAndParentSlugWhenSet) {
  NormalizedCategoryRecord record{.name = "Laptops",
                                  .slug = "laptops",
                                  .description = "Portable computers.",
                                  .parent_slug = "electronics"};
  const std::string payload = serialize_category_record_as_job_payload(record);
  EXPECT_NE(payload.find(R"("description":"Portable computers.")"), std::string::npos);
  EXPECT_NE(payload.find(R"("parent_slug":"electronics")"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::domain
