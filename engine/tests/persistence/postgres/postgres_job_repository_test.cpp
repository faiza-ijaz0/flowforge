#include "flowforge/persistence/postgres/postgres_job_repository.hpp"

#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;

domain::Job make_job(std::string queue = "integration-tests") {
  return domain::Job(infra::JobId::generate(), std::move(queue), R"({"hello":"world"})",
                     domain::RetryPolicy{}, std::chrono::system_clock::now());
}

using PostgresJobRepositoryTest = PostgresIntegrationTest;

TEST_F(PostgresJobRepositoryTest, InsertThenFindByIdRoundTripsAllFields) {
  PostgresJobRepository repo(pool_, logger_);
  domain::RetryPolicy policy;
  policy.max_attempts = 7;
  policy.initial_backoff = std::chrono::milliseconds{250};
  policy.max_backoff = std::chrono::milliseconds{30'000};
  policy.backoff_multiplier = 1.5;
  domain::Job job(infra::JobId::generate(), "emails", R"({"to":"a@example.com"})", policy,
                  std::chrono::system_clock::now(), /*priority=*/9);

  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->id(), job.id());
  EXPECT_EQ(found->queue_name(), "emails");
  EXPECT_EQ(found->payload(), R"({"to":"a@example.com"})");
  EXPECT_EQ(found->priority(), 9);
  EXPECT_EQ(found->status(), domain::JobStatus::Pending);
  EXPECT_EQ(found->attempt_count(), 0u);
  EXPECT_FALSE(found->last_error().has_value());
  EXPECT_EQ(found->retry_policy().max_attempts, 7u);
  EXPECT_EQ(found->retry_policy().initial_backoff.count(), 250);
  EXPECT_EQ(found->retry_policy().max_backoff.count(), 30'000);
  EXPECT_DOUBLE_EQ(found->retry_policy().backoff_multiplier, 1.5);
}

TEST_F(PostgresJobRepositoryTest, JobTypeRoundTripsThroughInsertAndUpdate) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job(infra::JobId::generate(), "emails", R"({"to":"a@example.com"})", domain::RetryPolicy{},
                  std::chrono::system_clock::now(), /*priority=*/0, /*job_type=*/"echo");
  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->job_type(), "echo");

  job.transition_to(domain::JobStatus::Queued, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(job).has_value());

  auto refetched = repo.find_by_id(job.id());
  ASSERT_TRUE(refetched.has_value());
  EXPECT_EQ(refetched->job_type(), "echo");
}

TEST_F(PostgresJobRepositoryTest, DefaultJobTypeIsEmptyString) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->job_type().empty());
}

TEST_F(PostgresJobRepositoryTest, InsertWithNonJsonPayloadRoundTrips) {
  // domain::Job::payload() is an opaque string, not guaranteed to be valid
  // JSON -- see the comment in postgres_job_repository.cpp. This is the
  // regression test for that: a payload that is plainly not JSON must
  // still round-trip byte-for-byte, not throw a jsonb cast error.
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job(infra::JobId::generate(), "integration-tests", "not-json-at-all: {broken",
                  domain::RetryPolicy{}, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->payload(), "not-json-at-all: {broken");
}

TEST_F(PostgresJobRepositoryTest, AutoProvisionsUnknownQueue) {
  // jobs.queue_name has a foreign key to queues.name, but nothing in this
  // phase creates queue rows ahead of time -- the repository must
  // transparently provision the queue row rather than fail the insert.
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job = make_job("brand-new-queue-" + infra::generate_uuid_v4());
  EXPECT_TRUE(repo.insert(job).has_value());
}

TEST_F(PostgresJobRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  PostgresJobRepository repo(pool_, logger_);
  auto found = repo.find_by_id(infra::JobId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST_F(PostgresJobRepositoryTest, DuplicateInsertReturnsConflictAndDoesNotPartiallyWrite) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  auto second = repo.insert(job);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);

  // The failed second insert must not have left a partial/duplicate row --
  // exactly one job with this id should exist. This is the transaction
  // rollback behavior: the failing INSERT statement's transaction commits
  // nothing.
  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->id(), job.id());
}

