#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace flowforge::infra {

/// Extracts the string value of `"key": "..."` from a flat JSON object
/// `payload`. Supports only `\"` and `\\` escapes -- returns
/// `std::nullopt` if `key` is not present, is not followed by a JSON
/// string value, or the string is unterminated. Deliberately not a
/// general-purpose JSON parser: the engine has zero JSON library
/// dependency by design (see docs/architecture/overview.md, "Dependency
/// direction") -- every job payload this codebase hand-parses (
/// `handlers::UserProcessHandler`, `handlers::ProductProcessHandler`) is a
/// flat, string-valued object it wrote itself via `json_escape` below, so
/// this bounded-scope parser is sufficient for every real caller.
[[nodiscard]] std::optional<std::string> extract_json_string_field(std::string_view payload,
                                                                   std::string_view key);

/// Escapes '"' and '\\' so a value can be safely embedded back into a
/// hand-built JSON string -- the write-side counterpart of
/// `extract_json_string_field` above.
[[nodiscard]] std::string json_escape(std::string_view value);

}  // namespace flowforge::infra
