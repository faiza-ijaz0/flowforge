#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::NormalizedUserRecord make_record(std::string email = "alice@example.com") {
  return {.name = "Alice", .email = std::move(email), .phone = std::nullopt};
}

TEST(InMemoryUserRepositoryTest, UpsertThenFindByEmail) {
  InMemoryUserRepository repo;
  auto job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->email, "alice@example.com");
  EXPECT_EQ((*found)->name, "Alice");
  EXPECT_EQ((*found)->job_id, job_id);
}

TEST(InMemoryUserRepositoryTest, FindByEmailReturnsNulloptForUnknownEmail) {
  InMemoryUserRepository repo;
  auto found = repo.find_by_email("nobody@example.com");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->has_value());
}

TEST(InMemoryUserRepositoryTest, ReimportingTheSameEmailUpdatesInPlace) {
  InMemoryUserRepository repo;
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record()).has_value());
  auto first_id = (*repo.find_by_email("alice@example.com"))->id;

  auto second_job_id = infra::JobId::generate();
  domain::NormalizedUserRecord updated{.name = "Alice K.", .email = "alice@example.com", .phone = "555-1234"};
  ASSERT_TRUE(repo.upsert(second_job_id, updated).has_value());

  auto found = repo.find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_EQ((*found)->name, "Alice K.");
  ASSERT_TRUE((*found)->phone.has_value());
  EXPECT_EQ(*(*found)->phone, "555-1234");
  EXPECT_EQ((*found)->job_id, second_job_id);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "an upsert of an existing email must never create a second row";
}

TEST(InMemoryUserRepositoryTest, CountReflectsDistinctEmailsOnly) {
  InMemoryUserRepository repo;
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("alice@example.com")).has_value());
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("bob@example.com")).has_value());
  ASSERT_TRUE(
      repo.upsert(infra::JobId::generate(), make_record("alice@example.com")).has_value());  // re-import

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 2u);
}

TEST(InMemoryUserRepositoryTest, ListRespectsLimitAndOffsetInInsertionOrder) {
  InMemoryUserRepository repo;
  std::vector<std::string> emails;
  for (int i = 0; i < 5; ++i) {
    std::string email = "user" + std::to_string(i) + "@example.com";
    emails.push_back(email);
    ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record(email)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value());
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].email, emails[0]);
  EXPECT_EQ((*page1)[1].email, emails[1]);

  auto page2 = repo.list(2, 4);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].email, emails[4]);
}

}  // namespace
}  // namespace flowforge::persistence
