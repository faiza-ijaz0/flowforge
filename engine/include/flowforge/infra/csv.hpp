#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "flowforge/result.hpp"

namespace flowforge::infra {

/// Strips a leading UTF-8 BOM (`EF BB BF`) if present -- a common artifact
/// of CSVs exported from spreadsheet tools. Returns `content` unchanged
/// otherwise.
[[nodiscard]] std::string_view strip_utf8_bom(std::string_view content) noexcept;

/// Structural (not full Unicode-semantic) UTF-8 well-formedness check:
/// every multi-byte sequence's continuation bytes are well-formed. Not a
/// complete validator (does not reject overlong encodings or surrogate
/// code points) -- sufficient to guarantee downstream JSON serialization
/// (`nlohmann::json`, which requires valid UTF-8 for string values) never
/// throws on this content, which is the actual requirement every caller
/// of this function has had so far.
[[nodiscard]] bool is_valid_utf8(std::string_view s) noexcept;

/// Single-pass, RFC 4180-style CSV tokenizer: handles quoted fields (`""`
/// as an escaped quote, commas/newlines inside quotes), `\r\n`/`\n`/bare-
/// `\r` row endings, and skips wholly-blank lines (a common trailing-
/// newline artifact) without counting them as rows. Aborts as soon as the
/// row count would exceed `max_rows` -- bounded work, not a full
/// tokenize-then-check pass. A raw `"` appearing inside an already-started
/// unquoted field is rejected as malformed (`ErrorCode::Validation`)
/// rather than silently accepted -- a deliberate strictness choice.
///
/// Deliberately domain-agnostic: this function knows nothing about what
/// the rows mean (users, products, categories, ...) -- see
/// `services::parse_user_import_csv` for the first, user-specific
/// consumer, and docs/architecture/user-import.md, "Why this isn't a
/// WorkloadService method" for why domain-specific CSV interpretation is
/// kept out of both this function and `WorkloadService`.
[[nodiscard]] Result<std::vector<std::vector<std::string>>> tokenize_csv(std::string_view content,
                                                                         std::size_t max_rows);

}  // namespace flowforge::infra
