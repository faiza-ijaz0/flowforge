#include "flowforge/services/user_import_parser.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace flowforge::services {
namespace {

TEST(ParseUserImportCsvTest, ValidCsvProducesNormalizedRows) {
  const std::string csv =
      "name,email,phone\n"
      "  Alice Khan , ALICE@EXAMPLE.COM ,555-1111\n"
      "Bob,bob@example.com,\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 2u);
  ASSERT_EQ(result->valid_rows.size(), 2u);
  EXPECT_TRUE(result->rejected_rows.empty());
  EXPECT_EQ(result->valid_rows[0].name, "Alice Khan");
  EXPECT_EQ(result->valid_rows[0].email, "alice@example.com");
  ASSERT_TRUE(result->valid_rows[0].phone.has_value());
  EXPECT_EQ(*result->valid_rows[0].phone, "555-1111");
  EXPECT_EQ(result->valid_rows[1].name, "Bob");
  EXPECT_FALSE(result->valid_rows[1].phone.has_value());
}

TEST(ParseUserImportCsvTest, HeaderWithoutPhoneColumnIsValid) {
  const std::string csv = "name,email\nAlice,alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
}

TEST(ParseUserImportCsvTest, ColumnOrderIsIrrelevant) {
  const std::string csv = "email,name\nalice@example.com,Alice\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
  EXPECT_EQ(result->valid_rows[0].name, "Alice");
  EXPECT_EQ(result->valid_rows[0].email, "alice@example.com");
}

TEST(ParseUserImportCsvTest, MissingNameColumnIsRejected) {
  auto result = parse_user_import_csv("email\na@example.com\n");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, MissingEmailColumnIsRejected) {
  auto result = parse_user_import_csv("name\nAlice\n");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, UnrecognizedColumnIsRejected) {
  auto result = parse_user_import_csv("name,email,age\nAlice,a@example.com,30\n");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, DuplicateHeaderColumnIsRejected) {
  auto result = parse_user_import_csv("name,name,email\nAlice,Alice,a@example.com\n");
  ASSERT_FALSE(result.has_value());
}

TEST(ParseUserImportCsvTest, EmptyFileIsRejected) {
  auto result = parse_user_import_csv("");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, HeaderOnlyProducesZeroRows) {
  auto result = parse_user_import_csv("name,email\n");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 0u);
  EXPECT_TRUE(result->valid_rows.empty());
  EXPECT_TRUE(result->rejected_rows.empty());
}

TEST(ParseUserImportCsvTest, TrailingBlankLineIsIgnored) {
  auto result = parse_user_import_csv("name,email\nAlice,a@example.com\n\n");
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 1u);
}

TEST(ParseUserImportCsvTest, QuotedFieldsWithEmbeddedCommasAreParsed) {
  const std::string csv = "name,email\n\"Khan, Alice\",alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
  EXPECT_EQ(result->valid_rows[0].name, "Khan, Alice");
}

TEST(ParseUserImportCsvTest, DoubledQuoteInsideQuotedFieldIsUnescaped) {
  const std::string csv = "name,email\n\"Ali \"\"Al\"\" Khan\",alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
  EXPECT_EQ(result->valid_rows[0].name, R"(Ali "Al" Khan)");
}

TEST(ParseUserImportCsvTest, QuotedFieldWithEmbeddedNewlineIsParsed) {
  const std::string csv = "name,email\n\"Alice\nKhan\",alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
  EXPECT_EQ(result->valid_rows[0].name, "Alice\nKhan");
}

TEST(ParseUserImportCsvTest, UnterminatedQuoteIsRejected) {
  auto result = parse_user_import_csv("name,email\n\"Alice,alice@example.com\n");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, StrayQuoteInUnquotedFieldIsRejected) {
  auto result = parse_user_import_csv("name,email\nAl\"ice,alice@example.com\n");
  ASSERT_FALSE(result.has_value());
}

TEST(ParseUserImportCsvTest, RowWithWrongFieldCountIsRejectedButOthersSucceed) {
  const std::string csv =
      "name,email\n"
      "Alice,alice@example.com\n"
      "Bob,bob@example.com,extra\n"
      "Carol,carol@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 3u);
  EXPECT_EQ(result->valid_rows.size(), 2u);
  ASSERT_EQ(result->rejected_rows.size(), 1u);
  EXPECT_EQ(result->rejected_rows[0].row_number, 2u);
}

