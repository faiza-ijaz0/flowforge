#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "flowforge/result.hpp"

namespace flowforge::engine {

/// One recognized word and its position, exactly as a real OCR engine
/// reports it (Phase 3D-1 -- see docs/architecture/input-processing.md,
/// "Image extraction"). `block_num`/`par_num`/`line_num` are the OCR
/// engine's own layout-analysis grouping (which words the engine believes
/// sit on the same visual line) -- `extractors::ImageExtractor` uses this
/// grouping, plus `left`/`top`/`width`/`height`, to reconstruct table
/// rows/columns; it is never business-domain-aware (no "name"/"email"
/// concept exists at this layer). `confidence` is the provider's own
/// per-word confidence in `[0, 100]`, or a negative value if the provider
/// doesn't report one for this word (mirrors Tesseract's own `-1`
/// convention for non-text detections).
struct OcrWord {
  std::string text;
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  double confidence = -1.0;
  int block_num = 0;
  int par_num = 0;
  int line_num = 0;
};

struct OcrResult {
  std::vector<OcrWord> words;
};

/// The swappable seam between FlowForge and whatever OCR technology
/// actually reads text out of image pixels (Phase 3D-1 -- see
/// docs/architecture/input-processing.md, "Extraction provider
/// architecture"). No FlowForge domain/engine type outside this file and
/// its implementations knows a concrete OCR vendor exists --
/// `extractors::ImageExtractor` depends only on this interface, so a
/// future cloud-vision provider or a different local OCR engine is a new
/// `IOcrProvider` implementation, never a change to `ImageExtractor`,
/// `StructuredRecord`, or anything upstream of it.
///
/// Implementations may be expensive to construct (e.g. probing for an
/// external binary) but `recognize()` itself must be safe to call
/// concurrently from multiple threads, mirroring `IInputExtractor`'s own
/// thread-safety contract.
class IOcrProvider {
 public:
  IOcrProvider() = default;
  virtual ~IOcrProvider() = default;
  IOcrProvider(const IOcrProvider&) = delete;
  IOcrProvider& operator=(const IOcrProvider&) = delete;
  IOcrProvider(IOcrProvider&&) = delete;
  IOcrProvider& operator=(IOcrProvider&&) = delete;

  /// Runs OCR over one already-validated, already-format-sniffed image
  /// (`infra::validate_image` has already run by the time this is called
  /// -- see `extractors::ImageExtractor`). Returns
  /// `ErrorCode::Infrastructure` if the OCR engine itself could not be
  /// reached/run at all (never a fake/empty result standing in for that
  /// failure) -- an image the engine *did* process but found no text in
  /// is a successful call returning an `OcrResult` with an empty `words`.
  [[nodiscard]] virtual Result<OcrResult> recognize(std::string_view image_bytes) const = 0;
};

}  // namespace flowforge::engine
