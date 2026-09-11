#pragma once

#include <cstddef>
#include <string_view>

#include "flowforge/result.hpp"

namespace flowforge::infra {

/// Image formats FlowForge accepts for OCR extraction (Phase 3D-1 -- see
/// docs/architecture/input-processing.md, "Image validation"). Deliberately
/// small and closed: every format here is one Tesseract's bundled Leptonica
/// image-decoding library can actually read, so `validate_image` never
/// accepts a format the extraction step downstream can't handle.
enum class ImageFormat : std::uint8_t { Png, Jpeg, WebP };

/// Maximum accepted upload size for an image/screenshot payload -- well
/// below the server's coarse 8 MiB request-body cap
/// (`apps/server/src/http/app.cpp`) so this check, not httplib's, is what a
/// caller actually sees for an oversized image.
inline constexpr std::size_t kMaxImageBytes = std::size_t{6} * 1024 * 1024;

/// Determines `bytes`' real image format by inspecting its file-signature
/// ("magic bytes") -- never by trusting a caller-supplied MIME type or file
/// extension, both of which are attacker-controlled (see
/// docs/architecture/input-processing.md, "Security: image validation").
/// Returns `ErrorCode::Validation` if `bytes` is empty, exceeds
/// `kMaxImageBytes`, or does not begin with a recognized signature for any
/// of `ImageFormat`'s members -- the error message is deliberately generic
/// (never echoes back the malformed bytes or a parser's internal detail).
[[nodiscard]] Result<ImageFormat> validate_image(std::string_view bytes);

[[nodiscard]] std::string_view to_string(ImageFormat format) noexcept;

}  // namespace flowforge::infra
