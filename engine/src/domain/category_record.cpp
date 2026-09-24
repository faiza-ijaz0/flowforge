#include "flowforge/domain/category_record.hpp"

#include <cctype>

#include "flowforge/infra/json_lite.hpp"

namespace flowforge::domain {

namespace {

constexpr std::size_t kMaxNameLength = 200;
constexpr std::size_t kMaxSlugLength = 200;
constexpr std::size_t kMaxDescriptionLength = 2000;

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

}  // namespace

std::string slugify(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  bool pending_separator = false;
  for (char raw_c : value) {
    const auto c = static_cast<unsigned char>(raw_c);
    if (std::isalnum(c) != 0) {
      if (pending_separator && !result.empty()) {
        result.push_back('-');
      }
      result.push_back(static_cast<char>(std::tolower(c)));
      pending_separator = false;
    } else {
      // Remembered, not emitted immediately -- this is what collapses a
      // run of separators ("  & ") into a single "-", and (since a
      // pending separator is only ever flushed just before the *next*
      // alphanumeric character) what guarantees the result never starts
      // or ends with "-".
      pending_separator = true;
    }
  }
  return result;
}

Result<NormalizedCategoryRecord> validate_and_normalize_category_record(
    std::string_view name, std::optional<std::string_view> slug, std::optional<std::string_view> description,
    std::optional<std::string_view> parent_slug) {
  const std::string trimmed_name = trim(name);
  if (trimmed_name.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' must not be blank"));
  }
  if (trimmed_name.size() > kMaxNameLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'name' must be <= " + std::to_string(kMaxNameLength) + " characters"));
  }

  // An explicitly-provided slug is normalized through the exact same
  // slugify() step a derived one goes through (see the header's "Slug
  // semantics" note) -- one code path, not "reject if the explicit value
  // has odd characters" vs. "derive cleanly from name".
  const std::string trimmed_slug_input = slug ? trim(*slug) : std::string();
  const std::string_view slug_source =
      trimmed_slug_input.empty() ? std::string_view(trimmed_name) : std::string_view(trimmed_slug_input);
  const std::string normalized_slug = slugify(slug_source);
  if (normalized_slug.empty()) {
    return std::unexpected(make_error(
        ErrorCode::Validation,
        "could not derive a valid slug: '" + std::string(slug_source) + "' contains no letters or digits"));
  }
  if (normalized_slug.size() > kMaxSlugLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'slug' must be <= " + std::to_string(kMaxSlugLength) + " characters"));
  }

  std::optional<std::string> normalized_description;
  if (description) {
    std::string trimmed_description = trim(*description);
    if (trimmed_description.size() > kMaxDescriptionLength) {
      return std::unexpected(
          make_error(ErrorCode::Validation,
                     "'description' must be <= " + std::to_string(kMaxDescriptionLength) + " characters"));
    }
    if (!trimmed_description.empty()) {
      normalized_description = std::move(trimmed_description);
    }
  }

  std::optional<std::string> normalized_parent_slug;
  if (parent_slug) {
    const std::string trimmed_parent = trim(*parent_slug);
    if (!trimmed_parent.empty()) {
      std::string candidate = slugify(trimmed_parent);
      if (candidate.empty()) {
        return std::unexpected(make_error(ErrorCode::Validation, "'parent_slug' (\"" + trimmed_parent +
                                                                     "\") contains no letters or digits"));
      }
      if (candidate.size() > kMaxSlugLength) {
        return std::unexpected(
            make_error(ErrorCode::Validation,
                       "'parent_slug' must be <= " + std::to_string(kMaxSlugLength) + " characters"));
      }
      if (candidate == normalized_slug) {
        return std::unexpected(make_error(ErrorCode::Validation, "a category cannot be its own parent"));
      }
      normalized_parent_slug = std::move(candidate);
    }
  }

  return NormalizedCategoryRecord{.name = trimmed_name,
                                  .slug = normalized_slug,
                                  .description = std::move(normalized_description),
                                  .parent_slug = std::move(normalized_parent_slug)};
}

std::string serialize_category_record_as_job_payload(const NormalizedCategoryRecord& record,
                                                     bool parent_in_submission) {
  std::string payload = R"({"name":")" + infra::json_escape(record.name) + R"(","slug":")" +
                        infra::json_escape(record.slug) + R"(")";
  if (record.description) {
    payload += R"(,"description":")" + infra::json_escape(*record.description) + R"(")";
  }
  if (record.parent_slug) {
    payload += R"(,"parent_slug":")" + infra::json_escape(*record.parent_slug) + R"(")";
    if (parent_in_submission) {
      payload += R"(,"parent_in_submission":"true")";
    }
  }
  payload += "}";
  return payload;
}

}  // namespace flowforge::domain
