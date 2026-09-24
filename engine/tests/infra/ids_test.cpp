#include "flowforge/infra/ids.hpp"

#include <gtest/gtest.h>

#include <regex>
#include <unordered_set>

namespace flowforge::infra {
namespace {

TEST(GenerateUuidV4Test, MatchesRfc4122Version4Format) {
  static const std::regex kUuidV4Pattern(
      "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
  const std::string uuid = generate_uuid_v4();
  EXPECT_TRUE(std::regex_match(uuid, kUuidV4Pattern)) << "uuid: " << uuid;
}

TEST(GenerateUuidV4Test, GeneratesUniqueValues) {
  std::unordered_set<std::string> seen;
  for (int i = 0; i < 1000; ++i) {
    EXPECT_TRUE(seen.insert(generate_uuid_v4()).second);
  }
}

TEST(IsUuidTest, AcceptsGeneratedAndCanonicalUuids) {
  EXPECT_TRUE(is_uuid(generate_uuid_v4()));
  EXPECT_TRUE(is_uuid("00000000-0000-0000-0000-000000000000"));
  EXPECT_TRUE(is_uuid("A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D"));
}

TEST(IsUuidTest, RejectsMalformedValues) {
  EXPECT_FALSE(is_uuid(""));
  EXPECT_FALSE(is_uuid("does-not-exist"));
  EXPECT_FALSE(is_uuid("00000000-0000-0000-0000-00000000000"));    // 35 chars
  EXPECT_FALSE(is_uuid("00000000-0000-0000-0000-0000000000000"));  // 37 chars
  EXPECT_FALSE(is_uuid("00000000_0000-0000-0000-000000000000"));   // wrong separator
  EXPECT_FALSE(is_uuid("0000000g-0000-0000-0000-000000000000"));   // non-hex
  EXPECT_FALSE(is_uuid("00000000-0000-0000-0000-00000000000'"));   // injection-style char
  EXPECT_FALSE(is_uuid("../../../../etc/passwd-0000-000000000"));
}

TEST(IdTest, DistinctTagsAreDistinctTypes) {
  JobId job_id = JobId::generate();
  WorkerId worker_id = WorkerId::generate();
  EXPECT_NE(job_id.value(), std::string{});
  EXPECT_NE(worker_id.value(), std::string{});
}

TEST(IdTest, EqualityComparesUnderlyingValue) {
  JobId a{"same-value"};
  JobId b{"same-value"};
  JobId c{"different-value"};
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(IdTest, EmptyReflectsDefaultConstruction) {
  JobId id;
  EXPECT_TRUE(id.empty());
  JobId generated = JobId::generate();
  EXPECT_FALSE(generated.empty());
}

}  // namespace
}  // namespace flowforge::infra
