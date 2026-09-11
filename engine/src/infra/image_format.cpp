#include "flowforge/infra/image_format.hpp"

#include <array>

namespace flowforge::infra {

namespace {

constexpr std::array<unsigned char, 8> kPngSignature = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::array<unsigned char, 3> kJpegSignature = {0xFF, 0xD8, 0xFF};

bool starts_with(std::string_view bytes, const unsigned char* signature, std::size_t signature_len) {
  if (bytes.size() < signature_len) {
    return false;
  }
  for (std::size_t i = 0; i < signature_len; ++i) {
    if (static_cast<unsigned char>(bytes[i]) != signature[i]) {
      return false;
    }
  }
  return true;
}

/// WebP's signature is not a fixed byte run: bytes 0-3 are the RIFF
/// container tag, bytes 4-7 are the (variable) chunk size, and bytes 8-11
/// are the "WEBP" format tag -- both fixed spans must match, with the
/// size field in between left unchecked.
bool is_webp(std::string_view bytes) {
  if (bytes.size() < 12) {
    return false;
  }
  return bytes.substr(0, 4) == std::string_view("RIFF", 4) &&
         bytes.substr(8, 4) == std::string_view("WEBP", 4);
}

}  // namespace

Result<ImageFormat> validate_image(std::string_view bytes) {
  if (bytes.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "image upload is empty"));
  }
  if (bytes.size() > kMaxImageBytes) {
    return std::unexpected(make_error(
        ErrorCode::Validation, "image upload must be <= " + std::to_string(kMaxImageBytes) + " bytes"));
  }
  if (starts_with(bytes, kPngSignature.data(), kPngSignature.size())) {
    return ImageFormat::Png;
  }
  if (starts_with(bytes, kJpegSignature.data(), kJpegSignature.size())) {
    return ImageFormat::Jpeg;
  }
  if (is_webp(bytes)) {
    return ImageFormat::WebP;
  }
  return std::unexpected(make_error(
      ErrorCode::Validation, "unsupported or corrupt image -- only PNG, JPEG, and WebP are supported"));
}

std::string_view to_string(ImageFormat format) noexcept {
  switch (format) {
    case ImageFormat::Png:
      return "png";
    case ImageFormat::Jpeg:
      return "jpeg";
    case ImageFormat::WebP:
      return "webp";
  }
  return "unknown";
}

}  // namespace flowforge::infra
