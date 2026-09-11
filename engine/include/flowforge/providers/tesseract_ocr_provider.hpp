#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "flowforge/engine/ocr_provider.hpp"
#include "flowforge/result.hpp"

namespace flowforge::providers {

/// `engine::IOcrProvider` implementation backed by the Tesseract OCR
/// command-line tool, invoked as an external process (Phase 3D-1 -- see
/// docs/architecture/input-processing.md, "Image extraction"). FlowForge
/// deliberately does not link against `libtesseract` directly: shelling
/// out to the CLI is the integration path that works identically on every
/// platform this project builds for (a system package on Linux CI, a
/// standard installer on Windows) without introducing a C++ ABI
/// dependency on a third-party library's build -- see
/// docs/architecture/input-processing.md, "Why the Tesseract CLI, not
/// libtesseract" for the full rationale.
///
/// Writes `image_bytes` to a process-unique temporary file, invokes
/// `tesseract <file> <output_base> -l eng --psm 6 tsv` (word-level
/// bounding boxes + confidence, never just plain text -- `
/// extractors::ImageExtractor` needs the boxes to reconstruct table rows/
/// columns), reads back `<output_base>.tsv`, and deletes both temporary
/// files before returning, success or failure.
class TesseractCliOcrProvider final : public engine::IOcrProvider {
 public:
  explicit TesseractCliOcrProvider(std::string executable_path,
                                   std::chrono::milliseconds timeout = std::chrono::milliseconds(15000));

  [[nodiscard]] Result<engine::OcrResult> recognize(std::string_view image_bytes) const override;

  /// Probes a small set of well-known locations (an explicit
  /// `FLOWFORGE_TESSERACT_PATH` override, then PATH, then the standard
  /// per-platform install directories) and actually runs `--version`
  /// against each candidate to confirm it is a real, executable Tesseract
  /// binary -- not merely that a file exists at that path. Returns
  /// `std::nullopt` if none is usable, which `apps/server/src/http/
  /// app.cpp` uses to decide whether to wire a real `ImageExtractor` in
  /// at all: a deployment without Tesseract installed gets a clean,
  /// honest "image extraction is not available" at every request rather
  /// than a crash the first time one is attempted.
  [[nodiscard]] static std::optional<std::string> discover_executable();

 private:
  std::string executable_path_;
  std::chrono::milliseconds timeout_;
};

}  // namespace flowforge::providers
