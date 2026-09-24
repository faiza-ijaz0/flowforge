# Phase 3H — Production Readiness & Final Validation

Status: complete. This document is both the baseline audit this phase started from and the final
production-readiness report it produced — written as one evolving document rather than two,
since every "final" section below is a direct answer to a "baseline" gap identified in §1.

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
- **Multi-level (grandchild)**: `Gaming Laptops` (parent=`laptops`) → succeeded, also same batch —
  demonstrating the system correctly resolves a full 3-level hierarchy submitted together, not just
  when parents are pre-existing.
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
17 results) is present and documented in `.env.example` — no gaps found, no changes needed.
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
Deliberately **not fixed** this phase: doing so correctly would mean adding UUID-shape validation
across every ID-accepting route (jobs, workloads, products would need it too if they exposed a
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
see the commit's final verification pass for the exact command output.

## 25. Final end-to-end matrix

| Flow | Input | Records | Preview | Confirm | Jobs | Execution | PostgreSQL | UI |
|---|---|---|---|---|---|---|---|---|
| Users CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ 95 rows persisted | ✓ browser-verified |
| Users Image | PNG (real OCR) | 100 | ✓ automated only | ✓ automated only | ✓ | ✓ automated | ✓ automated | NOT VERIFIED in browser this phase — automated coverage only (see §7) |
| Products CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ browser-confirmed (597 total) | ✓ browser-verified |
| Products Image | PNG (real OCR) | 100 | ✓ automated only | ✓ automated only | ✓ | ✓ automated | ✓ automated | NOT VERIFIED in browser this phase — automated coverage only |
| Categories CSV | CSV | 100 (95 valid) | ✓ browser + automated | ✓ browser + automated | ✓ | ✓ 95/95 succeeded | ✓ automated + hierarchy verified via direct API | ✓ browser-verified (volume); hierarchy sub-cases via direct API, not browser click flow (§6) |
| Categories Image | PNG (real OCR) | 100 | ✓ automated only | ✓ automated only | ✓ | ✓ automated | ✓ automated | NOT VERIFIED in browser this phase — automated coverage only |

"Automated only" rows are genuinely, currently automated-and-passing (§4) — they are not marked
"NOT VERIFIED" as a whole, only the *browser* UI layer specifically was not re-driven for the image
sources this phase (CSV already proved the UI→API wiring; OCR quality is identical regardless of
upload path). No "existing automated test" was reworded as "browser verified" anywhere in this
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

### Architecture Status

Unchanged from Phase 3G: C++ scheduler, worker pool, retry engine, PostgreSQL persistence, workload
model all intact, no rewrites, no new dependencies. Users persistence is the one structural
addition, and it is a peer of the existing Product/Category persistence, not a new pattern.

### Persistence Status

All three input domains (Users, Products, Categories) now have dedicated PostgreSQL tables with
real repositories, real migrations, real indexes, real uniqueness constraints, and real paginated
read APIs. Verified via 637 passing tests plus live browser/API sessions this phase.

### Input Processing Status

Unchanged and confirmed working end-to-end for CSV and real-OCR-image sources across all three
domains, including deterministic 100+ record reconciliation and category hierarchy edge cases
(root/child/multi-level/self-parent/missing-parent/duplicate-slug), all verified this phase.

### Reliability Status

Retry, dead-letter, and workload-reconciliation behavior all verified via existing passing tests.
No duplicate jobs, no lost records, correct terminal-state handling, confirmed at 100–500 record
scale this phase.

### Security Status

Baseline remains solid (parameterized SQL, magic-byte validation, bounded uploads/payloads,
exact-match CORS, no secret leakage). One non-critical finding (malformed UUID → 500 instead of
400) documented, not fixed, with explicit rationale. No authentication/authorization exists —
explicitly documented as an assumed-trusted-environment deployment model, not silently omitted.

### Testing Status

637 backend tests passing (0 failed), dashboard lint/typecheck/build clean, `clang-format` clean.
Six 100+ record bulk acceptance tests against real PostgreSQL (and real Tesseract OCR for image
sources) all passing.

### E2E Status

Real browser verification performed for Users/Products/Categories CSV flows at 100+ record scale,
including persisted-data confirmation in the dashboard UI. Category hierarchy edge cases verified
via direct API calls to the same endpoint the UI uses (browser click automation was flaky on the
target-selector control this session — documented honestly rather than claimed). Image/OCR flows
verified by automated test only, not re-driven through the browser this phase.

### Performance Baseline

Real, measured numbers recorded for 100 and 500-record CSV flows and 100-record OCR flows across
all three domains (§14). No scalability claims beyond what was measured.

### Docker Status

Docker remains unavailable in this environment (confirmed, not assumed). Static validation
performed on both Dockerfiles and docker-compose.yml. A real, honestly-documented gap: neither this
environment nor CI has ever performed runtime Docker validation (container build, startup,
healthcheck) — only static config parsing.

### CI/CD Status

All six CI jobs reviewed and confirmed to not silently skip critical coverage; every skip is
explicit and message-carrying. No CI configuration was weakened or changed this phase.

### Deployment Status

Documented in `docs/operations/deployment.md`. No actual production deployment was performed or is
claimed — this phase describes how to deploy, it does not deploy.

### Observability Status

`/metrics` output verified internally consistent against a real, mixed workload this phase. No new
metrics needed or added.

### Known Limitations

- Malformed (non-UUID) IDs return HTTP 500 instead of 400 on lookup routes (§13) — safe, not fixed.
- No authentication/authorization anywhere — by design, documented (§12).
- Docker has never been runtime-validated, locally or in CI — only static config validation exists
  (§10, §18).
- `db-migrate.ps1` bug fixed this phase, but had no test coverage before or after (no automated
  test exercises the migration *scripts* themselves, only that migrations are already applied to
  the test database by the time tests run) — a genuine gap in guarding against a regression of the
  same class of bug.
- Category hierarchy browser-click verification was blocked by UI automation flakiness this
  session; verified via direct API instead (§6, §25) — the underlying dashboard behavior was not
  independently re-confirmed by a human or a stable automated click flow.
- No distributed/multi-instance deployment story (`LocalWorkerPool`/`RetryDispatcher` remain
  single-process) — unchanged from prior phases, explicitly out of this phase's scope per the brief.

### Deferred Work

- UUID-shape request validation across ID-accepting routes (would fix the malformed-UUID-→-500
  finding properly, but is broader surface area than this phase's "fix only verified issues" scope).
- Real Docker runtime validation (needs a Docker-capable CI runner or local environment — neither
  was available this phase).
- A dedicated test for `db-migrate.ps1`/`db-migrate.sh` themselves (e.g. a CI job that runs the
  script against a throwaway database and asserts the resulting schema, catching exactly the class
  of bug found this phase before it reaches a developer).
- Authentication/authorization, if FlowForge is ever deployed outside a trusted network.

### Final Recommendation

FlowForge is **production-style** and demonstrably portfolio-grade: correct, reliable, tested (637
passing backend tests plus real 100+ record acceptance at the database level), reasonably secure
for a trusted-environment deployment, observable, and documented. It is **not** "fully production
ready" in the unqualified sense — it has no authentication, has never been Docker-runtime-validated,
and runs on a single-process worker pool. Anyone deploying it outside a trusted, private network, or
at a scale requiring distributed workers, must treat those as explicit prerequisites, not assumptions.
