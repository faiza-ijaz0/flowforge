#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flowforge::domain {

/// One record extracted from an input source, as a generic field-name ->
/// value map -- e.g. `{"name": "Alice", "email": "alice@example.com"}`
/// for a CSV row, or `{"name": "Widget", "price": "19.99", "sku":
/// "WID-1"}` for a future product CSV row. Deliberately not coupled to
/// any single business domain (see docs/architecture/input-processing.md,
/// "Structured record representation") -- turning a `StructuredRecord`
/// into a validated, business-specific type (`domain::
/// NormalizedUserRecord`, a future `NormalizedProductRecord`, ...) is the
/// job of a target-specific adapter downstream, not this type.
///
/// A plain `std::map<std::string, std::string, std::less<>>` rather than
/// a JSON value: the engine has zero JSON library dependency by design
/// (see docs/architecture/overview.md, "Dependency direction"), and a
/// flat string-to-string map is the simplest representation that already
/// covers every source this phase's architecture anticipates (CSV
/// columns, form-like key/value data). `std::less<>` enables heterogeneous
/// lookup (`field()` below takes a `string_view`, not a `std::string`,
/// without allocating a temporary).
struct StructuredRecord {
  std::map<std::string, std::string, std::less<>> fields;

  [[nodiscard]] std::optional<std::string_view> field(std::string_view name) const noexcept {
    const auto it = fields.find(name);
    if (it == fields.end()) {
      return std::nullopt;
    }
    return std::string_view(it->second);
  }

  /// Finds the first field whose (case-insensitive, trimmed) name
  /// matches one of `aliases` -- looser than `field()`'s exact match,
  /// for a target-specific mapping adapter matching a human- or OCR-
  /// generated column label (e.g. `"Product Code"`) against a canonical
  /// field name (e.g. `"sku"`). Shared by every target's mapping adapter
  /// (`services::map_structured_records_to_users`,
  /// `services::map_structured_records_to_products`, ...) so this
  /// fuzzy-matching rule -- and its rationale, see
  /// docs/architecture/input-processing.md, "Why image header matching is
  /// case-insensitive" -- is defined exactly once, not once per target.
  [[nodiscard]] std::optional<std::string_view> field_by_aliases(
      std::initializer_list<std::string_view> aliases) const noexcept {
    for (const auto& [key, value] : fields) {
      const std::string_view trimmed_key = trim(key);
      for (const auto& alias : aliases) {
        if (trimmed_key.size() == alias.size() &&
            std::ranges::equal(trimmed_key, alias, [](unsigned char a, unsigned char b) {
              return std::tolower(a) == std::tolower(b);
            })) {
          return std::string_view(value);
        }
      }
    }
    return std::nullopt;
  }

 private:
  [[nodiscard]] static std::string_view trim(std::string_view value) noexcept {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
      return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
  }
};

/// One record that failed extraction (a structural problem, e.g. the
/// wrong number of CSV fields) -- normalization/validation failures for a
/// specific business domain (e.g. "email must be valid") are reported by
/// that domain's own adapter, not here. `index` is the 1-indexed position
/// of the record within the input (e.g. the CSV data row number),
/// mirroring `services::RejectedImportRow::row_number`'s convention.
struct RejectedRecord {
  std::size_t index = 0;
  std::string reason;
};

/// Result of extracting `StructuredRecord`s from one `InputPayload` (see
/// `engine::IInputExtractor`). Mirrors `services::ParsedUserImport`'s
/// shape at a more generic level: `rejected_records` is bounded by the
/// extractor even though `total_records`/`rejected_record_count` remain
/// exact -- see each concrete extractor for its own limit.
///
/// `warnings` and `average_confidence` (Phase 3D-1) are generic,
/// extractor-optional metadata -- neither is CSV- or image-specific.
/// `CsvExtractor` leaves both at their empty/unset defaults;
/// `extractors::ImageExtractor` is the first extractor to populate either
/// (e.g. a "no table structure was detected, treated every line as a
/// single column" warning, and the OCR provider's own mean per-word
/// confidence) -- see docs/architecture/input-processing.md, "Image
/// extraction".
struct ExtractionResult {
  std::size_t total_records = 0;
  std::vector<StructuredRecord> records;
  std::vector<RejectedRecord> rejected_records;
  std::size_t rejected_record_count = 0;
  bool rejected_records_truncated = false;
  std::vector<std::string> warnings;
  std::optional<double> average_confidence;
};

}  // namespace flowforge::domain
