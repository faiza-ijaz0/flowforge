#include "flowforge/extractors/image_extractor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace flowforge::extractors {
namespace {

// No embedded NUL byte in either literal, so the implicit `const char*`
// -> `std::string` conversion captures the whole thing correctly.
std::string png_signature_bytes() {
  return std::string("\x89PNG\r\n\x1a\n") + "fake-pixel-data-not-real-png";
}

engine::OcrWord word(std::string text, int left, int top, int width, int height, double conf, int block,
                     int par, int line) {
  return engine::OcrWord{.text = std::move(text),
                         .left = left,
                         .top = top,
                         .width = width,
                         .height = height,
                         .confidence = conf,
                         .block_num = block,
                         .par_num = par,
                         .line_num = line};
}

/// Deterministic `engine::IOcrProvider` double: returns a fixed,
/// caller-configured `Result<engine::OcrResult>` instead of running real
/// OCR, so `ImageExtractor`'s table-reconstruction logic can be tested
/// precisely and quickly, independent of whether Tesseract is installed
/// on the machine running these tests. See
/// engine/tests/providers/tesseract_ocr_provider_test.cpp for the
/// separate, real-OCR integration tests.
class FakeOcrProvider final : public engine::IOcrProvider {
 public:
  explicit FakeOcrProvider(Result<engine::OcrResult> result) : result_(std::move(result)) {}

  [[nodiscard]] Result<engine::OcrResult> recognize(std::string_view) const override { return result_; }

 private:
  Result<engine::OcrResult> result_;
};

std::shared_ptr<engine::IOcrProvider> fake_provider(std::vector<engine::OcrWord> words) {
  return std::make_shared<FakeOcrProvider>(engine::OcrResult{.words = std::move(words)});
}

/// A clean, two-column "Name | Email" table: one header line plus two
/// data lines, each split into exactly two columns by a wide horizontal
/// gap -- mirrors this phase's real fixture image's layout.
std::vector<engine::OcrWord> simple_table_words() {
  return {
      word("Name", 20, 20, 60, 20, 96.0, 1, 1, 1),
      word("Email", 200, 20, 70, 20, 95.0, 1, 1, 1),
      word("Ali", 20, 70, 40, 20, 96.0, 1, 2, 1),
      word("ali@example.com", 200, 70, 240, 20, 92.0, 1, 2, 1),
      word("Sara", 20, 120, 45, 20, 97.0, 1, 2, 2),
      word("sara@example.com", 200, 120, 250, 20, 91.0, 1, 2, 2),
  };
}

TEST(ImageExtractorTest, SourceTypeIsImage) {
  ImageExtractor extractor(fake_provider({}));
  EXPECT_EQ(extractor.source_type(), domain::InputSourceType::Image);
}

TEST(ImageExtractorTest, RejectsPayloadWithWrongSourceType) {
  ImageExtractor extractor(fake_provider({}));
  auto result = extractor.extract({.source_type = domain::InputSourceType::Text, .content = "hello"});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ImageExtractorTest, AcceptsScreenshotSourceTypeThroughTheSameExtractor) {
  ImageExtractor extractor(fake_provider(simple_table_words()));
  auto result = extractor.extract(
      {.source_type = domain::InputSourceType::Screenshot, .content = png_signature_bytes()});
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->records.size(), 2u);
}

TEST(ImageExtractorTest, RejectsEmptyImagePayload) {
  ImageExtractor extractor(fake_provider({}));
  auto result = extractor.extract({.source_type = domain::InputSourceType::Image, .content = ""});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ImageExtractorTest, RejectsMalformedImageBytesBeforeCallingTheOcrProvider) {
  ImageExtractor extractor(fake_provider(simple_table_words()));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = "not an image at all"});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message().find("unsupported"), std::string::npos);
}

TEST(ImageExtractorTest, PropagatesOcrProviderFailure) {
  ImageExtractor extractor(std::make_shared<FakeOcrProvider>(
      std::unexpected(make_error(ErrorCode::Infrastructure, "OCR unavailable"))));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Infrastructure);
}

TEST(ImageExtractorTest, NoWordsDetectedReturnsNoTextDetectedError) {
  ImageExtractor extractor(fake_provider({}));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message().find("no text"), std::string::npos);
}

