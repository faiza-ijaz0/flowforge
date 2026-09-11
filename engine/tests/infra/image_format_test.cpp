#include "flowforge/infra/image_format.hpp"

#include <gtest/gtest.h>

#include <string>

namespace flowforge::infra {
namespace {

// Neither literal below contains an embedded NUL byte, so the implicit
// `const char*` -> `std::string` conversion (which stops at the first
// NUL) captures the whole literal correctly without a fragile manual
// length argument.
std::string png_bytes() {
  return std::string("\x89PNG\r\n\x1a\n") + "rest-of-file-content-here";
}

std::string jpeg_bytes() {
  return std::string("\xFF\xD8\xFF\xE0") + "rest-of-jpeg";
}

std::string webp_bytes() {
  std::string bytes = "RIFF";
  bytes += std::string(4, '\0');  // chunk size, unchecked by validate_image
  bytes += "WEBP";
  bytes += "VP8 more-data";
  return bytes;
}

TEST(ImageFormatTest, DetectsPng) {
  auto format = validate_image(png_bytes());
  ASSERT_TRUE(format.has_value());
  EXPECT_EQ(*format, ImageFormat::Png);
}

TEST(ImageFormatTest, DetectsJpeg) {
  auto format = validate_image(jpeg_bytes());
  ASSERT_TRUE(format.has_value());
  EXPECT_EQ(*format, ImageFormat::Jpeg);
}

TEST(ImageFormatTest, DetectsWebP) {
  auto format = validate_image(webp_bytes());
  ASSERT_TRUE(format.has_value());
  EXPECT_EQ(*format, ImageFormat::WebP);
}

TEST(ImageFormatTest, RejectsEmptyInput) {
  auto format = validate_image("");
  ASSERT_FALSE(format.has_value());
  EXPECT_EQ(format.error().code(), ErrorCode::Validation);
}

TEST(ImageFormatTest, RejectsOversizedInput) {
  const std::string oversized(kMaxImageBytes + 1, 'x');
  auto format = validate_image(oversized);
  ASSERT_FALSE(format.has_value());
  EXPECT_NE(format.error().message().find("<="), std::string::npos);
}

TEST(ImageFormatTest, RejectsCorruptOrUnrecognizedBytes) {
  auto format = validate_image("this is definitely not an image file");
  ASSERT_FALSE(format.has_value());
  EXPECT_EQ(format.error().code(), ErrorCode::Validation);
}

TEST(ImageFormatTest, RejectsUnsupportedFormatSignature) {
  // GIF87a signature -- a real image format, but not one FlowForge's OCR
  // pipeline (Tesseract via Leptonica) is configured to accept this phase.
  auto format = validate_image(std::string("GIF87a", 6) + "restofgif");
  ASSERT_FALSE(format.has_value());
  EXPECT_NE(format.error().message().find("unsupported"), std::string::npos);
}

TEST(ImageFormatTest, RejectsTruncatedSignature) {
  auto format = validate_image(std::string("\x89PN", 3));
  ASSERT_FALSE(format.has_value());
}

TEST(ImageFormatTest, ToStringMapsEveryFormat) {
  EXPECT_EQ(to_string(ImageFormat::Png), "png");
  EXPECT_EQ(to_string(ImageFormat::Jpeg), "jpeg");
  EXPECT_EQ(to_string(ImageFormat::WebP), "webp");
}

}  // namespace
}  // namespace flowforge::infra
