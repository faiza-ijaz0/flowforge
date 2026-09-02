#pragma once

#include "flowforge/engine/input_extractor.hpp"

namespace flowforge::extractors {

/// The first concrete `engine::IInputExtractor` (Phase 3C -- see
/// docs/architecture/input-processing.md). Turns generic CSV bytes into
/// generic `domain::StructuredRecord`s: the header row's columns become
/// each record's field names, verbatim -- there are no required columns
/// and no business-rule validation here (see `IInputExtractor`'s class
/// comment) -- only structural checks (non-empty, bounded size/row
/// count, valid UTF-8, no duplicate header column, every data row has
/// the header's field count).
///
/// Reuses the exact same, already-proven CSV tokenizer
/// (`infra::tokenize_csv`/`infra::is_valid_utf8`/`infra::strip_utf8_bom`,
/// `engine/include/flowforge/infra/csv.hpp`) that
/// `services::parse_user_import_csv` (the user-import-specific CSV
/// parser) uses -- there is exactly one RFC 4180 CSV implementation in
/// this codebase, never two. See docs/architecture/input-processing.md,
/// "Why CsvExtractor is not yet wired into /users" for why the existing
/// `/api/v1/workloads/user-imports` endpoint continues to use
/// `parse_user_import_csv` directly rather than this class.
class CsvExtractor final : public engine::IInputExtractor {
 public:
  [[nodiscard]] domain::InputSourceType source_type() const noexcept override {
    return domain::InputSourceType::Csv;
  }

  [[nodiscard]] Result<domain::ExtractionResult> extract(const domain::InputPayload& payload) const override;
};

}  // namespace flowforge::extractors