TEST(ImageExtractorTest, SingleColumnTextNeverSplitIntoColumnsReturnsNoTableDetectedError) {
  // Every word on its own line, none close enough to another to ever
  // form a second column -- realistic OCR output for a plain paragraph,
  // not a table.
  std::vector<engine::OcrWord> words = {
      word("Hello", 20, 20, 50, 20, 96.0, 1, 1, 1),
      word("World", 20, 60, 50, 20, 96.0, 1, 1, 2),
  };
  ImageExtractor extractor(fake_provider(words));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message().find("no table structure"), std::string::npos);
}

TEST(ImageExtractorTest, SingleLineOnlyReturnsNoTableDetectedError) {
  // Even a wide multi-column single line isn't a table -- a header with
  // no data rows underneath it can't become any records.
  std::vector<engine::OcrWord> words = {
      word("Name", 20, 20, 50, 20, 96.0, 1, 1, 1),
      word("Email", 300, 20, 60, 20, 96.0, 1, 1, 1),
  };
  ImageExtractor extractor(fake_provider(words));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message().find("no table structure"), std::string::npos);
}

TEST(ImageExtractorTest, ReconstructsTableRowsAndColumnsFromWordBoundingBoxes) {
  ImageExtractor extractor(fake_provider(simple_table_words()));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  ASSERT_EQ(result->records.size(), 2u);
  EXPECT_EQ(result->records[0].field("Name"), "Ali");
  EXPECT_EQ(result->records[0].field("Email"), "ali@example.com");
  EXPECT_EQ(result->records[1].field("Name"), "Sara");
  EXPECT_EQ(result->records[1].field("Email"), "sara@example.com");
  EXPECT_TRUE(result->rejected_records.empty());
  ASSERT_TRUE(result->average_confidence.has_value());
  EXPECT_GT(*result->average_confidence, 0.0);
}

TEST(ImageExtractorTest, RowWithWrongColumnCountIsRejectedNotSilentlyDropped) {
  std::vector<engine::OcrWord> words = simple_table_words();
  // A third data line with only one column (no gap wide enough to split
  // it) -- structurally inconsistent with the two-column header.
  words.push_back(word("Solo", 20, 170, 400, 20, 90.0, 1, 2, 3));
  ImageExtractor extractor(fake_provider(words));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 3u);
  EXPECT_EQ(result->records.size(), 2u);
  EXPECT_EQ(result->rejected_record_count, 1u);
  ASSERT_EQ(result->rejected_records.size(), 1u);
  EXPECT_EQ(result->rejected_records[0].index, 3u);
}

TEST(ImageExtractorTest, TooManyTableRowsIsRejectedWholesale) {
  std::vector<engine::OcrWord> words = {
      word("Name", 20, 0, 40, 20, 95.0, 1, 1, 0),
      word("Email", 200, 0, 40, 20, 95.0, 1, 1, 0),
  };
  for (int i = 1; i <= 201; ++i) {
    const int top = i * 30;
    words.push_back(word("N" + std::to_string(i), 20, top, 30, 20, 95.0, 1, 2, i));
    words.push_back(word("e" + std::to_string(i) + "@x.com", 200, top, 80, 20, 95.0, 1, 2, i));
  }
  ImageExtractor extractor(fake_provider(words));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message().find("too many"), std::string::npos);
}

TEST(ImageExtractorTest, LowAverageConfidenceProducesAWarning) {
  std::vector<engine::OcrWord> words = {
      word("Name", 20, 20, 50, 20, 40.0, 1, 1, 1),
      word("Email", 300, 20, 60, 20, 35.0, 1, 1, 1),
      word("Ali", 20, 70, 45, 20, 30.0, 1, 2, 1),
      word("ali@example.com", 300, 70, 240, 20, 25.0, 1, 2, 1),
  };
  ImageExtractor extractor(fake_provider(words));
  auto result =
      extractor.extract({.source_type = domain::InputSourceType::Image, .content = png_signature_bytes()});
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_FALSE(result->warnings.empty());
}

}  // namespace
}  // namespace flowforge::extractors
