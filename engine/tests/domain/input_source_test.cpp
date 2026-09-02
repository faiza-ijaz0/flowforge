#include "flowforge/domain/input_source.hpp"

#include <gtest/gtest.h>

namespace flowforge::domain {
namespace {

TEST(InputSourceTypeStringTest, RoundTripsThroughAllValues) {
  for (auto type : {InputSourceType::Csv, InputSourceType::Image, InputSourceType::Screenshot,
                    InputSourceType::Text, InputSourceType::Url}) {
    auto parsed = input_source_type_from_string(to_string(type));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, type);
  }
}

TEST(InputSourceTypeStringTest, KnownStringValues) {
  EXPECT_EQ(to_string(InputSourceType::Csv), "csv");
  EXPECT_EQ(to_string(InputSourceType::Image), "image");
  EXPECT_EQ(to_string(InputSourceType::Screenshot), "screenshot");
  EXPECT_EQ(to_string(InputSourceType::Text), "text");
  EXPECT_EQ(to_string(InputSourceType::Url), "url");
}

TEST(InputSourceTypeStringTest, UnrecognizedStringReturnsNullopt) {
  EXPECT_FALSE(input_source_type_from_string("pdf").has_value());
  EXPECT_FALSE(input_source_type_from_string("").has_value());
  EXPECT_FALSE(
      input_source_type_from_string("CSV").has_value());  // case-sensitive, matches JobStatus's convention
}

TEST(InputPayloadTest, CarriesSourceTypeAndOpaqueContent) {
  InputPayload payload{.source_type = InputSourceType::Csv, .content = "name,email\nAlice,a@example.com\n"};
  EXPECT_EQ(payload.source_type, InputSourceType::Csv);
  EXPECT_EQ(payload.content, "name,email\nAlice,a@example.com\n");
}

}  // namespace
}  // namespace flowforge::domain
