# tests/integration

This directory intentionally contains no test binary of its own. FlowForge's real integration
coverage — API endpoint → service → real PostgreSQL, exercised through the actual composition root
(`App::create`), not mocks — already exists and is wired into the normal CTest/CI run:

- **`apps/server/tests/*.cpp`** — HTTP-level integration tests. Every route file has a
  `*_routes_test.cpp` counterpart that boots a real `App` on an ephemeral port and drives it with a
  real `httplib::Client`. Most of these run against in-memory repositories (fast, no external
  service, run in CI on every push); a subset — every `*BulkPostgresTest` fixture in
  `process_routes_test.cpp`, plus `HttpServerPostgresPersistenceTest` in `http_server_test.cpp` —
  opt in to a real PostgreSQL database via `FLOWFORGE_TEST_DATABASE_URL`/`FLOWFORGE_DATABASE_URL`
  and `GTEST_SKIP()` (reported honestly, never silently passed) when neither is set.
- **`engine/tests/persistence/postgres/*.cpp`** — repository-level integration tests
  (`Postgres*RepositoryTest`) against a real database, using the same opt-in/skip convention (see
  `postgres_test_support.hpp`).
- **`engine/tests/services/*.cpp`** — service-level integration tests (workload creation,
  scheduling, CSV/user import, input processing) against the real `PriorityScheduler`, using
  in-memory repositories for speed (the repository is swapped for PostgreSQL at the layer below,
  which is what the Postgres-suffixed tests above exercise).

## Coverage map (Phase 3H)

| Area | Where |
|---|---|
| API → Workload → Jobs | `apps/server/tests/workload_routes_test.cpp`, `engine/tests/services/workload_service_test.cpp` |
| Scheduler → WorkerPool → Executor | `engine/tests/engine/execution_model_acceptance_test.cpp`, `local_worker_pool_test.cpp`, `job_executor_test.cpp` |
| PostgreSQL persistence | `engine/tests/persistence/postgres/*_repository_test.cpp` (one file per domain: job, workload, product, category, user, worker, workflow, execution) |
| Retry lifecycle | `engine/tests/engine/retry_dispatcher_test.cpp`, `retry_engine_acceptance_test.cpp`, `engine/tests/persistence/postgres/postgres_retry_engine_acceptance_test.cpp` |
| Dead-letter lifecycle | `engine/tests/engine/job_executor_test.cpp` (`RetryableFailureLandsOnDeadLetterWhenAttemptsExhausted` and friends), `postgres_retry_engine_acceptance_test.cpp` (`PermanentlyFailingJobReachesDeadLetterAgainstRealPostgres`) |
| Workload reconciliation | every `*BulkPostgresTest` in `apps/server/tests/process_routes_test.cpp` (see tests/e2e/README.md) |
| Processing API (preview/confirm) | `apps/server/tests/process_routes_test.cpp` |
| Preview does not persist | `ProcessRoutesTest.PreviewFailurePathCreatesNoWorkload`, `PreviewOfARealFixtureImageExtractsRecordsAndCreatesNoWorkload`, and the `workloads_before`/`workloads_before` assertion pattern used in every bulk test |
| Confirm persists | `ProcessRoutesTest.ConfirmCreatesARealWorkloadFromSubmittedRecords` and the domain-specific `Confirm*Persists*` tests |
| Users processing | `engine/tests/handlers/user_process_handler_test.cpp`, `apps/server/tests/user_import_routes_test.cpp`, `ProcessRoutesTest.CsvUsersPersistsRealUsersAfterExecution` |
| Products processing | `engine/tests/handlers/product_process_handler_test.cpp`, `ProcessRoutesTest.ConfirmCsvProductsCreatesOneWorkloadAndPersistsRealProducts` |
| Categories processing | `engine/tests/handlers/category_process_handler_test.cpp`, `ProcessRoutesTest.ConfirmCsvCategoriesCreatesOneWorkloadAndPersistsRealCategories`, plus the hierarchy tests (`ConfirmCategoryWithValidPreexistingParentSucceeds`, `ConfirmCategoryWithMissingParentIsAcceptedAtConfirmButFailsAsAJob`, and `domain::` tests `SelfParentIsRejected`/`DeeperCyclicParentChainIsRejected`/`MultiLevelParentChainIsAccepted`) |

A separate `tests/integration` binary would duplicate this coverage under a different name for no
benefit — GoogleTest/CTest already is the project's integration-test tooling (see the phase brief's
"prefer existing project tooling"); this directory exists so the coverage map above has a stable,
discoverable home rather than requiring a `grep` across the tree.
