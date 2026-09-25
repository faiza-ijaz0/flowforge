# Phase 3H — Production Readiness & Final Validation

Status: complete, including a follow-up pass (§0) that closed gaps the first pass left open and
corrected several of its claims. This document is both the baseline audit this phase started from
and the final production-readiness report it produced — written as one evolving document rather
than two, since every "final" section below is a direct answer to a "baseline" gap identified in §1.
Where the follow-up pass found an earlier statement to be wrong, the statement is kept and marked
**Corrected (§0)** rather than silently rewritten.

---

## 0. Follow-up pass (2026-09-25)

The first pass (commit `e11f863`) left five gaps open in its own "Known Limitations"/"Deferred
Work". The follow-up closed them and, along the way, found further real defects — two of which
contradicted claims made in this document.

### 0.1 Defects found and fixed

| # | Defect | How it was found | Fix | Evidence |
|---|---|---|---|---|
| 1 | **CI had never run.** `ci.yml` triggered on `push: branches: [main]`, but the repository's branch is `master`. The public GitHub API reported `total_count: 0` workflow runs — every "CI is authoritative" statement in Phases 3G/3H (sanitizers, PostgreSQL integration, format, Docker config) described a job that had never executed. | Reading `ci.yml` against the branch name and the GitHub Actions API | Trigger on `[master, main]` plus `workflow_dispatch` | `.github/workflows/ci.yml` |
| 2 | **Malformed IDs → HTTP 500** (the first pass's §13 finding, previously "documented, not fixed"). | First-pass security probe | `infra::is_uuid()`; all five path-ID routes (`GET /jobs/{id}`, `GET /jobs/{id}/attempts`, `POST /jobs/{id}/cancel`, `GET /workloads/{id}`, `GET /workloads/{id}/items`) return `400 validation_error` before touching the database | Re-probed against a live PostgreSQL-backed server: all six malformed requests (incl. `x' OR 1=1--`) → 400; a well-formed unknown UUID → 404; `flowforge_db_errors_total` stayed absent (0 DB errors); stderr clean. Tests: `IsUuidTest.*`, `HttpServerTest.MalformedJobIdIsRejectedWith400OnEveryJobRoute`, `WorkloadRoutesTest.MalformedWorkloadIdIsRejectedWith400` |
| 3 | **Same-submission category hierarchies failed nondeterministically.** Sibling jobs execute in parallel, so a child's job could look up its parent before the parent's job committed and fail *permanently* (a missing parent was non-retryable). The first pass's §6 "3-level hierarchy submitted together succeeds" was a timing coincidence. | Browser run (workload `567a62e2…`): root/child/grandchild in one CSV → **1 succeeded / 3 failed**; job timestamps show `laptops` failing its lookup at `.061` while `electronics` committed at `.080` | `confirm()` marks a category payload `parent_in_submission` when its parent slug is another record of the same submission; `CategoryProcessHandler` treats *that* missing immediate parent as **retryable**, so the existing retry engine re-runs it after backoff. A parent absent from the submission still fails immediately and non-retryably, as documented. No scheduler/worker-pool change. | Same browser flow after the fix (workload `dfb031b6…`): **3 succeeded / 1 failed** (only the orphan); `laptops`/`gaming-laptops` each succeeded on attempt 2. Test `MultiLevelHierarchyInOneSubmissionSucceedsRegardlessOfRecordOrder` (records deliberately child-first) passed 15/15 repeats. |
| 4 | **Duplicate natural keys within one submission silently collapsed.** Two records with the same SKU/slug/email became two jobs that upserted the same row; the preview reported both as valid. The Users CSV wizard already rejected in-file duplicate emails (user-import.md, "Duplicate rows"); the Processing Center did not. | Browser Categories-image run (workload `fd533b96…`): 100 jobs succeeded but only 96 rows linked to them — OCR produced the garbage slug `hhh` for 5 different rows | Preview (all three mapping functions) and confirm (independently — a caller can skip preview) reject later occurrences: "duplicate slug 'x' in this submission -- only the first occurrence is kept". Re-importing across submissions still upserts. | Browser preview now shows `Row 6: duplicate slug 'electronics-q3h' in this submission…`. Tests: `*MappingTest.RejectsDuplicate*`, `InputProcessingServiceTest.ConfirmRejectsDuplicate*` |
| 5 | **Dashboard Docker image could not build.** `Dockerfile.dashboard` copied `apps/dashboard/public`, which does not exist in the repository (`git ls-files` → 0 matches), so the `COPY --from=builder` fails. | Static review | Removed the line | `infra/docker/Dockerfile.dashboard` |
| 6 | **Dashboard container healthcheck would fail.** Next.js standalone binds to `$HOSTNAME`, which Docker sets to the container id; the `curl localhost:3000` healthcheck would not reach it. | Static review | `ENV HOSTNAME=0.0.0.0` | same |
| 7 | **`NEXT_PUBLIC_API_URL` was set only at container runtime**, where it has no effect — Next.js inlines it at build time. | Static review | Build `ARG` + compose `build.args` | `Dockerfile.dashboard`, `docker-compose.yml` |
| 8 | **Server image had no Tesseract**, so image/OCR processing would report "not supported" in Docker. | Static review (`tesseract_ocr_provider.cpp` probes `/usr/bin/tesseract`) | `tesseract-ocr` + `tesseract-ocr-eng` in the runtime stage | `Dockerfile.server` |
| 9 | **No `.dockerignore`**: the dashboard build's `COPY . .` would copy host `node_modules` (possibly built for another OS) over the clean `npm ci` layer, plus multi-GB CMake build trees. | Static review | Added `.dockerignore` | `.dockerignore` |
| 10 | **`FLOWFORGE_TESSERACT_PATH` undocumented.** It is read via `std::getenv` directly (not `getenv_fn` in `config.cpp`), so the first pass's grep-based "all 17 variables documented" check missed it. | Grep for every `getenv` call site | Added to `.env.example`; `FLOWFORGE_CORS_ALLOWED_ORIGIN` made an explicit, overridable compose variable | `.env.example`, `docker-compose.yml` |
| 11 | `db-migrate.sh` could not run from Git Bash on Windows (native `psql.exe` cannot open `/c/...` paths, and MSYS rewrote the `\i` argument). | Running the new migration check locally | `cygpath -m` + `MSYS2_ARG_CONV_EXCL` in a Windows-only branch; no-op on Linux | `scripts/db-migrate.sh` |
| 12 | `scripts/db-migrate.sh` was committed as mode `100644` (not executable), yet CI's `postgres-integration` job invokes it as `./scripts/db-migrate.sh` — that step would have failed with "Permission denied" on its first run. Hidden only because CI never ran (#1). | `git ls-files -s` while staging | Mode set to `100755` (as are the two new test scripts) | `git ls-files -s scripts/` |
| 13 | **The C++ code had never compiled with CI's or Docker's compiler.** The first CI run ever (run `36030301097`, on `5d31a22`) failed the Build step of all five C++ jobs and the server image build with `no template named 'Result'` / `no member named 'unexpected' in namespace 'std'`: libstdc++ only exposes `std::expected` to clang ≥ 19; Ubuntu 24.04's default clang is 18 and Debian bookworm's is 14. Local builds used MinGW GCC (and local clang 19 compiles every translation unit cleanly with `-Werror`), so it never showed. | Job logs of the first CI run | CI jobs install and use `clang-19` (+ `libclang-rt-19-dev` for the sanitizers) on a pinned `ubuntu-24.04` runner; the server image builds and runs on `debian:trixie-slim` with `clang-19` | `.github/workflows/ci.yml`, `infra/docker/Dockerfile.server` |

### 0.2 Gaps closed

- **`tests/e2e` and `tests/integration` now contain runnable tests**, both wired into CI:
  `tests/integration/check-migrations.sh` (fresh apply, schema/constraint/FK/index assertions,
  idempotent re-run, and a deliberately broken migration that must abort non-zero, unrecorded, with
  no partial DDL) and `tests/e2e/smoke-test.py` (black-box HTTP smoke test of a running stack).
  The migration check is not vacuous: a fake runner reproducing the original `.ps1` bug (prints
  "done: 16 migration(s) applied", applies nothing) fails it. Both real runners pass it locally
  (`db-migrate.sh` via Git Bash, `db-migrate.ps1` via PowerShell).
- **Docker runtime validation is now a CI job** (`docker-validate`): `docker compose build`, start
  PostgreSQL, run the `migrate` service, `up --wait` server + dashboard (their healthchecks must
  pass), then `smoke-test.py --ocr --dashboard-url` against the containers. **Docker is still not
  installed in this development environment, so none of this has been run locally.** It first runs
  on the first CI run on `master` after this commit; that outcome is not recorded in this document
  because it postdates it.
- **CI result:** the first run after the trigger fix (run `36030301097`, on `5d31a22`) failed on
  the compiler toolchain (defect #13). After that fix, run `36032434131` on `732eefe` passed **all
  7 jobs**: C++ build + test (Debug, Release), ASan + UBSan, PostgreSQL integration (including
  the fresh-database migration check), clang-format, dashboard lint/typecheck/build, and Docker
  build + runtime smoke test (images built, stack started with migrations, healthchecks passed,
  smoke test with real OCR passed against the containers). Docker still has not been run locally.
- **All six flows browser-verified** (§0.3) — the first pass verified only the three CSV flows.

### 0.3 Browser verification (follow-up)

A real Chrome session against a locally built `flowforge_server` (PostgreSQL `flowforge` database,
Tesseract OCR) and the dashboard. Files were uploaded through the real `<input type=file>`.
**Method note:** the automation tool's synthesized pointer clicks did not land on the target/source
toggle buttons (its screenshot coordinate frame was 1568 px wide for a 1280 px viewport) — the same
"flakiness" the first pass reported. Inspecting the DOM showed the page fully hydrated, and
`element.click()` changed React state normally, so the buttons were driven with DOM `click()`,
which invokes the same React `onClick` handlers a user's click does. This is not a dashboard defect.

| Flow | Preview (browser) | Workloads during preview | Confirm → workload | Jobs | Execution | PostgreSQL | UI list page |
|---|---|---|---|---|---|---|---|
| Products image (`products_bulk_100.png`) | 100 records, 100 valid, 0 invalid, ~85% OCR confidence | 22 → 22 | `9508e65f…` | 100 (100 distinct) | 100 succeeded, 0 failed | 100 `products` rows linked to the workload's jobs | `/products` row `PROD100` links to job `8525cd97…`, which PostgreSQL confirms is in `9508e65f…` |
| Categories image (`categories_bulk_100.png`) | 100 records, 100 valid, 0 invalid, ~64% confidence (low-confidence warning shown) | 23 → 23 | `fd533b96…` | 100 (100 distinct) | 100 succeeded, 0 failed | 96 rows: 5 records shared the OCR slug `hhh` → defect #4 | `/categories` shows `category-003` ("Category 0@3", an OCR-misread name) |
| Users image (`user_table_bulk_100.png`) | 100 records, 97 valid, 3 invalid (rows 20/40/100: OCR-garbled email), ~77% confidence | 27 → 27 | `bb165541…` | 97 (97 distinct) | 97 succeeded, 0 failed | `users` 95 → 192 (+97), 97 rows linked to the workload's jobs | `/users` row `personl@example.com` links to job `7ec7091e…`, in `bb165541…` |
| Categories hierarchy CSV, before fix #3 | 4 valid, 3 invalid (row 4 self-parent, row 6 in-file duplicate slug, row 7 blank name) | 24 → 24 | `567a62e2…` | 4 | **1 succeeded, 3 failed** (race) | only the root persisted | — |
| Categories hierarchy CSV, after fix #3 | same 4 valid / 3 invalid | 25 → 25 | `dfb031b6…` | 4 | 3 succeeded, 1 failed (orphan: "parent category 'does-not-exist-q3h' does not exist") | root / child / grandchild persisted with correct `parent_slug` | `/categories` shows all three with their parents |
| Duplicate slug across submissions (upsert) | 1 valid | — | `e1b069e3…` | 1 | 1 succeeded | `electronics-q3h` renamed in place: still 1 row, `updated_at` > `created_at` | `/categories` shows the new name |

The Products-image SKUs had first been inserted minutes earlier by a `smoke-test.py --ocr` run
against the same database, so the browser run upserted those 100 rows (re-linking each to its new
job) rather than growing the table; the `/products` total (697) did not change because of it.

**OCR data quality (observed, not fixed):** OCR misreads that still pass validation become "valid"
records — `PRODS` for `PROD5`, `Knife1e` for `Knife10`, `personl@example.com` for
`person1@example.com`, `Category 0@3` for `Category 003`. They are visible in the preview table,
which is the only review step before they are persisted. See Known Limitations.

### 0.5 Release pass (Phase 3I, v1.0.0)

The final regression run before tagging found two more issues:

| # | Issue | Evidence | Resolution |
|---|---|---|---|
| 14 | **Lost-update race on dispatch (core engine).** `WorkloadService`, `POST /api/v1/jobs`, and `RetryDispatcher` all scheduled a job *before* persisting `Queued`. `mark_queued()` is a read-modify-write, so a worker could finish the job between that read and write, and the late `Queued` write then overwrote the finished row. Present since the workload model was introduced; timing-dependent. | `HundredCategoryCsvFlowReconcilesAgainstRealPostgres` failed once: workload stuck at 94/95, one job `queued` with `attempt_count 0`, while `job_attempts` recorded a successful attempt (`.579`–`.581`) and the job row was written as `queued` at `.583`. | `Queued` is now persisted before `schedule()`; a rejection restores the previous row (`JobService::revert_queued`, or the original `Retrying` row with its `updated_at`). 4 regression tests use a probe scheduler that records the persisted status at the moment of scheduling: the two ordering tests **fail on the old code and pass on the fix**. After the fix, the three 100-record CSV acceptance tests and the hierarchy test each passed 10/10 repeats. |
| 15 | **Windows only: mismatched threading runtime DLL.** With PostgreSQL's `bin` ahead of the compiler's on `PATH`, test binaries loaded PostgreSQL's bundled `libwinpthread-1.dll` (52 KB; the toolchain's is 94 KB). `ThreadPoolTest.RunsAllSubmittedTasks` deadlocked: `gdb` showed every thread blocked inside that DLL (the main thread in `pthread_cond_signal`, after `BlockingQueue::push` had released its mutex). | Hung process inspected with `gdb` (5 threads, CPU time frozen for 10+ minutes). | Environment issue, not a FlowForge bug: with the toolchain first on `PATH`, the 53 concurrency tests passed 30/30 repeats. The required `PATH` order is now documented in the README and `docs/development/getting-started.md`. Linux/CI is unaffected. |

Release-pass results: 654 backend tests (551 engine + 103 server) passed against PostgreSQL with
Tesseract, 0 skipped; `tests/e2e/smoke-test.py --ocr` passed against a local PostgreSQL-backed
server (queued = started = succeeded = 195 in `/metrics`); dashboard lint/typecheck/build and
clang-format clean.

### 0.4 Test counts (follow-up)

| Suite | Count | Result |
|---|---|---|
| `flowforge_engine_tests` (`FLOWFORGE_TEST_DATABASE_URL` → real PostgreSQL, Tesseract present) | 547 | 547 passed, 0 failed, 0 skipped |
| `flowforge_server_tests` (same) | 103 | 103 passed, 0 failed, 0 skipped |
| **Total backend** | **650** | **650 passed** (637 before this pass, +13 new) |
| `tests/integration/check-migrations.sh` | 7 checks × 2 runners | passed (bash and PowerShell runners, disposable scratch schemas) |
| `tests/e2e/smoke-test.py --ocr` against a local PostgreSQL-backed server | 4 checks | passed |
| Dashboard `eslint .` / `tsc --noEmit` / `next build` | — | clean / clean / 17 routes compiled |
| `clang-format --dry-run --Werror` (local clang-format 19.1.1) | all `.cpp`/`.hpp` | clean |

---

## 1. Baseline audit

| Area | Current state (start of phase) | Gap | Planned action | Evidence |
|---|---|---|---|---|
| Users persistence | `UserProcessHandler` validated/normalized but wrote nothing beyond the job's own row — the only domain of Users/Products/Categories without a dedicated table | Real product inconsistency (Phase 3G's dashboard copy had to specially call out "unlike Products/Categories...") | Implement, mirroring Products/Categories exactly (§2) | `engine/include/flowforge/handlers/user_process_handler.hpp` (old, pre-3H version) |
| `tests/e2e` / `tests/integration` | README-only placeholders, one dated back to before the scheduler existed | Misleading — a reader would conclude no integration/e2e coverage exists | Rewrite both READMEs to map to the real coverage that already exists in `apps/server/tests`/`engine/tests` (§3) | Phase 3G audit §5 |
| Docker validation | Phase 3G could not run Docker locally | Unknown whether this environment now has it | Re-check; static-validate if still absent (§10) | this phase, Step 10 |
| Products/Categories 100+ acceptance | Automated Postgres bulk tests existed (`ProcessRoutesBulkPostgresTest`) but no browser-verified run was on record | Volume claim rested on automated tests only | Real browser run at 100+ scale for both (§ Browser verification below) | this phase |
| Auth/authz | None anywhere in the codebase | Undocumented as an explicit product decision | Document the trust model explicitly (§12) | `grep` across `apps/server/src`, `engine/` found zero auth/session/JWT code |
| Production env config | `.env.example` covers 17 vars; `AppConfig::load` requires `FLOWFORGE_DATABASE_URL` outside dev/test | Not yet cross-checked for completeness | Diff `.env.example` against every `getenv_fn(...)` call site (§11) | this phase |
| Remaining security gaps | Phase 3G's audit found the baseline solid (CORS, upload caps, magic-byte validation, parameterized SQL) but did not run adversarial probes | Untested against actual malformed/adversarial input | Run a focused probe battery against a live server (§13) | this phase |
| Performance/load coverage | None — no baseline had ever been measured and recorded | No documented throughput/latency numbers | Measure representative scenarios using real data (§14, §Performance Baseline) | this phase |
| Backup/recovery | Not documented anywhere | Operators have no guidance | Write `docs/operations/backup-and-recovery.md` (§19) | this phase |
| Observability readiness | `/metrics` exists and is rich (Phase 2B-5) but was never checked for internal consistency | Unverified | Cross-check counters against a real run's known quantities (§17) | this phase |

---

## 2. Users persistence decision

**Decision: implement.** Evidence-based, not a coin flip:

1. The Product/Category persistence pattern (repository interface, in-memory + Postgres
   implementations, `upsert()` keyed by a natural unique field, paginated read API) was already
   proven twice (Phases 3E/3F) and is entirely mechanical to replicate for a third domain — low
   risk, not a novel design decision.
2. `handlers::ProductProcessHandler`'s own header comment stated the reason Users lacked this was
   simply "no such requirement exists" *as of Phase 3E* — a point-in-time statement, not a
   permanent architectural constraint.
3. Every other domain's dashboard page is a real, persisted, browsable list. The `/users` page was
   the one exception — an import wizard with no way to see what was actually imported — which is a
   completeness gap, not a deliberate product boundary.
4. The task's own acceptance criteria (migration, repository interface, in-memory + Postgres
   implementations, parameterized queries, indexes, uniqueness, read API, workload/job
   relationship, tests) describe exactly the Product/Category pattern.

### What was built

- **Migration** `database/migrations/0016_create_users.sql` — `users` table: `id` (UUID PK),
  `name`, `email` (`UNIQUE`), `phone` (nullable), `job_id` (nullable FK to `jobs`,
  `ON DELETE SET NULL`), `created_at`/`updated_at`. Indexes: `idx_users_created_at` (pagination),
  `idx_users_job_id` (provenance lookup). Mirrors `products`/`categories` exactly.
- **Domain model** `engine/include/flowforge/domain/user.hpp` — `domain::User`, the persisted
  read-model counterpart of `NormalizedUserRecord`.
- **Repository** `IUserRepository` (`upsert`/`find_by_email`/`list`/`count`), `InMemoryUserRepository`,
  `PostgresUserRepository` — identical shape to `IProductRepository`.
- **Handler** `UserProcessHandler` now takes a constructor-injected `IUserRepository` and upserts
  after validation, exactly like `ProductProcessHandler`. It moved out of `register_builtin_handlers`
  (which only holds dependency-free handlers) and is registered separately in `App::create`,
  identically to Product/Category.
- **API** `GET /api/v1/users?limit=&offset=` (paginated, with `total` — no `POST`; a user row is
  always "confirm an import", never a direct write, matching Products/Categories).
- **Dashboard** `/users` now shows a real, paginated, persisted user list below the import wizard
  (same table pattern as `/products`), with a link back to each row's originating job.
- **Tests**: `InMemoryUserRepositoryTest` (5 tests), `PostgresUserRepositoryTest` (6 tests, real
  DB), `UserProcessHandlerTest` gained 2 persistence-specific cases, `WorkloadServiceTest`'s
  fixture updated (it needed a real `UserProcessHandler` registration to keep scheduling
  `user.process` jobs successfully), plus 3 new HTTP-level tests
  (`CsvUsersPersistsRealUsersAfterExecution`, `GetUsersReturnsEmptyListInitially`,
  `GetUsersSupportsPagination`) and a new 100-record bulk Postgres acceptance test
  (`HundredUserCsvFlowReconcilesAgainstRealPostgres`, using a new deterministic fixture
  `engine/tests/fixtures/users_bulk_100.csv`).

### What was deliberately not built

No `PUT`/`PATCH`/`DELETE` on `/api/v1/users` — matches Products/Categories, and nothing in the
product's current contract asks for direct user record editing outside re-import.

---

## 3. Real integration/E2E test suite

See `tests/integration/README.md` and `tests/e2e/README.md` (rewritten this phase) for the full
coverage map. Summary: FlowForge's integration/e2e coverage already lived in `apps/server/tests`
and `engine/tests` — GoogleTest/CTest is the project's integration-test tooling, and a separate
framework was not introduced (per the phase brief). What Phase 3H added:

- A new deterministic 100-row Users CSV fixture and bulk Postgres acceptance test (previously
  Users only had an image-based 100-row bulk test, not CSV).
- 3 new HTTP-level Users persistence tests.
- Persistence-verification lines appended to the existing Users image bulk test (it previously
  reconciled jobs/workload counts only, not the `users` table).

All items A–L from the phase brief's Step 3 checklist are mapped to specific existing test files in
`tests/integration/README.md`'s coverage table — reproduced here for completeness:

| Item | Covered by |
|---|---|
| A. API → Workload → Jobs | `workload_routes_test.cpp`, `workload_service_test.cpp` |
| B. Scheduler → WorkerPool → Executor | `execution_model_acceptance_test.cpp`, `local_worker_pool_test.cpp`, `job_executor_test.cpp` |
| C. PostgreSQL persistence | `engine/tests/persistence/postgres/*_repository_test.cpp` (8 files) |
| D. Retry lifecycle | `retry_dispatcher_test.cpp`, `retry_engine_acceptance_test.cpp`, `postgres_retry_engine_acceptance_test.cpp` |
| E. Dead-letter lifecycle | `job_executor_test.cpp`, `postgres_retry_engine_acceptance_test.cpp` |
| F. Workload reconciliation | all 6 `*BulkPostgresTest` tests |
| G. Processing API | `process_routes_test.cpp` (60+ tests) |
| H. Preview does not persist | asserted in every bulk test (`workloads_before`/`workloads_after` equality) |
| I. Confirm persists | `Confirm*Persists*` tests per domain |
| J. Users processing | `user_process_handler_test.cpp`, `user_import_routes_test.cpp`, new Users tests |
| K. Products processing | `product_process_handler_test.cpp`, `process_routes_test.cpp` Products section |
| L. Categories processing | `category_process_handler_test.cpp`, `process_routes_test.cpp` Categories section (incl. hierarchy) |

---

## 4. 100+ record automated acceptance — measured results

All six bulk tests run against a real PostgreSQL database (`flowforge_test`) and, for image
sources, a real local Tesseract OCR binary — never a mocked/fake OCR response. Run directly:

```
--gtest_filter=*Bulk*:*Hundred*
```

| Test | Records | Valid | Invalid | Duration | Reconciliation |
|---|---|---|---|---|---|
| `HundredUserCsvFlowReconcilesAgainstRealPostgres` | 100 | 95 | 5 | 4.3s | submitted=accepted+rejected ✓, accepted=jobs=succeeded ✓, `users` table ≥95 ✓ |
| `HundredRecordImageFlowReconcilesAgainstRealPostgres` (Users, image) | 100 | ≥80 (OCR-dependent) | — | 10.8s | same invariants ✓, `users` table ≥ valid ✓ |
| `HundredProductCsvFlowReconcilesAgainstRealPostgres` | 100 | 95 | 5 | 2.0s | same invariants ✓, `products` table ≥95 ✓ |
| `HundredProductImageFlowReconcilesAgainstRealPostgres` | 100 | ≥80 (OCR-dependent) | — | 5.2s | same invariants ✓ |
| `HundredCategoryCsvFlowReconcilesAgainstRealPostgres` | 100 | 95 | 5 | 2.3s | same invariants ✓, `categories` table ≥95 ✓ |
| `HundredCategoryImageFlowReconcilesAgainstRealPostgres` | 100 | ≥80 (OCR-dependent) | — | 5.6s | same invariants ✓ |

Every CSV test asserts *exact* equality (not "at least") because the CSV fixtures are fully
deterministic (5 invalid rows at fixed positions 17/34/51/68/85 across all three domains). Image
tests assert `>= 80` valid records because OCR yield is not bit-for-bit deterministic across
Tesseract versions/fonts — this is an honest bound, not a weakened assertion (see
`docs/architecture/input-processing.md` for the OCR reconstruction algorithm).

No duplicate jobs (`unordered_set<job_id>` size checked against expected count in every test), no
silent record loss (rejected count is asserted, not just "fewer than submitted"), no unexpected
workload duplication (`workloads_before`/`workloads_after` count delta checked).

---

## 5–7. Products / Categories / Users 100+ browser acceptance

**Browser automation was available this phase** (Claude in Chrome) and was used for real
verification against a running `flowforge_server` + PostgreSQL + dashboard stack.

### Products (Step 5)

Processing Center → Products → CSV → `products_bulk_100.csv` (the same deterministic fixture the
automated test uses) → Preview showed **95 valid / 5 invalid** (client-reported, matching the
server) → Confirm → real workload created → polled to **95/95 succeeded, 0 failed** → `/products`
list page showed the new rows with real SKU/name/price/job-link data (total climbed to 597,
consistent with prior test data already in the dev database).

### Categories (Step 6)

100-row volume: same flow, `categories_bulk_100.csv` → 95 valid / 5 invalid → confirmed → **95/95
succeeded, 0 failed**.

Hierarchy-specific cases (root/child/multi-level/self-parent/missing-parent/duplicate-slug): the
dashboard's target-selector button exhibited reproducible click-automation flakiness in this
session (a click via both element-reference and raw-coordinate targeting sometimes failed to
register the React state change, even after the 100-row Products/Users runs succeeded through the
identical selector). Rather than claim an unreliable browser interaction as verification, these
cases were verified via direct calls to the same `POST /api/v1/process/confirm` endpoint the
dashboard itself calls (already proven, in this same session, to be exactly what the UI drives) —
real server, real PostgreSQL, not a mock:

- **Root category**: `Electronics` (no parent) → succeeded.
- **Child**: `Laptops` (parent=`electronics`) → succeeded, submitted in the *same batch* as its parent.
- **Multi-level (grandchild)**: `Gaming Laptops` (parent=`laptops`) → succeeded, also same batch.
  **Corrected (§0, defect #3):** this outcome depended on job timing; nothing guaranteed
  parent-before-child. The follow-up reproduced the opposite outcome in the browser
  (1 succeeded / 3 failed) and fixed it; same-submission hierarchies now resolve via retry.
- **Self-parent**: rejected **at confirm time** (`"a category cannot be its own parent"`) — never
  became a job.
- **Missing parent**: accepted at confirm time, **failed at job execution** with
  `"parent category 'does-not-exist-h1' does not exist -- import it first, then re-import this
  record"` — workload correctly reached status `failed` (3 succeeded, 1 failed, matching the
  4 accepted records).
- **Duplicate slug (upsert)**: re-submitting `electronics-h1` with a changed name updated the
  existing row in place — `GET /api/v1/categories` confirmed exactly one row for that slug
  afterward, name updated, no duplicate.

No silent loss in any case: every rejected/failed record had a specific, real reason.

### Users (Step 7)

CSV: `users_bulk_100.csv` uploaded through the actual `/users` import wizard → client preview
showed 100 rows/95 valid/5 invalid → server confirmed the same exact split → workload succeeded
95/95 → **`/users` page showed 95 real persisted rows** (name/email/phone/job link/timestamp),
confirming Step 2's persistence implementation end-to-end through the real UI, not just the API.

Image (OCR): covered by the automated `HundredRecordImageFlowReconcilesAgainstRealPostgres` test
(§4) — not re-run through the browser separately this phase, since the CSV run already proved the
UI→API→execution→persistence chain, and OCR extraction quality is identical whether invoked from
the browser's multipart upload or the test's; browser automation adds no additional signal there.

Preview-is-side-effect-free was verified in both the automated suite (workload count assertions)
and observed live (workload count on `/workloads` did not change during any preview step).

---

## 8. Retry / failure / dead-letter acceptance

Not re-implemented — already real, already tested, verified passing this phase:

- `Running -> Failed`: `JobExecutorTest.NonRetryableFailureGoesStraightToFailedRegardlessOfAttemptsRemaining`
- `Running -> Retrying -> Queued -> Running -> Succeeded`: `RetryEngineAcceptanceTest.JobFailsOnceThenRetrySucceedsThroughRealPipeline`, `PostgresRetryEngineAcceptanceTest.JobFailsOnceThenRetrySucceedsAgainstRealPostgres`
- `Running -> Retrying -> ... -> DeadLetter`: `RetryEngineAcceptanceTest.PermanentlyFailingJobReachesDeadLetterAfterMaxAttempts`, `PostgresRetryEngineAcceptanceTest.PermanentlyFailingJobReachesDeadLetterAgainstRealPostgres`
- Workload progress correctness during retries: `WorkloadServiceTest` + Phase 3G's
  `retrying_items`/`dead_letter_items` sub-counts (verified via
  `GetWorkloadDistinguishesRetryingFromNeverRunViaSubCount`)
- No duplicate retry dispatch: `RetryDispatcherTest.DoesNotDuplicateScheduleOnASecondTickOnceQueued`
- Terminal states stop polling: `WorkloadProgressPanel`'s `TERMINAL_STATUSES` set (dashboard,
  Phase 3G) and `RetryDispatcher` only ever queries `Retrying`-status jobs (never touches a
  terminal one again)

All passing in this phase's full test run (§Test Matrix).

---

## 9. Database migration validation

**A real bug was found and fixed this phase.** `scripts/db-migrate.ps1` (the Windows migration
runner) silently reported success on every migration while actually applying none of them, when
run against a genuinely fresh database:

1. `psql -t -A`'s empty result (the normal case for "is this migration already applied" against a
   just-created `schema_migrations` table) came back as PowerShell `$null`, and `.Trim()` on that
   threw — **fixed** by piping through `Out-String` (verified more reliable than a `[string]`
   cast, which did not reliably coerce `$null` to `""` in this PowerShell version).
2. `psql`'s `\i` meta-command mis-parses a Windows backslash path (`C:\FlowForge\...`), producing a
   bizarre `"C:: Permission denied"` failure — **fixed** by converting to forward slashes before
   interpolating (PostgreSQL/psql accept forward slashes natively on Windows).
3. **The real bug**: neither failure above stopped the script, because a native command's non-zero
   exit code does not automatically become a PowerShell terminating error — `$LASTEXITCODE` must be
   checked explicitly, which the script never did. It printed `"apply 0001_..."` for all 16
   migrations and `"done: 16 migration(s) applied"` while creating **zero actual tables** — **fixed**
   by checking `$LASTEXITCODE` after each `psql` invocation and aborting with a clear error if
   non-zero.

This is Windows-only (`db-migrate.sh`, used by CI's Linux runners, was never affected — confirmed
by reading `.github/workflows/ci.yml`, which runs the `.sh` script successfully in the
`postgres-integration` job). Anyone developing on Windows who ran `db-migrate.ps1` before this fix
would have seen a false "success" with an empty database.

**Verified after the fix**, against disposable throwaway databases (created and dropped this
session, never touching `flowforge`/`flowforge_test`):

- **Fresh database**: all 16 migrations applied for real — `\dt` confirmed 13 real tables
  (`schema_migrations` + 12 domain tables), `schema_migrations` correctly listed all 16 versions.
- **Upgrade (idempotent re-run)**: re-running against the now-migrated database applied 0
  migrations, skipped all 16 — correct.
- **Upgrade (incremental)**: a database pre-seeded through migration 0015 correctly applied only
  migration 0016 (`users`) on the next run — `\d users` confirmed the exact expected schema
  (columns, types, nullability, defaults, unique constraint, both indexes, the `ON DELETE SET NULL`
  foreign key).

Migration ordering, constraints, indexes, foreign keys, uniqueness, `ON DELETE` behavior,
timestamps, and nullable fields were all inspected directly via `psql \d` against the real,
freshly-migrated schema and matched every migration file's stated intent. No migration history was
rewritten — migration 0016 (`users`) is new, additive SQL, consistent with every prior migration's
convention.

---

## 10. Docker validation

**Docker was checked and remains unavailable in this environment** (`docker`/`docker compose`: command
not found) — same finding as Phase 3G. Per the phase brief, no runtime validation is claimed.

Static validation performed instead:
- `infra/docker/Dockerfile.server` / `Dockerfile.dashboard`: reviewed for correctness against the
  Users-persistence changes. Both use directory-level `COPY` (`COPY engine ./engine`,
  `COPY apps/server ./apps/server`), so every new file this phase added (migration 0016, the new
  domain/repository/handler/route/json files) is automatically included with no Dockerfile changes
  needed.
- Both Dockerfiles already run as a non-root user (`USER flowforge`/`USER dashboard`, unchanged
  from Phase 3G) and now have `HEALTHCHECK` instructions (added Phase 3G) hitting `GET /ready` and
  `GET /` respectively.
- `docker-compose.yml`: service names, port mappings, `depends_on`/`condition: service_healthy`
  ordering, and environment variable wiring reviewed line-by-line — no changes needed for the Users
  addition (no new service, no new port, no new environment variable).

**Corrected (§0, defects #5–#9):** the static review above missed five real defects, including one
that makes the dashboard image unbuildable (`COPY` of a non-existent `public/` directory). All are
fixed in the follow-up, and CI's `docker-validate` job now builds, starts, and smoke-tests the
stack.

**A real gap was found and is honestly documented, not fixed (out of scope for local static
review): neither this environment nor CI actually starts the Docker stack.** CI's `docker-validate`
job only runs `docker compose config --quiet` (schema/interpolation validation) — it has never
built an image, started a container, or exercised a healthcheck. This means the Dockerfiles'
`RUN`/`COPY`/`HEALTHCHECK` correctness has never been runtime-verified anywhere in this project's
history. This is a real, currently-unaddressed gap — see "Known Limitations" and "Deferred Work"
below.

---

## 11. Production configuration

Every environment variable the server actually reads (`grep -oP 'getenv_fn\("\K[^"]+' engine/src/infra/config.cpp`,
17 results) is present and documented in `.env.example`. **Corrected (§0, defect #10):** that grep
only covered `config.cpp`; `FLOWFORGE_TESSERACT_PATH` is read with `std::getenv` in the OCR provider
and was undocumented until the follow-up.
`.env.example` contains only placeholder local-dev credentials (`flowforge`/`flowforge`), clearly
labeled as such; `.gitignore` already excludes real `.env` files. `AppConfig::load` already refuses
to start in `staging`/`production` without `FLOWFORGE_DATABASE_URL` set (verified via
`AppConfigTest.ProductionRequiresDatabaseUrl`, passing). CORS is exact-origin-match only (never a
wildcard — `cors.cpp`'s `origin_is_allowed`), upload limits (2 MB CSV, 6 MB image, 8 MB payload
cap) are enforced server-side regardless of client hints. No changes were needed to `.env.example`
this phase.

---

## 12. Authentication / authorization

**FlowForge has no authentication or authorization anywhere** — confirmed by search
(`grep -rli "authorization|bearer|jwt|session|authenticate"` across `apps/server/src`,
`engine/include`, `engine/src` returned one doc-comment mentioning "authorization" in the abstract,
zero implementation). Every API endpoint is open to anyone who can reach the server.

**Current FlowForge deployment assumes a trusted/private environment unless authentication and
authorization are added.** This is not a defect to fix this phase — it is an explicit, documented
scope boundary (see README's Roadmap, "Later phases"). Operators deploying FlowForge outside a
trusted network (VPN, private subnet, or a reverse proxy that itself enforces auth) must add an
authentication layer before doing so; nothing in this codebase provides one.

---

## 13. Security testing

A focused probe battery was run against a live server this phase. Results:

| Probe | Result | Verdict |
|---|---|---|
| Malformed JSON body | 400 `validation_error`, clean message | ✓ |
| SQL injection string in `queue_name` (`x'; DROP TABLE jobs; --`) | 201, stored as literal data; `jobs` table intact afterward | ✓ parameterized queries hold |
| Unknown job ID (valid UUID) | 404 `not_found` | ✓ |
| Malformed UUID as job ID | **500 `database_error`** (generic message, no leak) | ⚠ see below |
| Path traversal in ID segment | 404 (routing never touches a filesystem path) | ✓ |
| Oversized JSON payload (300 KB, over 256 KB cap) | 400, exact limit quoted | ✓ |
| Excessive pagination (`limit=99999999`) | 200, server-clamped to the documented cap | ✓ |
| Non-numeric pagination params | 200, falls back to default | ✓ |
| Invalid processing `target` | 400 `validation_error`, names the bad value | ✓ |
| Unknown workload ID | 404 | ✓ |
| Corrupt image (wrong magic bytes, `.png` extension) | 400, "unsupported or corrupt image" | ✓ magic-byte check, not extension-trust |
| Oversized image (7 MB, over 6 MB cap) | 400, exact limit quoted | ✓ |
| Malformed CSV (wrong field count) | 201 with `invalid_records:1`, specific reason, no job created for the bad row | ✓ |
| Unknown/invalid `job_type` on direct job creation | 201, job created but `scheduled:false` with a clear reason — never silently dropped | ✓ |
| Invalid `retry_policy.max_attempts` (string instead of int) | 400, names the field | ✓ |
| XSS-style string (`<script>...`) in `queue_name` | 201, stored/returned as inert JSON string data (never executed/rendered as HTML server-side) | ✓ |

**One real finding**: a malformed (non-UUID-shaped) ID passed to `GET /api/v1/jobs/{id}` returns
**HTTP 500** (`database_error`, generic message) instead of a client-error status. Root cause: the
malformed string reaches a parameterized `WHERE id = $1` query against a `UUID`-typed column, and
PostgreSQL itself rejects the cast, which the existing error-mapping layer classifies as a database
error. This is **not a security vulnerability** — no information leak (the response is the same
generic scrubbed message every 5xx gets), no crash, no injection, bounded resource use — but it is
an API-contract robustness issue (a malformed client ID should ideally be a 400, not a 500).
**Fixed in the follow-up pass (§0, defect #2).** Original rationale for deferring, kept for the
record: doing so correctly would mean adding UUID-shape validation across every ID-accepting route (jobs, workloads, products would need it too if they exposed a
similar lookup), which is more surface area than a "fix only verified issues" pass should absorb
this late without full re-test coverage. Documented here and in "Known Limitations".

The server remained healthy (`/ready` all green) throughout every probe, and `stderr` logs
contained zero stack traces, secrets, or credential material after the full battery.

---

## 14. Performance baseline

Representative scenarios, measured against a real server + real PostgreSQL (`flowforge` dev
database), no synthetic/estimated numbers:

| Scenario | Result |
|---|---|
| 100 CSV records (Products) | preview ~0.5s (extraction+validation only, isolated), confirm+execute+persist ~2.0s total (automated test) |
| 100 CSV records (Categories) | ~2.3s total |
| 100 CSV records (Users) | ~4.3s total |
| 500 CSV records (Products, manual run) | preview (extraction) **512 ms**; confirm (workload+500 jobs created+scheduled, synchronous HTTP call) **4164 ms**; execution-to-terminal (async worker pool + DB persistence, 4 workers) **578 ms**; total **~5.3s** |
| 100 image/OCR records (Users) | ~10.8s total — real Tesseract OCR extraction is the dominant cost here, not job execution |
| 100 image/OCR records (Products) | ~5.2s total |
| 100 image/OCR records (Categories) | ~5.6s total |

**OCR extraction time vs. execution time, separated** (from the 500-record manual Products run,
which has no OCR component to isolate the comparison cleanly): CSV extraction+validation for 500
rows took 512 ms; per-job execution (Postgres upsert + status transition) for 500 jobs, once
already scheduled, took 578 ms end-to-end via the 4-worker pool — i.e. roughly **1.2 ms/record of
real execution+persistence work**, comparable to the extraction cost itself. The dominant cost at
this workload size is `WorkloadService::create_workload`'s synchronous create-then-schedule loop
(4.2s for 500 items, ~8.3 ms/item) — this is a real, measured characteristic, not a guess: each
item is a full create-then-schedule HTTP-equivalent round trip performed serially within one
request. For image sources specifically, real Tesseract OCR (a subprocess invocation) is
consistently the largest single component of total time (e.g. ~10.8s vs ~4.3s for CSV at the same
100-Users-record scale).

No scalability claims beyond what was measured (e.g. no extrapolation to 10,000+ records was
attempted or is implied by these numbers).

Final persisted-count reconciliation for the 500-record run: 500 submitted = 500 valid = 500 jobs
= 500 succeeded = 500 (of `products.total`) newly persisted, 0 failed, 0 duplicates.

---

## 15. Concurrency / backpressure

Not re-implemented — existing coverage (all passing this phase, and run under ASan+UBSan in CI):

- Queue saturation / backpressure rejection: `PrioritySchedulerCapacityTest.QueueCapacityIsRespected`,
  `BackpressureRejectionIncrementsDedicatedCounter`
- Concurrent workload/job submission: `PrioritySchedulerConcurrencyTest.ConcurrentProducersLoseNoJobs`,
  `LocalWorkerPoolTest.NoJobIsLostUnderConcurrentDispatch`
- Worker pool limits: `LocalWorkerPoolTest.DispatchQueueCapacityIsRespected`,
  `MultipleWorkersExecuteConcurrently`
- Retry submission under load: `RetryDispatcherTest.RespectsBatchSizeLimitPerTick`
- Shutdown behavior: `PrioritySchedulerConcurrencyTest.ShutdownWhileProducersActiveDoesNotDeadlockOrCrash`,
  `LocalWorkerPoolTest.StopDrainsQueuedWorkBeforeJoining`, `AppStartupFailureTest.RepeatedStartupFailureLeaksNoThreadsOrConnections`
- Connection pool concurrency: `ConnectionPoolTest.ConcurrentAcquireReleaseFromMultipleThreadsStaysConsistent`
- Metrics/registry concurrency: `InMemoryMetricsRegistryTest.ConcurrentIncrementsAreNotLost`
- Handler registry concurrency: `HandlerRegistryTest.ConcurrentLookupsAreSafe`,
  `ConcurrentExecutionOfSameStatelessHandlerIsSafe`, `RegistrationDuringConcurrentLookupsDoesNotRace`

No new sanitizer run was performed locally this phase (ASan/UBSan builds on this Windows/MinGW
environment are not configured — `docs/development/getting-started.md`'s compiler notes explain
why clang+libstdc++ has a known linking bug on this platform combination); CI's `cpp-sanitizers`
job (Ubuntu+clang) remains authoritative and unchanged by this phase's additions, which follow the
exact same patterns (mutex-guarded maps, no new raw threading) as the code they mirror.

---

## 16. Crash / recovery review

Not re-implemented — existing coverage, verified passing this phase:

- **API starts before PostgreSQL / PostgreSQL unreachable at startup**:
  `AppStartupFailureTest.UnreachablePostgresFailsCreateCleanlyAndPromptly` (fails `App::create`
  cleanly, no partial/zombie state) and `RepeatedStartupFailureLeaksNoThreadsOrConnections` (10
  repeated failed-startup attempts leak nothing).
- **`/health` vs `/ready` semantics**: `/health` is a pure liveness check (`{"status":"ok"}`,
  always 200 if the process is running) — deliberately independent of dependency state. `/ready`
  performs real, cheap, non-blocking checks (database/scheduler/worker_pool/retry_dispatcher) and
  returns **503**, never a lying 200, the moment any one is unavailable — confirmed by direct
  inspection of `health_routes.cpp` (unchanged this phase) and by this phase's own live server
  session, which returned `{"status":"ok", "checks": {...all "ok"...}}` at 200 throughout.
- **Database connection loss mid-run**: not independently re-tested against the shared local
  PostgreSQL instance this phase (doing so would have disrupted the same database other work in
  this session depended on) — existing `ConnectionPoolTest.CreateFailsClearlyForAnUnreachableDatabase`
  and the startup-failure tests above cover the connection-pool's failure-handling contract, which
  `database_healthy()` (used by `/ready`) reads from the same pool state.

---

## 17. Observability validation

`/metrics` output was captured live after a mixed workload (500 products + assorted job/security
probes) and checked for internal consistency:

- `flowforge_executor_jobs_started_total` (500) == `flowforge_executor_jobs_succeeded_total` (500) ✓
- `flowforge_worker_pool_jobs_submitted_total` (500) == `flowforge_worker_pool_jobs_completed_total` (500) ✓
- `flowforge_scheduler_jobs_dispatched_total` (500) == `flowforge_scheduler_jobs_scheduled_total` (500) ✓
- `flowforge_db_product_upserts_total` (500) matches the 500 products actually persisted ✓
- `flowforge_db_errors_total` (1) matches the single malformed-UUID 500 produced during security
  testing (§13) — traceable, not a mystery counter ✓
- `flowforge_scheduler_rejections_total` (1) matches the single unknown-`job_type` scheduling
  rejection produced during security testing ✓
- All names well-formed, consistent `flowforge_<component>_<noun>_total` convention for counters,
  plain names for gauges (`flowforge_worker_pool_active_workers`), `_count`/`_sum`/`_min`/`_max`
  suffixes for the one histogram (`flowforge_executor_execution_duration_ms`) — no malformed or
  duplicate names found.

No new metrics were added this phase — the existing vocabulary (Phase 2B-5) already covers every
category the phase brief asks for (HTTP requests/errors, job creation/rejection, queueing,
backpressure, worker completions, timeouts, retries, dead letters, retry candidates, DB pool state)
and terminal states are correctly and distinctly represented (`succeeded`/`dead_letter`/`timed_out`
each have their own counter, not folded into a generic "failed").

---

## 18. CI/CD audit

**Corrected (§0, defect #1): none of the jobs below had ever run** — the workflow triggered only on
pushes to `main`, and this repository's branch is `master` (GitHub reported 0 workflow runs). The
job-by-job review below describes what the jobs *would* do. The follow-up fixed the trigger, added
the migration check to `postgres-integration`, and turned `docker-validate` into a real build +
runtime smoke test.

`.github/workflows/ci.yml` reviewed job-by-job. All six jobs are real and none silently skip a
critical test:

- `cpp-build-test` (Debug + Release matrix): full `ctest` run, in-memory repositories only (no
  Postgres service in this job — PostgreSQL-backed tests `GTEST_SKIP()` here, which is expected
  and honestly reported by CTest as "skipped", not "passed").
- `cpp-sanitizers`: ASan+UBSan build, same skip behavior for the Postgres-backed subset (no
  service configured in this job either).
- `postgres-integration`: the one job with a real `postgres:16` service container — runs
  `scripts/db-migrate.sh` (the Linux script, unaffected by this phase's Windows-only `.ps1` bug)
  against it, then the **full** test suite including every PostgreSQL-backed test and all six bulk
  acceptance tests (gated only by whether a Tesseract binary is present on the runner image — it
  is, per the `apt-get install ... tesseract-ocr` step).
- `cpp-format`: `clang-format --dry-run --Werror` across every `.cpp`/`.hpp` — this phase's changes
  were formatted and verified clean before commit (§Repository cleanup).
- `frontend`: dashboard lint + typecheck + build — this phase's dashboard changes (Users list page,
  `listUsers` API client method, shared types) verified clean locally with the identical commands.
- `docker-validate`: `docker compose config --quiet` only (see §10 — this is a real, documented gap,
  not something this phase weakened; it was already this narrow before Phase 3H).

No job was modified this phase. No test is skipped without an explicit, visible reason (every skip
in this codebase is a `GTEST_SKIP()` call with a message, never a silently-omitted test).

---

## 19. Backup / recovery documentation

See `docs/operations/backup-and-recovery.md` (new this phase).

## 20. Deployment documentation

See `docs/operations/deployment.md` (new this phase).

## 21. Final API audit

`docs/api/reference.md` updated this phase: added the `GET /api/v1/users` section (mirroring
Products/Categories' documented shape), and the `workload_id`/`total` fields added in Phase 3G were
already documented there. Every route reviewed against its actual route-file implementation this
phase; no undocumented-but-implemented or documented-but-unimplemented routes found.

## 22. Final README

`README.md` updated this phase: Users now described as a persisted domain (was previously
documented, accurately at the time, as the one exception). Roadmap gained a Phase 3H entry.

## 23. Repository cleanup audit

See final `git status`/`git diff --stat` in the commit report. `grep` performed across every
changed file for `TODO|FIXME|console\.log|debugger` — zero matches introduced. No hardcoded
`localhost` URLs added outside existing, already-reviewed dev-default config patterns. No secrets,
API keys, or credentials added. `pnpm-lock.yaml` (pre-existing, unrelated, untracked) left
untouched. Temporary test artifacts (fixtures generated for manual security/performance probing,
throwaway databases) were all deleted before commit — the only new fixture kept is
`engine/tests/fixtures/users_bulk_100.csv`, which is real, committed test data backing the new bulk
acceptance test, not a scratch file.

## 24. Complete test matrix — exact counts

| Suite | Count | Result |
|---|---|---|
| `flowforge_engine_tests` (in-memory + PostgreSQL-backed) | 537 | 537 passed, 0 failed |
| `flowforge_server_tests` (in-memory + PostgreSQL-backed) | 100 | 100 passed, 0 failed |
| **Total backend tests** | **637** | **637 passed, 0 failed** |
| Dashboard lint (`eslint .`) | — | clean, 0 warnings/errors |
| Dashboard typecheck (`tsc --noEmit`) | — | clean |
| Dashboard build (`next build`) | 17 routes | clean, all routes compiled |
| `clang-format --dry-run --Werror` | all `.cpp`/`.hpp` under `engine`, `apps/server`, `benchmarks` | clean |

These counts were produced by an actual `ctest`/direct-binary run this session (not estimated) —
see the commit's final verification pass for the exact command output. **Superseded by §0.4**
(650 passing after the follow-up).

## 25. Final end-to-end matrix

| Flow | Input | Records | Preview | Confirm | Jobs | Execution | PostgreSQL | UI |
|---|---|---|---|---|---|---|---|---|
| Users CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ 95 rows persisted | ✓ browser-verified |
| Users Image | PNG (real OCR) | 100 (97 valid, 3 invalid) | ✓ browser (follow-up) + automated | ✓ browser + automated | ✓ 97 | ✓ 97/97 succeeded | ✓ 97 rows (users 95 → 192) | ✓ browser-verified (follow-up, §0.3) |
| Products CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ browser-confirmed (597 total) | ✓ browser-verified |
| Products Image | PNG (real OCR) | 100 (100 valid) | ✓ browser (follow-up) + automated | ✓ browser + automated | ✓ 100 | ✓ 100/100 succeeded | ✓ 100 rows linked (upserted, see §0.3) | ✓ browser-verified (follow-up, §0.3) |
| Categories CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ automated; hierarchy verified in browser (follow-up) | ✓ browser-verified (volume, first pass); hierarchy cases browser-verified in the follow-up (§0.3) |
| Categories Image | PNG (real OCR) | 100 (100 valid) | ✓ browser (follow-up) + automated | ✓ browser + automated | ✓ 100 | ✓ 100/100 succeeded | ✓ 96 distinct slugs (5 OCR duplicates → defect #4, now rejected at preview) | ✓ browser-verified (follow-up, §0.3) |

The CSV rows' browser runs are from the first pass; the image rows and the hierarchy cases are from
the follow-up (§0.3). The first pass's reasoning for skipping image flows in the browser ("OCR
quality is identical regardless of upload path, so the browser adds no signal") was wrong in
practice: the Categories-image browser run is what exposed defect #4. No "existing automated test" was reworded as "browser verified" anywhere in this
document.

## 26. Release checklist

See `docs/operations/release-checklist.md` (new this phase).

## 27. Final sections

### Executive Summary

FlowForge closed its last major domain-persistence gap (Users), gained real automated 100+ record
acceptance coverage across all three domains (both CSV and OCR sources), found and fixed a genuine
Windows-only migration-tooling bug via real fresh/upgrade database testing, ran a focused security
probe battery with one documented (non-critical) finding, measured a real performance baseline, and
produced complete operational documentation (deployment, backup/recovery, release checklist). No
architecture was rewritten; every change was additive, following patterns already proven twice in
this codebase.

The follow-up pass (§0) found that CI had never run (wrong branch trigger), fixed two data-integrity
defects that only real browser runs exposed (a same-submission category-hierarchy race and silently
collapsed in-submission duplicate keys), fixed the malformed-ID → 500 finding, fixed five Docker
defects (one made the dashboard image unbuildable), and put runnable tests in `tests/integration`
and `tests/e2e`, with CI now running them plus a real Docker build + runtime smoke test.

### Architecture Status

Unchanged from Phase 3G: C++ scheduler, worker pool, retry engine, PostgreSQL persistence, workload
model all intact, no rewrites, no new dependencies. Users persistence is the one structural
addition, and it is a peer of the existing Product/Category persistence, not a new pattern.

### Persistence Status

All three input domains (Users, Products, Categories) now have dedicated PostgreSQL tables with
real repositories, real migrations, real indexes, real uniqueness constraints, and real paginated
read APIs. Verified via 650 passing tests (§0.4) plus live browser/API sessions. Duplicate natural
keys within one submission are now rejected instead of silently collapsing onto one row (§0,
defect #4).

### Input Processing Status

Unchanged and confirmed working end-to-end for CSV and real-OCR-image sources across all three
domains, including deterministic 100+ record reconciliation and category hierarchy edge cases
(root/child/multi-level/self-parent/missing-parent/duplicate-slug). The multi-level case only
became reliable in the follow-up (§0, defect #3); all six source × target flows and every hierarchy
case are browser-verified (§0.3).

### Reliability Status

Retry, dead-letter, and workload-reconciliation behavior all verified via existing passing tests.
No duplicate jobs, no lost records, correct terminal-state handling, confirmed at 100–500 record
scale this phase.

### Security Status

Baseline remains solid (parameterized SQL, magic-byte validation, bounded uploads/payloads,
exact-match CORS, no secret leakage). The one probe finding (malformed UUID → 500 instead of 400)
is fixed and re-verified against a live PostgreSQL-backed server (§0, defect #2). No authentication/authorization exists —
explicitly documented as an assumed-trusted-environment deployment model, not silently omitted.

### Testing Status

650 backend tests passing (0 failed, 0 skipped, against real PostgreSQL with Tesseract present),
dashboard lint/typecheck/build clean, `clang-format` clean (§0.4). Six 100+ record bulk acceptance
tests against real PostgreSQL (and real Tesseract OCR for image sources) all passing. New runnable
tests in `tests/integration` (migration runner check) and `tests/e2e` (black-box stack smoke test).

### E2E Status

All six flows (Users/Products/Categories × CSV/image) are browser-verified at 100-record scale with
persisted-data confirmation in PostgreSQL and on the dashboard list pages — CSV in the first pass,
image/OCR in the follow-up — plus every category-hierarchy case in the browser (§0.3). Toggle
buttons were driven with DOM `click()` because the automation tool's pointer clicks missed them;
see the method note in §0.3.

### Performance Baseline

Real, measured numbers recorded for 100 and 500-record CSV flows and 100-record OCR flows across
all three domains (§14). No scalability claims beyond what was measured.

### Docker Status

Docker remains unavailable in this environment (confirmed, not assumed). **Docker runtime validation
unavailable in this environment.** The follow-up's static review found and fixed five defects the
first review missed (§0, #5–#9), and CI's `docker-validate` job now builds both images, starts the
stack with migrations, waits for the healthchecks, and runs the smoke test (with OCR) against the
containers. That job's first run happens after this document was written.

### CI/CD Status

The workflow had never run: it triggered on `main`, and the branch is `master` (§0, defect #1).
The follow-up fixed the trigger, added the migration-runner check to `postgres-integration`, and
replaced the config-only Docker job with a build + runtime smoke test. No check was weakened or
removed. Every job's first real execution is on the follow-up commit.

### Deployment Status

Documented in `docs/operations/deployment.md`. No actual production deployment was performed or is
claimed — this phase describes how to deploy, it does not deploy.

### Observability Status

`/metrics` output verified internally consistent against a real, mixed workload this phase. No new
metrics needed or added.

### Known Limitations

- No authentication/authorization anywhere — by design, documented (§12).
- Docker runtime validation exists only as a CI job (§0.2). Docker is not installed in this
  development environment, so it has never been run locally; until that job has run green on
  `master`, treat the Docker images as unverified.
- CI never executed before the follow-up (§0, defect #1). Every job — sanitizers and the
  PostgreSQL integration job included — runs for the first time on the follow-up commit, and a
  first run of a never-executed workflow may surface environment issues (e.g. Ubuntu clang vs. the
  local MinGW GCC toolchain).
- OCR misreads that remain syntactically valid are accepted as valid records (§0.3). The preview
  table is the only review step; no confidence threshold blocks a confirm.
- Same-submission category parents are resolved by retrying the child (§0, defect #3). A chain
  deeper than the job's retry budget (default 3 attempts) could, with worst-case timing, still end
  in `dead_letter` at its deepest levels; importing deep hierarchies level by level avoids this.
- A job that succeeds on a retry keeps the previous attempt's `last_error` text (seen on the
  hierarchy run's retried jobs). The status is correct; the leftover message can mislead.
- No distributed/multi-instance deployment story (`LocalWorkerPool`/`RetryDispatcher` remain
  single-process) — unchanged from prior phases, explicitly out of this phase's scope per the brief.

### Deferred Work

- Clearing `last_error` when a retried job succeeds (see Known Limitations).
- A minimum OCR-confidence gate at confirm time, if OCR-imported data must be trusted without
  human review.
- Authentication/authorization, if FlowForge is ever deployed outside a trusted network.

### Final Recommendation

FlowForge is **production-style** and portfolio-grade: tested (650 passing backend tests plus real
100+ record acceptance at the database level and in the browser), reasonably secure for a
trusted-environment deployment, observable, and documented. It is **not** "fully production ready"
in the unqualified sense: it has no authentication; its Docker images and its CI pipeline have not
yet completed a run (both first run on the follow-up commit); and it runs on a single-process
worker pool. Anyone deploying it outside a trusted, private network, or
at a scale requiring distributed workers, must treat those as explicit prerequisites, not assumptions.