TEST(ParseUserImportCsvTest, BlankRequiredFieldRejectsOnlyThatRow) {
  const std::string csv =
      "name,email\n"
      ",alice@example.com\n"
      "Bob,bob@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 2u);
  EXPECT_EQ(result->valid_rows.size(), 1u);
  ASSERT_EQ(result->rejected_rows.size(), 1u);
  EXPECT_EQ(result->rejected_rows[0].row_number, 1u);
}

TEST(ParseUserImportCsvTest, InvalidEmailRejectsOnlyThatRow) {
  const std::string csv =
      "name,email\n"
      "Alice,not-an-email\n"
      "Bob,bob@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->valid_rows.size(), 1u);
  EXPECT_EQ(result->valid_rows[0].name, "Bob");
  ASSERT_EQ(result->rejected_rows.size(), 1u);
  EXPECT_EQ(result->rejected_rows[0].row_number, 1u);
}

TEST(ParseUserImportCsvTest, DuplicateEmailKeepsFirstRejectsSubsequent) {
  const std::string csv =
      "name,email\n"
      "Alice,dup@example.com\n"
      "Bob,other@example.com\n"
      "Alice Two,DUP@EXAMPLE.COM\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, 3u);
  ASSERT_EQ(result->valid_rows.size(), 2u);
  EXPECT_EQ(result->valid_rows[0].name, "Alice");
  ASSERT_EQ(result->rejected_rows.size(), 1u);
  EXPECT_EQ(result->rejected_rows[0].row_number, 3u);
  EXPECT_NE(result->rejected_rows[0].reason.find("duplicate"), std::string::npos);
}

TEST(ParseUserImportCsvTest, RowCountExceedingLimitIsRejectedWholesale) {
  std::ostringstream csv;
  csv << "name,email\n";
  for (std::size_t i = 0; i < kMaxUserImportRows + 1; ++i) {
    csv << "User" << i << ",user" << i << "@example.com\n";
  }
  auto result = parse_user_import_csv(csv.str());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, RowCountAtExactLimitSucceeds) {
  std::ostringstream csv;
  csv << "name,email\n";
  for (std::size_t i = 0; i < kMaxUserImportRows; ++i) {
    csv << "User" << i << ",user" << i << "@example.com\n";
  }
  auto result = parse_user_import_csv(csv.str());
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, kMaxUserImportRows);
  EXPECT_EQ(result->valid_rows.size(), kMaxUserImportRows);
}

TEST(ParseUserImportCsvTest, OversizedFileIsRejected) {
  // Comfortably over the 2 MiB file-size bound with a single legitimate
  // header plus one absurdly long data row.
  std::string csv = "name,email\nAlice,";
  csv += std::string(std::size_t{3} * 1024 * 1024, 'a');
  csv += "@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, InvalidUtf8IsRejected) {
  std::string csv = "name,email\n";
  csv += "Alice\xFF\xFE,alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(ParseUserImportCsvTest, Utf8BomIsStripped) {
  std::string csv = "\xEF\xBB\xBFname,email\nAlice,alice@example.com\n";
  auto result = parse_user_import_csv(csv);
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->valid_rows.size(), 1u);
}

TEST(ParseUserImportCsvTest, ManyRejectedRowsAreCountedButReportTruncated) {
  std::ostringstream csv;
  csv << "name,email\n";
  constexpr int kInvalidRows = 250;
  for (int i = 0; i < kInvalidRows; ++i) {
    csv << ",not-an-email-" << i << "\n";  // blank name -> every row rejected
  }
  auto result = parse_user_import_csv(csv.str());
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_rows, static_cast<std::size_t>(kInvalidRows));
  EXPECT_TRUE(result->valid_rows.empty());
  EXPECT_EQ(result->rejected_row_count, static_cast<std::size_t>(kInvalidRows));
  EXPECT_LT(result->rejected_rows.size(), static_cast<std::size_t>(kInvalidRows));
  EXPECT_TRUE(result->rejected_rows_truncated);
}

}  // namespace
}  // namespace flowforge::services
