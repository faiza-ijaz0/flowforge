#include "flowforge/extractors/csv_extractor.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace flowforge::extractors {
namespace {

domain::InputPayload csv_payload(std::string content) {
  return {.source_type = domain::InputSourceType::Csv, .content = std::move(content)};
}

TEST(CsvExtractorTest, SourceTypeIsCsv) {
  CsvExtractor extractor;
  EXPECT_EQ(extractor.source_type(), domain::InputSourceType::Csv);
}

TEST(CsvExtractorTest, ExtractsGenericRecordsFromArbitraryColumns) {
  // No "name"/"email" assumption -- proves the extractor is genuinely
  // domain-agnostic, unlike services::parse_user_import_csv.
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("sku,price\nWID-1,19.99\nWID-2,4.50\n"));
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 2u);
  ASSERT_EQ(result->records.size(), 2u);
  EXPECT_EQ(result->records[0].field("sku"), "WID-1");
  EXPECT_EQ(result->records[0].field("price"), "19.99");
  EXPECT_EQ(result->records[1].field("sku"), "WID-2");
  EXPECT_TRUE(result->rejected_records.empty());
}

TEST(CsvExtractorTest, RejectsPayloadWithWrongSourceType) {
  CsvExtractor extractor;
  auto result = extractor.extract({.source_type = domain::InputSourceType::Text, .content = "hello"});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CsvExtractorTest, EmptyInputIsRejected) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload(""));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CsvExtractorTest, HeaderOnlyProducesZeroRecords) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("a,b\n"));
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 0u);
  EXPECT_TRUE(result->records.empty());
}

TEST(CsvExtractorTest, RowWithWrongFieldCountIsRejectedButOthersSucceed) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("a,b\n1,2\n3\n5,6\n"));
  ASSERT_TRUE(result.has_value()) << result.error().message();
  EXPECT_EQ(result->total_records, 3u);
  EXPECT_EQ(result->records.size(), 2u);
  ASSERT_EQ(result->rejected_records.size(), 1u);
  EXPECT_EQ(result->rejected_records[0].index, 2u);
}

TEST(CsvExtractorTest, QuotedFieldsWithEmbeddedCommasAreParsed) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("name\n\"Doe, John\"\n"));
  ASSERT_TRUE(result.has_value()) << result.error().message();
  ASSERT_EQ(result->records.size(), 1u);
  EXPECT_EQ(result->records[0].field("name"), "Doe, John");
}

TEST(CsvExtractorTest, DuplicateHeaderColumnIsRejected) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("a,a\n1,2\n"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CsvExtractorTest, MalformedCsvIsRejected) {
  CsvExtractor extractor;
  auto result = extractor.extract(csv_payload("a,b\n\"unterminated\n"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CsvExtractorTest, InvalidUtf8IsRejected) {
  CsvExtractor extractor;
  std::string content = "a\n";
  content += "\xFF\xFE\n";
  auto result = extractor.extract(csv_payload(content));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(CsvExtractorTest, RecordCountExceedingLimitIsRejectedWholesale) {
  CsvExtractor extractor;
  std::ostringstream csv;
  csv << "a\n";
  for (int i = 0; i < 1001; ++i) {
    csv << i << "\n";
  }
  auto result = extractor.extract(csv_payload(csv.str()));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

}  // namespace
}  // namespace flowforge::extractors
