#pragma once

// Shared support for the Tesseract-backed OCR integration tests (Phase
// 3D-1). These tests run real OCR against a real, deterministic fixture
// image (engine/tests/fixtures/user_table.png) -- never a mock, and never
// a network service (see docs/architecture/input-processing.md, "Testing:
// real vs. fake OCR"). If no usable `tesseract` binary is found on this
// machine, every TEST_F using OcrIntegrationTest calls GTEST_SKIP() --
// reported honestly as "SKIPPED", never silently treated as a pass.
// Mirrors engine/tests/persistence/postgres/postgres_test_support.hpp's
// convention exactly.

#include <gtest/gtest.h>

#include <string>

#include "flowforge/providers/tesseract_ocr_provider.hpp"
#include "flowforge/test_support/fixtures_path.hpp"

namespace flowforge::providers::test {

class OcrIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto path = TesseractCliOcrProvider::discover_executable();
    if (!path) {
      GTEST_SKIP() << "No usable Tesseract OCR binary found on this machine -- skipping real-OCR "
                      "integration tests. Install Tesseract (e.g. `apt-get install tesseract-ocr` on "
                      "Linux, or the UB-Mannheim Windows installer) or set FLOWFORGE_TESSERACT_PATH. "
                      "See docs/architecture/input-processing.md, \"Image extraction\".";
    }
    tesseract_path_ = *path;
  }

  [[nodiscard]] static std::string fixture_path(const std::string& filename) {
    return std::string(flowforge::test::kFixturesDir) + "/" + filename;
  }

  std::string tesseract_path_;
};

}  // namespace flowforge::providers::test
