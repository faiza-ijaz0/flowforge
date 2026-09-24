#include "flowforge/persistence/postgres/postgres_user_repository.hpp"

#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresUserRepositoryTest = PostgresIntegrationTest;

domain::NormalizedUserRecord make_record(std::string email = "alice@example.com") {
  return {.name = "Alice", .email = std::move(email), .phone = "555-1234"};
}

/// `users.job_id` is a real foreign key (database/migrations/
/// 0016_create_users.sql) -- every test needs one genuine, already-
/// persisted `jobs` row to reference, mirroring
/// postgres_product_repository_test.cpp's identical pattern.
infra::JobId insert_test_job(const std::shared_ptr<PgConnectionPool>& pool,
                             const std::shared_ptr<infra::Logger>& logger) {
  PostgresJobRepository jobs(pool, logger);
  domain::Job job(infra::JobId::generate(), "integration-tests", R"({"hello":"world"})",
                  domain::RetryPolicy{}, std::chrono::system_clock::now());
  auto inserted = jobs.insert(job);
  if (!inserted) {
    ADD_FAILURE() << "failed to insert prerequisite job row: " << inserted.error().message();
  }
  return job.id();
}

TEST_F(PostgresUserRepositoryTest, UpsertThenFindByEmailRoundTripsFields) {
  auto job_id = insert_test_job(pool_, logger_);
  PostgresUserRepository repo(pool_, logger_);
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->email, "alice@example.com");
  EXPECT_EQ((*found)->name, "Alice");
  ASSERT_TRUE((*found)->phone.has_value());
  EXPECT_EQ(*(*found)->phone, "555-1234");
  ASSERT_TRUE((*found)->job_id.has_value());
  EXPECT_EQ(*(*found)->job_id, job_id);
}

TEST_F(PostgresUserRepositoryTest, FindByEmailReturnsNulloptForUnknownEmail) {
  PostgresUserRepository repo(pool_, logger_);
  auto found = repo.find_by_email("nobody@example.com");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PostgresUserRepositoryTest, PhoneIsNullableAndRoundTripsAsNullopt) {
  auto job_id = insert_test_job(pool_, logger_);
  PostgresUserRepository repo(pool_, logger_);
  domain::NormalizedUserRecord record{.name = "Bob", .email = "bob@example.com", .phone = std::nullopt};
  ASSERT_TRUE(repo.upsert(job_id, record).has_value());

  auto found = repo.find_by_email("bob@example.com");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_FALSE((*found)->phone.has_value());
}

TEST_F(PostgresUserRepositoryTest, ReimportingTheSameEmailUpdatesInPlaceRatherThanConflicting) {
  auto job_id_1 = insert_test_job(pool_, logger_);
  auto job_id_2 = insert_test_job(pool_, logger_);
  PostgresUserRepository repo(pool_, logger_);

  ASSERT_TRUE(repo.upsert(job_id_1, make_record()).has_value());
  auto first_id = (*repo.find_by_email("alice@example.com"))->id;

  // The whole point of upsert(): a duplicate email must never surface as
  // ErrorCode::Conflict -- see IUserRepository's class comment.
  domain::NormalizedUserRecord updated{.name = "Alice K.", .email = "alice@example.com", .phone = "555-9999"};
  auto second = repo.upsert(job_id_2, updated);
  ASSERT_TRUE(second.has_value()) << second.error().message();

  auto found = repo.find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_EQ((*found)->name, "Alice K.");
  EXPECT_EQ(*(*found)->job_id, job_id_2);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u);
}

TEST_F(PostgresUserRepositoryTest, ListRespectsLimitAndOffsetInCreationOrder) {
  PostgresUserRepository repo(pool_, logger_);
  std::vector<std::string> emails;
  for (int i = 0; i < 3; ++i) {
    auto job_id = insert_test_job(pool_, logger_);
    std::string email = "user" + std::to_string(i) + "@example.com";
    emails.push_back(email);
    ASSERT_TRUE(repo.upsert(job_id, make_record(email)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].email, emails[0]);
  EXPECT_EQ((*page1)[1].email, emails[1]);

  auto page2 = repo.list(2, 2);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].email, emails[2]);
}

// Restart-persistence: a user upserted by one repository instance is
// visible, unchanged, to a second instance backed by the same connection
// pool/database.
TEST_F(PostgresUserRepositoryTest, PersistsAcrossRepositoryInstances) {
  auto job_id = insert_test_job(pool_, logger_);
  {
    PostgresUserRepository writer(pool_, logger_);
    ASSERT_TRUE(writer.upsert(job_id, make_record()).has_value());
  }

  PostgresUserRepository reader(pool_, logger_);
  auto found = reader.find_by_email("alice@example.com");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->name, "Alice");
}

}  // namespace
}  // namespace flowforge::persistence::postgres
