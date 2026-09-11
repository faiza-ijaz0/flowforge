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

}  // namespace
}  // namespace flowforge::providers::test
