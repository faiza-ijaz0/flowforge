# FlowForge

FlowForge is a high-performance job processing and workflow orchestration platform built around a
concurrent C++ engine, with a REST API, PostgreSQL persistence, and a Next.js operator dashboard.

This repository is at **Phase 2B-5: production observability and operational reliability**. Phase
1, Phase 2A (PostgreSQL persistence), Phase 2B-1 (handler abstraction), Phase 2B-2 (Scheduler +
priority dispatch), Phase 2B-3 (Executor + WorkerPool + real job execution), and Phase 2B-4
(retry engine: retryable-failure classification, exponential backoff, `RetryDispatcher`,
DeadLetter) are done. A job dispatched by the Scheduler is genuinely executed —
`Queued -> Running -> Succeeded`/`Failed`/`Retrying`/`DeadLetter`/`Cancelled` — through
`HandlerRegistry`/`IJobHandler`, with a real `job_attempts` row persisted per attempt, cooperative
cancellation/timeout (never a forced thread kill), and a real, restart-safe retry engine that
re-submits a failed-but-retryable job through the same scheduling path a fresh job takes. Phase
2B-5 adds the operational layer on top: `GET /ready` reflects the real state of PostgreSQL, the
scheduler, the worker pool, and the retry dispatcher (never a hardcoded `200 ok`); a coherent,
bounded metrics vocabulary covers job/scheduler/executor/retry/database counters plus a
monotonic-clock execution-duration histogram; every 5xx HTTP error returns a safe, generic message
(the real diagnostic detail stays in logs); and startup/shutdown are RAII-clean with no leaked
threads on a failed start. Verified end-to-end locally: a real job created through the API/
dashboard is picked up, executed, and reaches `Succeeded` against a real PostgreSQL database, and
a real retry-then-succeed cycle has been observed live against the running server. Workflow DAG
execution is still deliberately not yet implemented — see
[`docs/architecture/overview.md`](docs/architecture/overview.md) and
[`docs/architecture/execution-model.md`](docs/architecture/execution-model.md) for exactly what's
real versus interface-only, and why.

## Why FlowForge exists

Most "job queue" projects are either a thin wrapper around a database table or a toy demonstrating a
single pattern. FlowForge is an attempt to build the real thing: a job engine with proper concurrency
primitives, typed configuration, structured error handling, a persistence layer that can be swapped
without touching business logic, and an API/dashboard that never lies about what's actually
implemented.

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
│   ├── architecture/           architecture overview + decisions
│   ├── api/                    placeholder for OpenAPI/API reference docs
│   └── development/            placeholder for contributor guides
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
PostgreSQL-backed integration tests (real repository tests plus a restart-persistence acceptance
test) are opt-in and `GTEST_SKIP()` unless a test database is configured; see
[`docs/development/getting-started.md`](docs/development/getting-started.md), "Running PostgreSQL
integration tests".

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
from `apps/dashboard/.env.example`) if the server isn't on `http://localhost:8080`.

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
separately.

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
