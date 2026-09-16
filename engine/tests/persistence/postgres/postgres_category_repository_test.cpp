#include "flowforge/persistence/postgres/postgres_category_repository.hpp"

#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresCategoryRepositoryTest = PostgresIntegrationTest;

domain::NormalizedCategoryRecord make_record(std::string slug = "electronics") {
  return {
      .name = "Electronics", .slug = std::move(slug), .description = "Gadgets", .parent_slug = std::nullopt};
}

/// `categories.job_id` is a real foreign key (database/migrations/
/// 0015_create_categories.sql) -- every test needs one genuine,
/// already-persisted `jobs` row to reference, mirroring
/// postgres_product_repository_test.cpp's identical helper.
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

TEST_F(PostgresCategoryRepositoryTest, UpsertThenFindBySlugRoundTripsFields) {
  auto job_id = insert_test_job(pool_, logger_);
  PostgresCategoryRepository repo(pool_, logger_);
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_slug("electronics");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->slug, "electronics");
  EXPECT_EQ((*found)->name, "Electronics");
  ASSERT_TRUE((*found)->description.has_value());
  EXPECT_EQ(*(*found)->description, "Gadgets");
  EXPECT_FALSE((*found)->parent_slug.has_value());
  ASSERT_TRUE((*found)->job_id.has_value());
  EXPECT_EQ(*(*found)->job_id, job_id);
}

TEST_F(PostgresCategoryRepositoryTest, FindBySlugReturnsNulloptForUnknownSlug) {
  PostgresCategoryRepository repo(pool_, logger_);
  auto found = repo.find_by_slug("nope");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PostgresCategoryRepositoryTest, PersistsAndReadsBackParentSlug) {
  auto job_id = insert_test_job(pool_, logger_);
  PostgresCategoryRepository repo(pool_, logger_);
  ASSERT_TRUE(repo.upsert(job_id, make_record("electronics")).has_value());

  domain::NormalizedCategoryRecord child{
      .name = "Laptops", .slug = "laptops", .description = std::nullopt, .parent_slug = "electronics"};
  ASSERT_TRUE(repo.upsert(job_id, child).has_value());

  auto found = repo.find_by_slug("laptops");
  ASSERT_TRUE(found.has_value() && found->has_value());
  ASSERT_TRUE((*found)->parent_slug.has_value());
  EXPECT_EQ(*(*found)->parent_slug, "electronics");
}

TEST_F(PostgresCategoryRepositoryTest, ReimportingTheSameSlugUpdatesInPlaceRatherThanConflicting) {
  auto job_id_1 = insert_test_job(pool_, logger_);
  auto job_id_2 = insert_test_job(pool_, logger_);
  PostgresCategoryRepository repo(pool_, logger_);

  ASSERT_TRUE(repo.upsert(job_id_1, make_record("electronics")).has_value());
  auto first_id = (*repo.find_by_slug("electronics"))->id;

  // The whole point of upsert(): a duplicate slug must never surface as
  // ErrorCode::Conflict -- see ICategoryRepository's class comment.
  domain::NormalizedCategoryRecord updated{
      .name = "Electronics", .slug = "electronics", .description = "v2", .parent_slug = std::nullopt};
  auto second = repo.upsert(job_id_2, updated);
  ASSERT_TRUE(second.has_value()) << second.error().message();

  auto found = repo.find_by_slug("electronics");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_EQ((*found)->description, "v2");
  EXPECT_EQ(*(*found)->job_id, job_id_2);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u);
}

TEST_F(PostgresCategoryRepositoryTest, ListRespectsLimitAndOffsetInCreationOrder) {
  PostgresCategoryRepository repo(pool_, logger_);
  std::vector<std::string> slugs;
  for (int i = 0; i < 3; ++i) {
    auto job_id = insert_test_job(pool_, logger_);
    std::string slug = "cat-" + std::to_string(i);
    slugs.push_back(slug);
    ASSERT_TRUE(repo.upsert(job_id, make_record(slug)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].slug, slugs[0]);
  EXPECT_EQ((*page1)[1].slug, slugs[1]);

  auto page2 = repo.list(2, 2);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].slug, slugs[2]);
}

// Restart-persistence: a category upserted by one repository instance is
// visible, unchanged, to a second instance backed by the same connection
// pool/database.
TEST_F(PostgresCategoryRepositoryTest, PersistsAcrossRepositoryInstances) {
  auto job_id = insert_test_job(pool_, logger_);
  {
    PostgresCategoryRepository writer(pool_, logger_);
    ASSERT_TRUE(writer.upsert(job_id, make_record("electronics")).has_value());
  }

  PostgresCategoryRepository reader(pool_, logger_);
  auto found = reader.find_by_slug("electronics");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->name, "Electronics");
}

}  // namespace
}  // namespace flowforge::persistence::postgres
