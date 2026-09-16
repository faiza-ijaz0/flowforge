#include "flowforge/providers/tesseract_ocr_provider.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "flowforge/extractors/image_extractor.hpp"
#include "ocr_test_support.hpp"

namespace flowforge::providers::test {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// Real, deterministic, offline OCR against `user_table.png` -- a
/// four-line "Name | Email | Phone" table this phase generated and
/// committed as a fixture (see fixture generation notes in
/// docs/architecture/input-processing.md, "Testing: real vs. fake OCR").
/// No mock, no network service: this genuinely shells out to a real
/// Tesseract binary. Skipped (never silently passed) when no such binary
/// is available -- see OcrIntegrationTest.
TEST_F(OcrIntegrationTest, RecognizesRealTextFromTheFixtureImage) {
  TesseractCliOcrProvider provider(tesseract_path_);
  const std::string image_bytes = read_file(fixture_path("user_table.png"));
  ASSERT_FALSE(image_bytes.empty()) << "fixture image failed to load";

  auto result = provider.recognize(image_bytes);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_FALSE(result->words.empty());

  bool found_name = false;
  bool found_email = false;
  for (const auto& word : result->words) {
    if (word.text == "Name") {
      found_name = true;
    }
    if (word.text == "ali@example.com") {
      found_email = true;
    }
  }
  EXPECT_TRUE(found_name);
  EXPECT_TRUE(found_email);
}

TEST_F(OcrIntegrationTest, RejectsCorruptImageBytesWithoutInvokingTesseract) {
  TesseractCliOcrProvider provider(tesseract_path_);
  auto result = provider.recognize("not a real image");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

/// End-to-end: real image bytes -> real OCR -> ImageExtractor's table
/// reconstruction -> generic StructuredRecords, with no user-specific
/// mapping involved yet (see input_processing_service_test.cpp and
/// user_mapping_test.cpp for that layer).
TEST_F(OcrIntegrationTest, ImageExtractorProducesRealStructuredRecordsFromTheFixture) {
  auto provider = std::make_shared<TesseractCliOcrProvider>(tesseract_path_);
  extractors::ImageExtractor extractor(provider);
  const std::string image_bytes = read_file(fixture_path("user_table.png"));

  auto result = extractor.extract({.source_type = domain::InputSourceType::Image, .content = image_bytes});
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 3u);
  ASSERT_EQ(result->records.size(), 3u);
  EXPECT_EQ(result->records[0].field("Email"), "ali@example.com");
  EXPECT_EQ(result->records[1].field("Email"), "sara@example.com");
  EXPECT_EQ(result->records[2].field("Email"), "john@example.com");
}

/// Phase 3D-2: real OCR + real table reconstruction against a 100-data-row
/// fixture (`user_table_bulk_100.png` -- see docs/architecture/
/// input-processing.md, "100+ record fixture"). Proves the generic,
/// domain-agnostic extraction layer doesn't silently lose or merge rows
/// at scale: `total_records` must be exactly 100 (every row the header
/// implies is accounted for, whether it ends up a valid record or a
/// reported structural rejection -- never neither), and every record
/// still has the two generic columns (`Name`/`Email`) this extractor
/// knows nothing more specific about than that.
TEST_F(OcrIntegrationTest, ReconstructsAllHundredRowsFromTheBulkFixtureWithoutLosingAny) {
  auto provider = std::make_shared<TesseractCliOcrProvider>(tesseract_path_);
  extractors::ImageExtractor extractor(provider);
  const std::string image_bytes = read_file(fixture_path("user_table_bulk_100.png"));
  ASSERT_FALSE(image_bytes.empty()) << "bulk fixture image failed to load";

  auto result = extractor.extract({.source_type = domain::InputSourceType::Image, .content = image_bytes});
  ASSERT_TRUE(result.has_value()) << result.error().message();

  EXPECT_EQ(result->total_records, 100u);
  EXPECT_EQ(result->records.size() + result->rejected_record_count, 100u);
  // The large majority of 100 real, OCR'd rows must structurally parse
  // into two-column records -- a near-total loss would indicate a real
  // table-reconstruction regression, not ordinary OCR text-recognition
  // noise (see ImageExtractorTest for the deterministic, non-OCR-
  // dependent coverage of the reconstruction algorithm itself).
  EXPECT_GE(result->records.size(), 90u);
  for (const auto& record : result->records) {
    EXPECT_TRUE(record.field("Name").has_value());
    EXPECT_TRUE(record.field("Email").has_value());
  }
  ASSERT_TRUE(result->average_confidence.has_value());
  EXPECT_GT(*result->average_confidence, 0.0);
}

/// Phase 3E: real OCR + real table reconstruction against a 100-data-row
/// **product** table fixture (`products_bulk_100.png` -- see
/// docs/architecture/product-processing.md, "100+ product fixture").
/// `ImageExtractor` itself has no product-specific knowledge (see its own
/// class comment) -- this proves the exact same generic extraction path
/// the Users fixture exercises above handles a completely different
/// column set (SKU/Name/Price) equally well, without any change to
/// `ImageExtractor`.
TEST_F(OcrIntegrationTest, ReconstructsAllHundredRowsFromTheBulkProductFixtureWithoutLosingAny) {
  auto provider = std::make_shared<TesseractCliOcrProvider>(tesseract_path_);
  extractors::ImageExtractor extractor(provider);
  const std::string image_bytes = read_file(fixture_path("products_bulk_100.png"));
  ASSERT_FALSE(image_bytes.empty()) << "bulk product fixture image failed to load";

  auto result = extractor.extract({.source_type = domain::InputSourceType::Image, .content = image_bytes});
  ASSERT_TRUE(result.has_value()) << result.error().message();

  EXPECT_EQ(result->total_records, 100u);
  EXPECT_EQ(result->records.size() + result->rejected_record_count, 100u);
  EXPECT_GE(result->records.size(), 90u);
  for (const auto& record : result->records) {
    EXPECT_TRUE(record.field("SKU").has_value());
    EXPECT_TRUE(record.field("Name").has_value());
    EXPECT_TRUE(record.field("Price").has_value());
  }
}

TEST_F(OcrIntegrationTest, ReconstructsAllHundredRowsFromTheBulkCategoryFixtureWithoutLosingAny) {
  auto provider = std::make_shared<TesseractCliOcrProvider>(tesseract_path_);
  extractors::ImageExtractor extractor(provider);
  const std::string image_bytes = read_file(fixture_path("categories_bulk_100.png"));
  ASSERT_FALSE(image_bytes.empty()) << "bulk category fixture image failed to load";

  auto result = extractor.extract({.source_type = domain::InputSourceType::Image, .content = image_bytes});
  ASSERT_TRUE(result.has_value()) << result.error().message();

  EXPECT_EQ(result->total_records, 100u);
  EXPECT_EQ(result->records.size() + result->rejected_record_count, 100u);
  EXPECT_GE(result->records.size(), 90u);
  for (const auto& record : result->records) {
    EXPECT_TRUE(record.field("Name").has_value());
    EXPECT_TRUE(record.field("Slug").has_value());
  }
}

}  // namespace
}  // namespace flowforge::providers::test
