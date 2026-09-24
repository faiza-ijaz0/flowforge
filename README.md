# FlowForge

FlowForge is a **production-style** job processing and workload orchestration platform: upload a
CSV or an image/screenshot, watch it become a real workload of real jobs, dispatched through a
concurrent C++ scheduler and worker pool, persisted in PostgreSQL, and observable end-to-end from a
Next.js dashboard. It is built around a concurrent C++ engine, a REST API, PostgreSQL persistence,
and an operator dashboard that never shows a fabricated number.

This repository is at **Phase 3H: Production Readiness & Final Validation**. The execution core
(Phase 1–2B) remains the foundation and is unchanged: a job dispatched by
`engine::PriorityScheduler` is genuinely executed — `Queued -> Running ->
Succeeded`/`Failed`/`Retrying`/`DeadLetter`/`Cancelled` — through `HandlerRegistry`/`IJobHandler`,
with a real `job_attempts` row persisted per attempt, cooperative cancellation/timeout, and a real,
restart-safe retry engine (`RetryDispatcher`). Phase 3A–3F added the **Workload** model (a logical
grouping of jobs submitted as one unit — e.g. one CSV import) and a source-/target-agnostic
**input processing pipeline** (CSV and image/screenshot-via-OCR sources, crossed with
Users/Products/Categories targets). Phase 3G unified the dashboard into one coherent, navigable
product. **Phase 3H (this phase)** closed the last domain-persistence gap — **Users, Products, and
Categories are now all real, persisted domains** with their own PostgreSQL tables, repositories,
and paginated read APIs — added real 100+ record automated acceptance coverage (CSV and real-OCR
image sources, all three domains, against a real PostgreSQL database), found and fixed a genuine
Windows migration-tooling bug via real fresh-database and upgrade-path testing, ran a focused
security probe battery, measured a real performance baseline, and produced complete operational
documentation (deployment, backup/recovery, release checklist). See
[`docs/architecture/phase-3h-production-readiness.md`](docs/architecture/phase-3h-production-readiness.md)
for the full report — including honestly-documented gaps (no authentication, Docker never
runtime-validated) this phase did **not** claim to close. See also
[`docs/architecture/phase-3g-audit.md`](docs/architecture/phase-3g-audit.md),
[`docs/architecture/overview.md`](docs/architecture/overview.md),
[`docs/architecture/execution-model.md`](docs/architecture/execution-model.md),
[`docs/architecture/workload-model.md`](docs/architecture/workload-model.md), and
[`docs/architecture/input-processing.md`](docs/architecture/input-processing.md) for what's real
versus interface-only, and why. Workflow DAG execution is still deliberately not yet implemented.

## Why FlowForge exists

Most "job queue" projects are either a thin wrapper around a database table or a toy demonstrating a
single pattern. FlowForge is an attempt to build the real thing: a job engine with proper concurrency
primitives, typed configuration, structured error handling, a persistence layer that can be swapped
without touching business logic, and an API/dashboard that never lies about what's actually
implemented.

## Example workflow

1. Open the dashboard's **Processing Center** (`/processing`), pick a target (Users, Products, or
   Categories) and a source (CSV, image, or screenshot), and upload a file.
2. CSV+Users submits directly; every other combination goes through an explicit
   **preview** step first (`POST /api/v1/process/preview` — extracts and validates records,
   creates nothing yet) so you can see exactly what will be created before confirming
   (`POST /api/v1/process/confirm`).
3. Confirming creates a real **Workload** (`POST /api/v1/workloads` under the hood) — one Job per
   valid record, each dispatched through the same `PriorityScheduler`/`LocalWorkerPool` pipeline as
   any other job.
4. The dashboard takes you straight to that workload's detail page (`/workloads/{id}`), which polls
   `GET /api/v1/workloads/{id}` and shows real, live-computed progress — queued/running/succeeded/
   failed, plus retrying/dead-letter sub-counts — never a cached or invented number.
5. Drill into any individual job (`/jobs/{id}`) for its real execution attempt history
   (`GET /api/v1/jobs/{id}/attempts`) — worker, outcome, duration, error — or browse
   `/workloads`, `/jobs`, `/products`, `/categories` for the full, paginated history.
6. Check `/health` for the same live `GET /ready` breakdown (database, scheduler, worker pool,
   retry dispatcher) an orchestrator's healthcheck would use.

## Architecture at a glance

```
apps/dashboard  --(HTTP/JSON)-->  apps/server  -->  engine  -->  spdlog
   (Next.js)                      (C++ HTTP API)   (C++ domain +
                                                      business logic)
```