TEST_F(PostgresJobRepositoryTest, ListOrdersByCreationAndRespectsLimitOffset) {
  PostgresJobRepository repo(pool_, logger_);
  std::vector<infra::JobId> ids;
  for (int i = 0; i < 5; ++i) {
    domain::Job job = make_job();
    ids.push_back(job.id());
    ASSERT_TRUE(repo.insert(job).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].id(), ids[0]);
  EXPECT_EQ((*page1)[1].id(), ids[1]);

  auto page2 = repo.list(2, 4);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].id(), ids[4]);

  auto out_of_range = repo.list(2, 100);
  ASSERT_TRUE(out_of_range.has_value());
  EXPECT_TRUE(out_of_range->empty());
}

TEST_F(PostgresJobRepositoryTest, UpdatePersistsStatusAttemptCountAndLastError) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  job.record_attempt_failure("boom", std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status(), job.status());
  EXPECT_EQ(found->attempt_count(), 1u);
  ASSERT_TRUE(found->last_error().has_value());
  EXPECT_EQ(*found->last_error(), "boom");
}

TEST_F(PostgresJobRepositoryTest, UpdateUnknownJobReturnsNotFound) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job job = make_job();
  auto result = repo.update(job);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

// Phase 2B-4: engine::RetryDispatcher's real query -- proves it round-trips
// through actual PostgreSQL (not just InMemoryJobRepository), using the
// existing idx_jobs_status index (migration 0005, no new migration
// needed).
TEST_F(PostgresJobRepositoryTest, ListByStatusReturnsOnlyRetryingJobsOldestFirst) {
  PostgresJobRepository repo(pool_, logger_);
  domain::Job retrying_1 = make_job();
  domain::Job succeeded = make_job();
  domain::Job retrying_2 = make_job();
  ASSERT_TRUE(repo.insert(retrying_1).has_value());
  ASSERT_TRUE(repo.insert(succeeded).has_value());
  ASSERT_TRUE(repo.insert(retrying_2).has_value());

  retrying_1.record_attempt_failure("boom", std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(retrying_1).has_value());
  succeeded.record_attempt_success(std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(succeeded).has_value());
  retrying_2.record_attempt_failure("boom again", std::chrono::system_clock::now() + std::chrono::seconds(1));
  ASSERT_TRUE(repo.update(retrying_2).has_value());

  auto retrying = repo.list_by_status(domain::JobStatus::Retrying, 10);
  ASSERT_TRUE(retrying.has_value()) << retrying.error().message();
  ASSERT_EQ(retrying->size(), 2u);
  EXPECT_EQ((*retrying)[0].id(), retrying_1.id());
  EXPECT_EQ((*retrying)[1].id(), retrying_2.id());
  for (const auto& job : *retrying) {
    EXPECT_EQ(job.status(), domain::JobStatus::Retrying);
  }

  auto dead_letter = repo.list_by_status(domain::JobStatus::DeadLetter, 10);
  ASSERT_TRUE(dead_letter.has_value());
  EXPECT_TRUE(dead_letter->empty());
}

TEST_F(PostgresJobRepositoryTest, ListByStatusRespectsLimit) {
  PostgresJobRepository repo(pool_, logger_);
  for (int i = 0; i < 3; ++i) {
    domain::Job job = make_job();
    job.record_attempt_failure("boom", std::chrono::system_clock::now());
    ASSERT_TRUE(repo.insert(job).has_value());
  }

  auto limited = repo.list_by_status(domain::JobStatus::Retrying, 2);
  ASSERT_TRUE(limited.has_value());
  EXPECT_EQ(limited->size(), 2u);
}

TEST_F(PostgresJobRepositoryTest, DataSurvivesAcrossRepositoryInstances) {
  // Simulates a process restart: a second, independent repository object
  // (its own pool) must see data written by the first. This is the crux
  // of the phase's acceptance test at the repository layer.
  domain::Job job = make_job();
  {
    PostgresJobRepository repo(pool_, logger_);
    ASSERT_TRUE(repo.insert(job).has_value());
  }

  auto url = test::test_database_url();
  ASSERT_TRUE(url.has_value());
  auto second_pool =
      PgConnectionPool::create(PgPoolConfig{.connection_string = *url, .pool_size = 1}, logger_);
  ASSERT_TRUE(second_pool.has_value());
  PostgresJobRepository second_repo(*second_pool, logger_);

  auto found = second_repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->id(), job.id());
  EXPECT_EQ(found->payload(), job.payload());
}

}  // namespace
}  // namespace flowforge::persistence::postgres
