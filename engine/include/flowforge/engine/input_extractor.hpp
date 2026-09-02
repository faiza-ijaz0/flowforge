#pragma once

#include "flowforge/domain/input_source.hpp"
#include "flowforge/domain/structured_record.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Extracts `domain::StructuredRecord`s from one `domain::InputPayload`
/// (Phase 3C -- see docs/architecture/input-processing.md). The generic,
/// source-agnostic seam a concrete extractor (`extractors::CsvExtractor`
/// today; a future image/OCR extractor later) implements, mirroring
/// `IJobHandler`'s shape/conventions (job_handler.hpp): resolved by
/// `source_type()`, never a switch statement over concrete extractor
/// types at the call site.
///
/// An extractor knows nothing about *why* the records will be used (no
/// "user"/"product" awareness) -- see `domain::StructuredRecord`'s class
/// comment. It only knows how to turn its one source type's raw bytes
/// into generic field-name -> value records, rejecting only structural
/// problems (e.g. a CSV row with the wrong field count), never
/// business-rule problems (e.g. "email must be valid") -- that
/// validation is a target-specific adapter's job, downstream of
/// extraction (see docs/architecture/input-processing.md, "Extraction
/// pipeline").
///
/// Thread-safety: mirrors `IJobHandler` -- implementations are expected
/// to be stateless (or otherwise safe to call concurrently), since a
/// single registered instance may be resolved and used from multiple
/// threads.
class IInputExtractor {
 public:
  IInputExtractor() = default;
  virtual ~IInputExtractor() = default;
  IInputExtractor(const IInputExtractor&) = delete;
  IInputExtractor& operator=(const IInputExtractor&) = delete;
  IInputExtractor(IInputExtractor&&) = delete;
  IInputExtractor& operator=(IInputExtractor&&) = delete;

  [[nodiscard]] virtual domain::InputSourceType source_type() const noexcept = 0;

  /// Returns `ErrorCode::Validation` for a `payload.source_type` this
  /// extractor doesn't handle, or for any failure that invalidates the
  /// *whole* input (empty/oversized, malformed, too many records) -- a
  /// per-record structural problem is reported via
  /// `domain::ExtractionResult::rejected_records` instead, never aborting
  /// the whole extraction (mirrors `services::parse_user_import_csv`'s
  /// whole-file-vs-per-row split).
  [[nodiscard]] virtual Result<domain::ExtractionResult> extract(
      const domain::InputPayload& payload) const = 0;
};

}  // namespace flowforge::engine