The engine (`engine/`) has zero HTTP or JSON dependencies and is fully testable on its own. See
[`docs/architecture/overview.md`](docs/architecture/overview.md) for the full component breakdown,
dependency direction, data flow, and a Mermaid diagram.

## Technology stack

| Layer | Technology | Why |
|---|---|---|
| Engine | C++23, CMake, Ninja | Modern error handling (`std::expected`), strong concurrency primitives |
| HTTP server | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | Header-only, no external deps, right-sized for this phase's endpoint count |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | De facto standard, header-only |
| Logging | [spdlog](https://github.com/gabime/spdlog), behind a facade | Fast, structured, swappable without touching call sites |
| Testing | GoogleTest, GoogleBenchmark | Industry standard, CTest integration |
| Database | PostgreSQL via [libpqxx](https://github.com/jtv/libpqxx) | Real client library, parameterized queries, real connection pool -- see `docs/architecture/overview.md` §7 |
| Dashboard | Next.js 15, TypeScript, Tailwind CSS | App Router, server components for real data fetching |
| Infra | Docker, Docker Compose, GitHub Actions | Environment-driven config, no committed secrets |

## Repository structure

```
flowforge/
├── apps/
│   ├── server/          C++ HTTP API (httplib + nlohmann::json; depends on engine/)
│   └── dashboard/        Next.js + TypeScript operator dashboard
├── packages/
│   └── shared/            TypeScript types mirroring the API's JSON contracts
├── engine/                 C++ core: domain model, services, concurrency primitives,
│                           persistence interfaces, infra (config/logging/clock/metrics)
│   ├── include/flowforge/  public headers
│   ├── src/                 implementation
│   └── tests/                GoogleTest unit + integration tests
├── services/
│   ├── scheduler/          placeholder for the Phase 2 standalone scheduler service
│   └── workers/             placeholder for the Phase 2 standalone worker service
├── database/
│   ├── migrations/          numbered SQL migrations (applied by scripts/db-migrate.sh)
│   └── seeds/                 optional dev-only seed data
├── benchmarks/              Google Benchmark suite for the concurrency primitives
├── infra/
│   ├── docker/                Dockerfiles for server + dashboard
│   └── monitoring/            placeholder for future Prometheus/Grafana config
├── docs/
│   ├── architecture/           architecture overview, per-domain design docs, and phase audits
│   ├── api/                    API reference (`reference.md`)
│   ├── development/            contributor setup guide (`getting-started.md`)
│   └── operations/             deployment, backup/recovery, release checklist
├── scripts/                  db-migrate.sh/.ps1 and other dev scripts
├── cmake/                    CompilerWarnings.cmake, Sanitizers.cmake, StaticAnalysis.cmake
├── CMakeLists.txt
├── docker-compose.yml
└── .env.example
```

## Development setup

### Prerequisites

- A C++20/23 compiler. This repo was developed on Windows via the [WinLibs](https://winlibs.com/)
  UCRT+LLVM distribution, which bundles both **GCC 14** and **clang 19**. Use **g++** as
  `CMAKE_CXX_COMPILER` on Windows/MinGW: clang+libstdc++ on the `x86_64-w64-mingw32` target has a
  known TLS relocation bug (`relocation truncated to fit: IMAGE_REL_AMD64_SECREL` against
  `std::__once_call`/`std::__once_callable`) that breaks linking anything using `std::call_once`
  transitively (e.g. `std::future`/`std::async` internals) — this is a Windows-COFF-specific
  clang/libstdc++ incompatibility, not a FlowForge bug, and does not occur on Linux. clang remains
  fully usable on Windows for `clang-format`/`clang-tidy`. CI builds with clang on Ubuntu, which is
  unaffected.
- [CMake](https://cmake.org/) >= 3.24 and [Ninja](https://ninja-build.org/).
- [Node.js](https://nodejs.org/) >= 20 and npm, for the dashboard.
- [Docker](https://www.docker.com/) and Docker Compose, for the full local stack (optional — the
  server and dashboard both run natively without Docker).
- PostgreSQL client tools (`psql`), only if you plan to run migrations (`scripts/db-migrate.sh`).
- `libpq` (PostgreSQL's C client library), to build the PostgreSQL-backed persistence layer
  (`FLOWFORGE_WITH_POSTGRES`, default `ON` — auto-disables with a warning if not found). See
  [`docs/development/getting-started.md`](docs/development/getting-started.md), "PostgreSQL setup",
  for install commands per OS.

### Clone and configure

```bash
# Windows/MinGW: use g++ (see the compiler note above for why not clang++)
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_BUILD_TYPE=Debug

# Linux/macOS: clang or gcc both work
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug
```

The first configure fetches spdlog, nlohmann/json, cpp-httplib, GoogleTest, and Google Benchmark via
CMake `FetchContent` — this requires network access and takes a few minutes; subsequent configures
are cached.

## Build instructions

```bash
cmake --build build -j
```

Useful CMake options (pass as `-D<OPTION>=ON|OFF` at configure time):

| Option | Default | Effect |
|---|---|---|
| `FLOWFORGE_BUILD_TESTS` | `ON` | Build `flowforge_engine_tests` / `flowforge_server_tests` |
| `FLOWFORGE_BUILD_BENCHMARKS` | `ON` | Build `flowforge_benchmarks` |
| `FLOWFORGE_WARNINGS_AS_ERRORS` | `OFF` | Escalate first-party-target warnings to errors (CI uses `ON`) |
| `FLOWFORGE_ENABLE_ASAN` | `OFF` | AddressSanitizer |
| `FLOWFORGE_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer |
| `FLOWFORGE_ENABLE_TSAN` | `OFF` | ThreadSanitizer (mutually exclusive with ASan/UBSan) |
| `FLOWFORGE_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy during the build (requires `clang-tidy` on PATH) |

Example: a sanitizer build (Linux/macOS with clang, or Windows with g++ — g++ also supports
`-fsanitize=address,undefined` via the same `FLOWFORGE_ENABLE_ASAN`/`FLOWFORGE_ENABLE_UBSAN` flags) —

```bash
cmake -B build-san -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc \
  -DFLOWFORGE_ENABLE_ASAN=ON -DFLOWFORGE_ENABLE_UBSAN=ON
cmake --build build-san -j
ctest --test-dir build-san --output-on-failure
```

## Running the server

```bash
FLOWFORGE_ENV=development ./build/apps/server/flowforge_server
```

Then, in another shell:

```bash
curl http://localhost:8080/health
curl http://localhost:8080/ready    # 503 with {"status":"unavailable",...} if a dependency is down
curl http://localhost:8080/metrics
curl -X POST http://localhost:8080/api/v1/jobs \
  -H 'Content-Type: application/json' \
  -d '{"queue_name":"emails","payload":{"to":"a@example.com"}}'
curl http://localhost:8080/api/v1/jobs
```

See [`.env.example`](.env.example) for every configuration variable.

## Test instructions

```bash
ctest --test-dir build --output-on-failure
```

or run the test binaries directly for more verbose GoogleTest output:

```bash
./build/engine/tests/flowforge_engine_tests
./build/apps/server/tests/flowforge_server_tests
```

By default this runs entirely against in-memory repositories — no PostgreSQL required. The
PostgreSQL-backed integration tests (real repository tests, a restart-persistence acceptance test,
and six 100+ record bulk acceptance tests covering Users/Products/Categories × CSV/real-OCR-image —
see `docs/architecture/phase-3h-production-readiness.md` §4) are opt-in and `GTEST_SKIP()` unless a
test database is configured; see
[`docs/development/getting-started.md`](docs/development/getting-started.md), "Running PostgreSQL
integration tests". As of Phase 3H: 537 engine tests + 100 server tests, all passing.

## Benchmarks

```bash
./build/benchmarks/flowforge_benchmarks
```

## Dashboard

```bash
npm install
npm run dev:dashboard
```

Opens on `http://localhost:3000`. Set `NEXT_PUBLIC_API_URL` (in `apps/dashboard/.env.local`, copied
from `apps/dashboard/.env.example`) if the server isn't on `http://localhost:8080`. The server's
`FLOWFORGE_CORS_ALLOWED_ORIGIN` must match the dashboard's own origin exactly (default
`http://localhost:3000` on both sides) — see `.env.example`.

Pages: **Overview** (`/`, real workload/job counts + `/ready` health breakdown) · **Processing
Center** (`/processing`, upload → preview → confirm) · **Workloads** (`/workloads`,
`/workloads/{id}`) · **Jobs** (`/jobs`, `/jobs/{id}`, with execution attempt history) · **Users** /
**Products** / **Categories** (`/users`, `/products`, `/categories` — import wizard plus a real,
paginated, persisted record list for all three, since Phase 3H) · **Workflows** / **Workers**
(read-only) · **System health** (`/health`, live `GET /ready` polling) · **Metrics** (`/metrics`,
raw text feed) · **Queues** / **Logs** / **Settings** (honest `NotYetImplemented` placeholders — no
backend yet, never a fake empty state).

```bash
npm run lint:dashboard
npm run typecheck:dashboard
npm run build:dashboard
```

## Database

The C++ server talks to PostgreSQL through a real, libpqxx-backed persistence layer (see
`docs/architecture/overview.md` §7). To stand up a local database and apply the schema:

```bash
docker compose up -d postgres
cp .env.example .env   # edit POSTGRES_* / FLOWFORGE_DATABASE_URL as needed
docker compose run --rm migrate
# or, without Docker:
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge ./scripts/db-migrate.sh
```

Then run the server with `FLOWFORGE_DATABASE_URL` set (see
[`docs/development/getting-started.md`](docs/development/getting-started.md), "PostgreSQL setup") to
use it — unset (the `development`/`test` default), the server uses in-memory repositories instead.

## Docker

```bash
cp .env.example .env
docker compose up --build
```

Starts PostgreSQL, the server (`:8080`), and the dashboard (`:3000`). Migrations are **not** run
automatically (see `docs/architecture/overview.md` §7) — run `docker compose run --rm migrate`
separately. Both the server and dashboard images run as non-root users and have `HEALTHCHECK`
instructions. **Docker has not been runtime-validated in this project's CI or local development** —
only `docker compose config` (static validation) runs today; see
[`docs/operations/deployment.md`](docs/operations/deployment.md) and the production-readiness
report for the full, honest status.

## Code quality

```bash
# C++ formatting (requires clang-format)
find engine apps/server benchmarks -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Static analysis (requires clang-tidy)
cmake -B build -G Ninja -DFLOWFORGE_ENABLE_CLANG_TIDY=ON && cmake --build build -j
```

## CI

`.github/workflows/ci.yml` runs on every push/PR: C++ build + test (Debug and Release), an
ASan+UBSan test run, a dedicated PostgreSQL integration test job (spins up a `postgres:16` service
container, runs migrations, then runs the full test suite including the PostgreSQL-backed repository
tests and the restart-persistence acceptance test), `clang-format --dry-run`, dashboard
lint/typecheck/build, and `docker compose config` validation.

## Roadmap

**Phase 1 — done:** monorepo structure, CMake build system with warnings/sanitizers/
clang-tidy support, domain model, `ThreadPool`/`BlockingQueue` concurrency primitives, typed
config + structured logging + in-memory metrics, `IJobRepository`/`IWorkflowRepository`/
`IWorkerRepository` interfaces with real in-memory implementations, `JobService` with full CRUD +
validation, a real HTTP API (`/health`, `/ready`, `/metrics`, `/api/v1/jobs*`,
`/api/v1/workflows`, `/api/v1/workers`), PostgreSQL schema + migration runner, Next.js dashboard
shell wired to real endpoints where they exist, Docker/Compose, CI.

**Phase 2A (this phase) — done:** `libpqxx`-backed `IJobRepository`/`IWorkflowRepository`/
`IWorkerRepository` implementations, a real connection pool and transaction strategy, a
config-driven repository factory/composition root (`persistence::create_repositories`) replacing
the hardcoded in-memory construction in `App`, PostgreSQL-specific error mapping, PostgreSQL
integration test suite (opt-in, real database, no mocks), a restart-persistence acceptance test,
CI PostgreSQL service container, migration `0010` (`workflow_steps.position`), and updated
documentation. In-memory repositories are kept for fast unit tests and as the
development/test-mode default.

**Phase 2B-1 — done:** `IJobHandler` handler abstraction, `ExecutionContext`,
`domain::ExecutionResult`, a thread-safe `HandlerRegistry` (explicitly constructed/injected, not a
singleton), three real built-in handlers (`echo`, `delay` with a bounded/cancellable sleep,
`transform`), `domain::Job::job_type()` (domain-only at the time — persistence/API added in
Phase 2B-2 below), and full unit/concurrency test coverage. See
[`docs/architecture/execution-model.md`](docs/architecture/execution-model.md).

**Phase 2B-2 — done:** `engine::PriorityScheduler` (a real `IScheduler`
implementation) — bounded priority-ordered dispatch queue (`PriorityBlockingQueue`, FIFO
tie-break), clean start/stop lifecycle, backpressure (`ErrorCode::Conflict` at capacity),
`HandlerRegistry`-backed validation/dispatch, scheduler metrics/logging, `job_type` persisted
end-to-end (migration `0011`, `CreateJobRequest`, HTTP API, both repository backends),
`POST /api/v1/jobs` submits to the real Scheduler when `job_type` is set, dashboard shows job
type/priority. See [`docs/architecture/execution-model.md`](docs/architecture/execution-model.md)
§7–§9.

**Phase 2B-3 (this phase) — done:** `engine::LocalWorkerPool` (a real `IWorkerPool`
implementation, built on `BlockingQueue`/`ThreadPool`) and `engine::JobExecutor` (a real
`IExecutor` implementation) complete the execution path. A job dispatched by the Scheduler is
genuinely executed: `Queued -> Running -> Succeeded`/`Failed`/`Cancelled`, resolved through
`HandlerRegistry`/`IJobHandler` (never a job-type switch statement), with a real `job_attempts`
row persisted per attempt (`InMemoryExecutionRepository`/`PostgresExecutionRepository`
implementing the existing `engine::IExecutionManager` — no new migration needed) and one real,
persisted `domain::Worker` row per local worker thread. Cooperative cancellation (a queued job
never executes once cancelled; a running `DelayHandler` job observes cancellation and stops) and a
cooperative per-attempt timeout are both real — neither ever forcibly terminates a thread.
Additive `GET /api/v1/jobs/{id}/attempts` endpoint; dashboard shows real execution attempt
history (worker, outcome, duration, error). Comprehensive lifecycle/execution/cancellation/
timeout/concurrency test coverage, plus a real local end-to-end verification run (C++ backend +
PostgreSQL + Next.js dashboard: a real `echo` job created through the API genuinely reaches
`Succeeded`). See execution-model.md §10–§17.

**Phase 2B-4 — done:** retry engine. `JobExecutor` now classifies a failed attempt as retryable or
not via `domain::ExecutionResult::retryable()` (a Phase 2B-1 hint nothing previously consumed),
landing on `Retrying`/`DeadLetter` (via the existing `Job::record_attempt_failure()`) or a
permanent `Failed` accordingly. `engine::RetryDispatcher` is the new component that actually acts
on a `Retrying` job: poll-based (not a fragile in-memory timer) and therefore restart-safe by
construction, re-submitting through the same `IScheduler` a fresh job uses once
`RetryPolicy::compute_backoff()` says the backoff has elapsed. Real PostgreSQL acceptance tests
cover retry-then-succeed and permanently-fails-to-DeadLetter, plus a live demonstration against
the running server. See execution-model.md §18–§19.

**Phase 2B-5 (this phase) — done:** production observability and operational reliability.
`GET /ready` now performs real, cheap, non-blocking checks against PostgreSQL (via
`PgConnectionPool::is_available()`), the scheduler, the worker pool, and the retry dispatcher, and
returns `503` — never a lying `200` — the moment any of them is unavailable; `GET /health` stays a
pure liveness check, deliberately independent of all of that. A monotonic (`steady_clock`, not
wall-clock) execution-duration histogram now backs `flowforge_executor_execution_duration_ms`, and
a small, coherent metrics vocabulary was added across jobs/scheduler/worker-pool/executor/retry/
database (backpressure rejections, retrying vs. dead-lettered vs. permanently-failed vs.
timed-out attempts, connection-pool leased/size gauges) — see execution-model.md §20 for the full
list and the naming convention. Every 5xx HTTP error now returns a fixed, generic message instead
of ever echoing a raw exception string to a client. Startup-failure cleanup (an unreachable
PostgreSQL leaves no leaked thread or connection) and graceful-shutdown ordering are both now
covered by dedicated tests, not just asserted in comments.

**Phase 3A — done:** the Workload model. `domain::Workload`/`services::WorkloadService`,
`jobs.workload_id` (migration 0013), `POST /api/v1/workloads` (one Job per item, created-then-
scheduled exactly like `POST /api/v1/jobs`), live-computed progress
(queued/running/completed/failed) derived from child jobs on every read — never a persisted,
driftable counter. See [`docs/architecture/workload-model.md`](docs/architecture/workload-model.md).

**Phase 3B — done:** bulk User import. `POST /api/v1/workloads/user-imports` (CSV upload → one
workload, one `user.process` job per valid row), strict CSV parsing/validation, paginated
per-item results (`GET /api/v1/workloads/{id}/items`). See
[`docs/architecture/user-import.md`](docs/architecture/user-import.md).

**Phase 3C/3D — done:** the source-/target-agnostic Processing Center. `POST /api/v1/process`
generalizes bulk import beyond Users+CSV; `POST /api/v1/process/preview` +
`POST /api/v1/process/confirm` add an explicit review step (extraction creates nothing; only
confirmation does) and image/screenshot-via-OCR as a real second input source alongside CSV. See
[`docs/architecture/input-processing.md`](docs/architecture/input-processing.md).

**Phase 3E/3F — done:** Products and Categories as real, persisted, dedicated domains —
`handlers::ProductProcessHandler`/`CategoryProcessHandler` upsert into their own tables,
with paginated read APIs (`GET /api/v1/products`, `GET /api/v1/categories`, both with `total`) and
dashboard list pages. See
[`docs/architecture/product-processing.md`](docs/architecture/product-processing.md) /
[`docs/architecture/category-processing.md`](docs/architecture/category-processing.md).

**Phase 3G (this phase) — done:** platform unification and hardening, driven by an explicit
audit-first pass rather than new features. Closed: no `/workloads` browse page existed despite the
API supporting it; a job's own JSON never exposed its `workload_id` (so a job page couldn't link
back to its workload); `GET /api/v1/jobs`/`GET /api/v1/workloads` had no `total`, blocking real
pagination UI; workload-level progress collapsed `retrying`→queued and `dead_letter`→failed into
their parent buckets with no visible sub-count. Added: a `/health` dashboard page over the
already-real `GET /ready`; a dashboard home showing real workload/job counts instead of a bare
reachability dot; Docker `HEALTHCHECK`s for the server and dashboard images; consistent
loading/empty/error+retry handling across list pages; an explicit "View Workload / View Jobs /
Return to Processing Center" path after a successful submission instead of a dead-end success
message. See [`docs/architecture/phase-3g-audit.md`](docs/architecture/phase-3g-audit.md) for the
full gap analysis this phase worked from.

**Phase 3H (this phase) — done:** production readiness and final validation. Closed the last
domain-persistence gap: Users gained a dedicated `users` table, repository, and
`GET /api/v1/users` read API, mirroring Products/Categories exactly (`handlers::UserProcessHandler`
now upserts instead of only validating/normalizing). Added real 100+ record automated acceptance
coverage against a real PostgreSQL database for all three domains across both CSV and real-OCR
image sources (six new/extended `ProcessRoutesBulkPostgresTest` cases). Found and fixed a real bug
in `scripts/db-migrate.ps1` that silently reported success on every migration while applying none
of them against a genuinely fresh Windows database — verified via real fresh-database and
incremental-upgrade runs afterward. Ran a focused security probe battery (malformed input, SQL
injection strings, oversized payloads, path traversal, corrupt uploads) against a live server;
found one non-critical API-contract issue (a malformed ID returns 500 instead of 400) and
documented it rather than rushing a cross-cutting fix. Measured a real performance baseline (100
and 500-record CSV flows, 100-record OCR flows) with no unsupported scalability claims. Added
`docs/operations/{deployment,backup-and-recovery,release-checklist}.md`. Explicitly documented,
rather than silently ignored: no authentication/authorization exists, and Docker has never been
runtime-validated (only statically). See
[`docs/architecture/phase-3h-production-readiness.md`](docs/architecture/phase-3h-production-readiness.md)
for the complete report.

**Next phase — workflow DAG execution, stronger cancellation/timeout, more observability:**
- Workflow execution: DAG validation (cycle detection), step sequencing, a real create-workflow
  path (today `workflows`/`workflow_steps` are schema-and-read-only).
- Stronger cancellation/timeout semantics beyond cooperative-only signaling, if a real need for
  them emerges (e.g. a supervisory process that can restart a stuck worker).
- Distributed/multi-process worker coordination (`LocalWorkerPool` is in-process/local only) and
  distributed retry dispatching (today's `RetryDispatcher` is single-process only).
- Worker process registration/heartbeating as a real standalone deployable (`services/workers/`).
- An operator-triggered "retry now" / replay endpoint for a `DeadLetter` job (today the only way
  back from `DeadLetter` is direct database access).
- A real Prometheus exposition format for `/metrics` (today's plain `name value` text lines are a
  deliberately simple placeholder — see execution-model.md §20.1).

**Later phases:** API authentication/authorization, rate limiting, distributed job claiming across
multiple server instances (`SELECT ... FOR UPDATE SKIP LOCKED`), load/stress testing harness,
audit log UI, log aggregation, request-latency histograms (see execution-model.md §20.1 for why
this was deferred this phase specifically).
