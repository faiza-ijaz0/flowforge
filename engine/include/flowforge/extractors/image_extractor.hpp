#pragma once

#include <memory>

#include "flowforge/engine/input_extractor.hpp"
#include "flowforge/engine/ocr_provider.hpp"

namespace flowforge::extractors {

/// Turns an image or screenshot into generic `domain::StructuredRecord`s
/// (Phase 3D-1 -- see docs/architecture/input-processing.md, "Image
/// extraction"): runs OCR via an injected `engine::IOcrProvider`, then
/// reconstructs table rows/columns from the recognized words' bounding
/// boxes -- a real, generic table-layout heuristic, not business-domain
/// aware (no "name"/"email" concept exists here; see
/// `services::map_structured_records_to_users` for the target-specific
/// adapter downstream that is aware of those).
///
/// `domain::InputSourceType::Image` and `::Screenshot` are both accepted
/// (see .cpp) -- a screenshot is pixel data like any other image, and
/// this codebase deliberately does not model two extractors for what is,
/// to an OCR engine, the same input shape (see docs/architecture/
/// input-processing.md, "Why Image and Screenshot share one extractor").
/// `source_type()` reports `Image` as this extractor's nominal identity
/// since `engine::IInputExtractor` has room for only one.
///
/// Never hardcodes or leaks a specific OCR vendor: all recognition goes
/// through `IOcrProvider`, so swapping providers (or adding a second one
/// behind a future registry) never touches this class's table-
/// reconstruction logic.
class ImageExtractor final : public engine::IInputExtractor {
 public:
  explicit ImageExtractor(std::shared_ptr<engine::IOcrProvider> ocr_provider)
      : ocr_provider_(std::move(ocr_provider)) {}

  [[nodiscard]] domain::InputSourceType source_type() const noexcept override {
    return domain::InputSourceType::Image;
  }

  [[nodiscard]] Result<domain::ExtractionResult> extract(const domain::InputPayload& payload) const override;

 private:
  std::shared_ptr<engine::IOcrProvider> ocr_provider_;
};

}  // namespace flowforge::extractors
