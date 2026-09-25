# FlowForge

**A C++-powered workload processing platform for high-throughput structured data ingestion and
concurrent job execution.**

[![CI](https://github.com/faiza-ijaz0/flowforge/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/faiza-ijaz0/flowforge/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A53.24-064F8C?logo=cmake&logoColor=white)
![PostgreSQL](https://img.shields.io/badge/PostgreSQL-16-4169E1?logo=postgresql&logoColor=white)
![Next.js](https://img.shields.io/badge/Next.js-15-000000?logo=nextdotjs&logoColor=white)

FlowForge turns CSV files and images of tables into validated **workloads** of **jobs**, executes
them on a concurrent C++ engine (priority scheduler, bounded worker pool, retries, dead letters),
persists the results in PostgreSQL, and exposes every step through a REST API and a Next.js
operations dashboard.

It is a production-style, portfolio-grade systems project: tested against a real PostgreSQL
database, validated in CI (including a Docker build and runtime smoke test), and explicit about the
trade-offs it has not addressed yet (see [Known limitations](#known-limitations)).

---

## Contents

[Overview](#overview) · [Key capabilities](#key-capabilities) · [Architecture](#architecture) ·
[Engineering highlights](#engineering-highlights) · [Processing domains](#supported-processing-domains) ·
[Processing workflow](#processing-workflow) · [Reliability model](#reliability-model) ·
[Technology stack](#technology-stack) · [Project structure](#project-structure) ·
[Getting started](#getting-started) · [Configuration](#environment-configuration) · [API](#api) ·
[Testing](#testing) · [CI](#cicd) · [Docker](#docker) · [Security](#security-considerations) ·
[Known limitations](#known-limitations) · [Roadmap](#roadmap) · [Documentation](#documentation)

## Overview

Bulk data arrives in inconvenient shapes: a spreadsheet export, a screenshot of a table, a scanned
list. Turning it into database records usually means ad-hoc scripts with no validation, no review
step, no retry behavior, and no visibility into what happened to each row.

FlowForge treats every import as a unit of work that can be inspected and reasoned about:

```
Input (CSV / image / screenshot)
  → Extraction (CSV parser or Tesseract OCR)
  → Normalization + validation (per domain)
  → Preview (nothing is created yet)
  → Confirmation
  → Workload (one per submission)
  → Jobs (one per accepted record)
  → Priority scheduler → bounded worker pool → job executor
  → PostgreSQL
  → Observability (live progress, attempts, metrics, readiness)
```

Invalid records are rejected with a reason and a row number, before any job is created. Accepted
records become jobs that run concurrently, retry on transient failures, and land in a dead-letter
state when their retry budget is exhausted. Workload progress is computed from those jobs on every
read, so it is never a cached counter that can drift.

## Key capabilities

- **Concurrent C++ execution engine**: priority scheduler, bounded worker pool, and job executor
  with cooperative cancellation and per-attempt timeouts
- **Workload/job model**: one workload per submission, one job per accepted record, with live
  progress (queued, running, succeeded, failed, retrying, dead-letter)
- **Retry and dead-letter handling**: per-job retry policy with exponential backoff; a
  poll-based, restart-safe retry dispatcher
- **PostgreSQL persistence**: repository interfaces with PostgreSQL (libpqxx) and in-memory
  implementations, a connection pool, parameterized SQL, and 16 numbered migrations
- **CSV ingestion** and **image/screenshot ingestion via Tesseract OCR**
- **Preview before confirmation**: extraction and validation create nothing until the user confirms
- **Three processing domains**: Users, Products, and Categories (with parent/child hierarchies),
  each persisted to its own table
- **Observability**: `/health` (liveness), `/ready` (real dependency checks), `/metrics`, and per-job
  execution attempt history
- **Paginated read APIs** with totals for jobs, workloads, workload items, users, products, and
  categories
- **Docker images and Compose stack** with healthchecks, non-root containers, and a migration
  service
- **CI** covering Debug/Release builds, sanitizers, PostgreSQL integration, formatting, the
  dashboard, and a Docker runtime smoke test

## Architecture

```mermaid
flowchart TB
    user([Operator]) --> dash["Next.js dashboard"]
    dash -->|HTTP / JSON| api

    subgraph server["flowforge_server (C++23)"]
        api["HTTP API<br/>(cpp-httplib)"]
        ips["InputProcessingService<br/>CSV + image extractors"]
        wls["WorkloadService<br/>JobService"]
        sched["PriorityScheduler<br/>priority queue"]
        pool["LocalWorkerPool<br/>bounded queue"]
        exec["JobExecutor<br/>handler registry"]
        retry["RetryDispatcher"]
        metrics["Metrics<br/>/health · /ready"]
        repos["Repositories<br/>PostgreSQL · in-memory"]

        api --> ips --> wls
        api --> wls
        wls --> sched --> pool --> exec
        retry -->|due retries| sched
        exec --> repos
        wls --> repos
        retry --> repos
        api --> metrics
    end

    ips -.->|subprocess| ocr[["Tesseract OCR"]]
    repos --> pg[("PostgreSQL")]
```

The C++ code is split into two layers. `engine/` holds the domain model, services, concurrency
primitives, persistence, and infrastructure (config, logging, metrics), with no HTTP or JSON
dependency. `apps/server/` holds the HTTP layer and the composition root (`App::create`). The
dashboard only talks to the public HTTP API.

### Input-processing pipeline

```mermaid
flowchart LR
    src["CSV / image"] --> ext["Extraction<br/>(CSV parser, OCR)"]
    ext --> rec["Structured<br/>records"]
    rec --> map["Mapping +<br/>normalization"]
    map --> val["Validation<br/>(per row)"]
    val --> prev["Preview<br/>(creates nothing)"]
    prev --> conf["Confirm<br/>(re-validates)"]
    conf --> wl["Workload"]
    wl --> jobs["Jobs"]
    jobs --> eng["C++ engine"]
    eng --> db[("PostgreSQL")]
```

More detail: [`docs/architecture/overview.md`](docs/architecture/overview.md) (components and
dependency direction), [`execution-model.md`](docs/architecture/execution-model.md) (scheduler,
worker pool, executor, retries), [`workload-model.md`](docs/architecture/workload-model.md), and
[`input-processing.md`](docs/architecture/input-processing.md).

## Engineering highlights

### Concurrent execution

- **`PriorityScheduler`** accepts jobs into a bounded **`PriorityBlockingQueue`** (priority order,
  FIFO within a priority) and drains it with a configurable number of dispatch threads.
- **`LocalWorkerPool`** executes jobs on a fixed set of worker threads fed by its own bounded
  dispatch queue, so concurrency is capped at `FLOWFORGE_WORKER_POOL_SIZE` regardless of input size.
- **`JobExecutor`** resolves each job's handler through a `HandlerRegistry` (no job-type switch
  statements), records a `job_attempts` row per attempt, and enforces a cooperative per-attempt
  timeout. Cancellation is cooperative too: threads are never forcibly killed.
- **Backpressure** is explicit. A full scheduler or worker-pool queue rejects new work with a
  `Conflict` error and increments a dedicated metric instead of growing without bound.

### Reliability

- Each job carries a **retry policy** (default 3 attempts, 1 s initial backoff, ×2 multiplier,
  60 s cap). A handler marks each failure as retryable or not; non-retryable failures (such as
  invalid data) fail immediately.
- The **`RetryDispatcher`** polls for `Retrying` jobs whose backoff has elapsed and re-submits them
  through the same scheduler. Because it is poll-based, not an in-memory timer, pending retries
  survive a server restart when PostgreSQL is used.
- Jobs that exhaust their attempts move to **dead letter**; every attempt stays visible through
  `GET /api/v1/jobs/{id}/attempts`.
- **Workload reconciliation**: a workload's counts are derived from its jobs on every read, and the
  100-record acceptance tests assert `submitted = accepted + rejected` and
  `accepted = jobs = succeeded` exactly.
- **No lost updates on dispatch**: a job's `queued` status is persisted *before* the job is handed
  to the scheduler, so a worker that finishes quickly can never have its result overwritten by a late
  status write. This was a real race, caught by the 100-record acceptance tests during release
  validation and fixed with regression tests (see
  [`execution-model.md` §8](docs/architecture/execution-model.md)).
- **`/ready`** checks the database pool, scheduler, worker pool, and retry dispatcher and returns
  `503` when any of them is unavailable; **`/health`** is a plain liveness check.

### Persistence

- Repository interfaces (`IJobRepository`, `IWorkloadRepository`, `IProductRepository`, …) have both
  **PostgreSQL** (libpqxx) and **in-memory** implementations, selected at startup by
  `FLOWFORGE_DATABASE_URL`. Staging and production refuse to start without a database.
- A bounded **connection pool** (`FLOWFORGE_DB_POOL_SIZE`), **parameterized queries throughout**,
  and PostgreSQL error mapping into typed application errors.
- **16 numbered SQL migrations** applied by a small runner (`scripts/db-migrate.sh` / `.ps1`) that
  tracks versions in `schema_migrations`. CI validates the runner itself against a fresh database,
  including a deliberately broken migration that must abort without being recorded.
- Domain records are **upserted by natural key** (`email`, `sku`, `slug`): re-importing updates rows
  in place, while duplicate keys *within* one submission are rejected.

### Input processing

- A single, domain-agnostic **`InputProcessingService`** handles every source × target pair:
  extractors produce generic `StructuredRecord`s, and small per-domain mapping functions turn them
  into typed, validated records. Adding a domain does not touch extraction.
- **CSV parsing** handles quoting, malformed rows (rejected per row, not per file), invalid UTF-8,
  duplicate headers, and size/row limits.
- **OCR sits behind an `IOcrProvider` abstraction**; the implementation runs the Tesseract CLI as a
  subprocess and reconstructs table rows from word positions. Uploads are identified by their
  **magic bytes** (PNG, JPEG, WebP), not by file extension.
- **Confirm never trusts the client**: every record is re-validated server-side, even though it
  normally comes straight from the preview response.
- Category imports resolve **parent/child hierarchies**. A parent can be in the same submission: the
  child's job is retried until the parent's job has committed.

### Observability

- `/metrics` exposes counters, gauges, and a histogram for HTTP requests and errors, job
  creation/rejection, scheduling, backpressure, worker completions, timeouts, retries, dead letters,
  database errors, and connection-pool usage (plain text, not Prometheus format).
- Every job has a persisted **execution attempt history** (worker, outcome, duration, error).
- The dashboard polls workload progress live and stops polling once the workload is terminal.
- Structured logging (spdlog behind a facade) with an optional one-JSON-object-per-line format.

### Security

Parameterized SQL; per-row input validation; upload limits (2 MB CSV, 6 MB image, 8 MB HTTP payload,
256 KB job payload); magic-byte image validation; the OCR subprocess launched with an argument
vector (never through a shell) and a timeout; an exact-match CORS allowlist; malformed path IDs
rejected with `400` before reaching the database; generic 5xx messages that never echo internal
errors; non-root Docker containers. See [Security considerations](#security-considerations).

## Supported processing domains

| Domain | CSV | Image / screenshot (OCR) | Persistence | Workload + jobs |
|---|---|---|---|---|
| **Users** | ✓ (processed directly, or via the `/users` import wizard) | ✓ (with preview) | `users` table, upsert by email | ✓ `user.process` |
| **Products** | ✓ (with preview) | ✓ (with preview) | `products` table, upsert by SKU | ✓ `product.process` |
| **Categories** | ✓ (with preview) | ✓ (with preview) | `categories` table, upsert by slug, parent/child hierarchy | ✓ `category.process` |

A Users CSV is the one combination without a preview step. It is validated row by row and
submitted directly, matching the dedicated Users import wizard.

## Processing workflow

A real run from the browser verification of the Users image flow:

```
Upload user_table_bulk_100.png (100 rows)
  → Preview: 100 records, 97 valid, 3 invalid (rows 20, 40, 100: OCR-garbled emails), ~77% OCR confidence
  → Confirm
  → 1 workload
  → 97 jobs
  → PriorityScheduler → LocalWorkerPool → JobExecutor
  → 97 users upserted into PostgreSQL
  → workload succeeded: 97 / 97
```

The 97/3 split is specific to that test fixture and the Tesseract version used. It is **not** a
general OCR accuracy figure. OCR can also produce values that pass validation but are wrong (for
example `personl@example.com` instead of `person1@example.com`), which is why the preview exists.

## Reliability model

```mermaid
stateDiagram-v2
    [*] --> Pending: created
    Pending --> Queued: accepted
    Queued --> Running
    Running --> Succeeded
    Running --> Failed: not retryable
    Running --> Retrying: retryable
    Retrying --> Queued: backoff elapsed
    Running --> DeadLetter: attempts used up
    Succeeded --> [*]
    Failed --> [*]
    DeadLetter --> [*]
```

Any job that is not yet terminal (pending, queued, running, or retrying) can also be cancelled
through `POST /api/v1/jobs/{id}/cancel`; cancellation is cooperative, so a running handler stops at
its next cancellation check.

Every execution attempt increments the job's `attempt_count` and writes a `job_attempts` row, so a
job that succeeded on its second attempt shows both attempts. A workload is `pending` or `queued` before
any job progresses, `running` while any job is not yet terminal, `succeeded` when every job
succeeded, and `failed` when every job is terminal and at least one did not succeed. Retrying and dead-lettered jobs are shown as separate sub-counts.

## Technology stack

| Area | Technology |
|---|---|
| Core engine and API server | C++23, cpp-httplib, nlohmann/json, spdlog |
| Database | PostgreSQL 16 via libpqxx (libpq) |
| OCR | Tesseract (CLI, invoked as a subprocess) |
| Dashboard | Next.js 15 (App Router), React 19, TypeScript 5, Tailwind CSS 3 |
| Shared contracts | TypeScript types in `packages/shared` (npm workspaces) |
| Build | CMake ≥ 3.24, Ninja; dependencies fetched with CMake `FetchContent` |
| Testing | GoogleTest / CTest, Google Benchmark, Python smoke test, Bash migration check |
| Code quality | clang-format, clang-tidy (optional), ASan + UBSan, ESLint, `tsc` |
| Infrastructure | Docker, Docker Compose |
| CI | GitHub Actions |

## Project structure

```
flowforge/
├── apps/
│   ├── server/              C++ HTTP API: routes, JSON mapping, CORS, composition root, tests
│   └── dashboard/           Next.js operations dashboard
├── engine/                  C++ core (no HTTP/JSON dependency)
│   ├── include/flowforge/   domain · engine · services · persistence · extractors ·
│   │                        handlers · providers · infra
│   ├── src/
│   └── tests/               GoogleTest suites, including PostgreSQL-backed tests and fixtures
├── packages/shared/         TypeScript types mirroring the API contracts
├── database/
│   ├── migrations/          0001–0016 numbered SQL migrations
│   └── seeds/               optional development seed data
├── tests/
│   ├── integration/         check-migrations.sh: migration runner validation
│   └── e2e/                 smoke-test.py: black-box HTTP smoke test of a running stack
├── benchmarks/              Google Benchmark suite for the concurrency primitives
├── infra/docker/            Dockerfiles for the server and dashboard
├── scripts/                 db-migrate.sh / db-migrate.ps1
├── cmake/                   compiler warnings, sanitizers, static analysis
├── docs/                    architecture, API reference, development, operations
├── docker-compose.yml
└── .env.example
```

`services/scheduler/` and `infra/monitoring/` contain only placeholder READMEs for future
standalone services and monitoring configuration.

## Getting started

### Prerequisites

- A C++23 compiler:
  - **Linux/macOS:** clang **19 or newer** (what CI uses) or GCC. clang 18 and older cannot compile
    the project against libstdc++ (they do not see `std::expected`).
  - **Windows:** GCC 14 from the [WinLibs](https://winlibs.com/) UCRT distribution (the local
    development toolchain). Do not use clang as the compiler on Windows/MinGW; see
    [`docs/development/getting-started.md`](docs/development/getting-started.md).
- CMake ≥ 3.24 and Ninja
- PostgreSQL 16 (or 17) with `libpq` and `psql`
- Node.js ≥ 20 and npm
- Tesseract OCR, only for image/screenshot processing (the server starts without it and reports
  image preview as unsupported)
- Docker with Compose (optional alternative; see [Docker](#docker))

### 1. Clone and configure

```bash
git clone https://github.com/faiza-ijaz0/flowforge.git
cd flowforge
cp .env.example .env
cp apps/dashboard/.env.example apps/dashboard/.env.local
```

### 2. Create the database and run migrations

```bash
# Option A: PostgreSQL in Docker
docker compose up -d postgres

# Option B: an existing PostgreSQL install
psql -U postgres -c "CREATE ROLE flowforge LOGIN PASSWORD 'flowforge';"
psql -U postgres -c "CREATE DATABASE flowforge OWNER flowforge;"

# Apply migrations (idempotent)
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge ./scripts/db-migrate.sh
# Windows PowerShell: $env:FLOWFORGE_DATABASE_URL="..."; .\scripts\db-migrate.ps1
```

### 3. Build the C++ server

```bash
# Linux/macOS
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_C_COMPILER=clang-19 -DCMAKE_BUILD_TYPE=Debug
# Windows (MinGW)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug

cmake --build build -j
```

The first configure downloads dependencies with `FetchContent` (network access required).
Useful options: `FLOWFORGE_BUILD_TESTS`, `FLOWFORGE_BUILD_BENCHMARKS`, `FLOWFORGE_WARNINGS_AS_ERRORS`,
`FLOWFORGE_ENABLE_ASAN`, `FLOWFORGE_ENABLE_UBSAN`, `FLOWFORGE_ENABLE_TSAN`,
`FLOWFORGE_ENABLE_CLANG_TIDY`.

### 4. Start the server

```bash
FLOWFORGE_ENV=development \
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge \
  ./build/apps/server/flowforge_server

curl http://localhost:8080/ready
```

Without `FLOWFORGE_DATABASE_URL`, development mode uses in-memory repositories (nothing persists).
On Windows, PostgreSQL's `bin` directory must be on `PATH` so `libpq.dll` can be found. Put it
**after** the compiler's `bin` directory: PostgreSQL ships its own `libwinpthread-1.dll`, and if that
copy is loaded instead of the toolchain's, the threading runtime can deadlock (observed in the
thread-pool tests).

### 5. Start the dashboard

```bash
npm install
npm run dev:dashboard
```

Open <http://localhost:3000> and start in the **Processing Center**. Sample inputs are in
`engine/tests/fixtures/` (for example `products_bulk_100.csv` and `user_table_bulk_100.png`).

## Environment configuration

Every server variable is documented in [`.env.example`](.env.example). The ones that matter most:

| Variable | Default | Notes |
|---|---|---|
| `FLOWFORGE_ENV` | `development` | `development`, `test`, `staging`, or `production` |
| `FLOWFORGE_DATABASE_URL` | unset | Unset: in-memory repositories (development/test only). **Required** in staging/production. |
| `FLOWFORGE_DB_POOL_SIZE` | `8` | PostgreSQL connection pool size |
| `FLOWFORGE_WORKER_POOL_SIZE` | `4` | Concurrent job executions |
| `FLOWFORGE_SCHEDULER_QUEUE_CAPACITY` / `FLOWFORGE_WORKER_POOL_QUEUE_CAPACITY` | `1024` | Backpressure limits |
| `FLOWFORGE_EXECUTION_TIMEOUT_MS` | `60000` | Cooperative per-attempt timeout |
| `FLOWFORGE_CORS_ALLOWED_ORIGIN` | `http://localhost:3000` | Exact origin of the dashboard; no wildcards |
| `FLOWFORGE_TESSERACT_PATH` | auto-detected | Path to the `tesseract` CLI if it is not on `PATH` |
| `FLOWFORGE_LOG_LEVEL` / `FLOWFORGE_STRUCTURED_LOGGING` | `info` / `false` | `true` emits one JSON object per line |
| `NEXT_PUBLIC_API_URL` | `http://localhost:8080` | Dashboard → API URL as seen by the browser. **Inlined at build time.** |

- **Development:** in-memory or local PostgreSQL; the credentials in `.env.example` are local
  placeholders only.
- **Testing:** PostgreSQL-backed tests use `FLOWFORGE_TEST_DATABASE_URL` pointing at a disposable
  database (the tests truncate the tables they touch). Without it, those tests report `SKIPPED`.
- **Production:** set `FLOWFORGE_ENV=production`, a real `FLOWFORGE_DATABASE_URL`, the dashboard's
  real origin in `FLOWFORGE_CORS_ALLOWED_ORIGIN`, and `FLOWFORGE_STRUCTURED_LOGGING=true`. `.env`
  files are git-ignored; never commit real credentials. See
  [`docs/operations/deployment.md`](docs/operations/deployment.md).

## API

REST over JSON under `/api/v1`, plus operational endpoints at the root. The full reference, including
request and response shapes, limits, and error behavior, is in
[`docs/api/reference.md`](docs/api/reference.md).

| Group | Endpoints |
|---|---|
| Health and metrics | `GET /health`, `GET /ready`, `GET /metrics` |
| Processing | `POST /api/v1/process/preview`, `POST /api/v1/process/confirm`, `POST /api/v1/process` |
| Workloads | `POST /api/v1/workloads`, `POST /api/v1/workloads/user-imports`, `GET /api/v1/workloads`, `GET /api/v1/workloads/{id}`, `GET /api/v1/workloads/{id}/items` |
| Jobs | `POST /api/v1/jobs`, `GET /api/v1/jobs`, `GET /api/v1/jobs/{id}`, `GET /api/v1/jobs/{id}/attempts`, `POST /api/v1/jobs/{id}/cancel` |
| Domain records | `GET /api/v1/users`, `GET /api/v1/products`, `GET /api/v1/categories` |
| Read-only | `GET /api/v1/workers`, `GET /api/v1/workflows` |

List endpoints take `limit`/`offset` (server-clamped) and return a `total`. Errors use one shape:
`{"error": {"code": "...", "message": "..."}}`.

## Testing

Latest verified results (v1.0.0 release pass):

| Suite | Result |
|---|---|
| `flowforge_engine_tests`: unit, concurrency, service, handler, and PostgreSQL repository tests | **551 passed** |
| `flowforge_server_tests`: HTTP-level integration tests through the real `App` | **103 passed** |
| **Backend total** (run against real PostgreSQL with Tesseract available, 0 skipped) | **654 passed, 0 failed** |
| `tests/integration/check-migrations.sh`: fresh apply, schema/constraint/FK/index checks, idempotent re-run, broken-migration failure path | passed (bash and PowerShell runners locally; fresh database in CI) |
| `tests/e2e/smoke-test.py`: black-box HTTP smoke test of a running stack, including real OCR | passed |
| Dashboard: ESLint, `tsc --noEmit`, `next build` | clean |
| `clang-format --dry-run --Werror` | clean |

What the suites cover beyond unit tests:

- **PostgreSQL-backed tests** for every repository, retry-to-success and retry-to-dead-letter
  acceptance tests, and a restart-persistence test.
- **100-record acceptance tests** for Users, Products, and Categories, each from CSV and from a real
  image through Tesseract, reconciling submitted, accepted, and rejected records, jobs, and persisted
  rows exactly.
- **Browser verification** (manual, recorded in the
  [production-readiness report](docs/architecture/phase-3h-production-readiness.md)) of all six
  source × target flows and every category-hierarchy case, cross-checked against PostgreSQL.

```bash
ctest --test-dir build --output-on-failure            # in-memory by default

# Include the PostgreSQL-backed tests (disposable database: tests truncate tables)
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge_test ./scripts/db-migrate.sh
FLOWFORGE_TEST_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge_test \
  ctest --test-dir build --output-on-failure

# Smoke-test a running server (writes real product rows)
python3 tests/e2e/smoke-test.py --api-url http://localhost:8080 --ocr

# Dashboard
npm run lint:dashboard && npm run typecheck:dashboard && npm run build:dashboard
```

## CI/CD

[GitHub Actions](.github/workflows/ci.yml) runs on every push to `master` and on pull requests:

| Job | What it does |
|---|---|
| C++ build + test (Debug, Release) | clang 19, warnings as errors, full CTest run |
| C++ ASan + UBSan | Sanitizer build and full CTest run |
| PostgreSQL integration tests | `postgres:16` service; migration-runner check on a fresh database; migrations; full suite including PostgreSQL-backed and 100-record acceptance tests |
| clang-format check | Formatting of all C++ sources |
| Dashboard lint + typecheck + build | ESLint, `tsc`, `next build` |
| Docker build + runtime smoke test | Builds both images, starts PostgreSQL, runs migrations, starts server + dashboard and waits for their healthchecks, runs `tests/e2e/smoke-test.py --ocr` against the containers |

All jobs passed on `732eefe` (run `36032434131`) and on `8a2ba95` (run `36120702923`). CI does not deploy anywhere; there is no
deployment pipeline.

## Docker

```bash
cp .env.example .env        # set NEXT_PUBLIC_API_URL / CORS origin before building
docker compose up --build -d
docker compose run --rm migrate
```

- Services: `postgres` (16), `server` (`:8080`), `dashboard` (`:3000`), and a one-off `migrate`
  service. Migrations never run implicitly on startup.
- The server image builds with clang 19 on Debian trixie and includes Tesseract; the dashboard image
  uses Next.js standalone output. Both run as non-root users and have `HEALTHCHECK`s (server:
  `GET /ready`; dashboard: `GET /`).
- `NEXT_PUBLIC_API_URL` is a build argument, so rebuild the dashboard image after changing it.
- **Validation status:** the Compose stack is built, started, and smoke-tested in CI. It has not
  been run in the local development environment, which does not have Docker.

## Security considerations

Implemented protections:

- Parameterized SQL for every query; SQL-injection probe strings are stored as inert data
- Row-level input validation, with server-side re-validation on confirm
- Upload and payload limits: 2 MB CSV, 1,000 CSV rows, 6 MB image, 200 OCR rows, 8 MB HTTP body,
  256 KB job payload
- Image type detection by magic bytes (PNG, JPEG, WebP); corrupt images rejected
- OCR subprocess launched with an argument vector (no shell) and a timeout
- Exact-match CORS allowlist, never a wildcard
- Malformed IDs rejected with `400` before any database access; generic messages on `5xx`
- Non-root Docker containers; no secrets committed (`.env` is git-ignored)

> **Authentication and authorization are not currently implemented; FlowForge should be deployed
> behind an appropriate trusted network boundary until identity and access control are added.**

## Known limitations

- **No authentication or authorization.** Every endpoint is open to anyone who can reach it.
- **OCR can produce values that are structurally valid but wrong** (`PRODS` for `PROD5`,
  `personl@…` for `person1@…`). Review the preview before confirming; there is no minimum
  confidence threshold.
- **Single-process execution.** The scheduler, worker pool, and retry dispatcher run inside one
  server process; there are no distributed workers or multi-instance job claiming.
- **Deep category hierarchies in one submission** rely on retries. A chain deeper than the retry
  budget (3 attempts by default) could, with worst-case timing, dead-letter at its deepest level;
  importing level by level avoids this.
- **Worker records accumulate:** each server start registers its worker threads, and records from
  earlier runs are never removed.
- **A job that succeeds on retry keeps the previous attempt's `last_error` text**; its status is
  correct.
- **Workflows, queue management, log querying, and settings editing have no backend.** Their
  dashboard pages say so.
- `/metrics` is plain text, not Prometheus exposition format.
- **Docker has been validated in CI only**, not in local development.

## Roadmap

- Authentication and authorization
- Stronger OCR verification (confidence thresholds, per-field review)
- Distributed workers and multi-instance job claiming
- Workload filtering and search
- Prometheus-format metrics and richer operational analytics
- A documented cloud deployment

## Documentation

| Topic | Document |
|---|---|
| Architecture overview | [`docs/architecture/overview.md`](docs/architecture/overview.md) |
| Execution model (scheduler, worker pool, retries, metrics) | [`docs/architecture/execution-model.md`](docs/architecture/execution-model.md) |
| Workload model | [`docs/architecture/workload-model.md`](docs/architecture/workload-model.md) |
| Input processing (CSV, OCR, preview/confirm) | [`docs/architecture/input-processing.md`](docs/architecture/input-processing.md) |
| User import | [`docs/architecture/user-import.md`](docs/architecture/user-import.md) |
| Product processing | [`docs/architecture/product-processing.md`](docs/architecture/product-processing.md) |
| Category processing | [`docs/architecture/category-processing.md`](docs/architecture/category-processing.md) |
| API reference | [`docs/api/reference.md`](docs/api/reference.md) |
| Developer setup | [`docs/development/getting-started.md`](docs/development/getting-started.md) |
| Deployment | [`docs/operations/deployment.md`](docs/operations/deployment.md) |
| Backup and recovery | [`docs/operations/backup-and-recovery.md`](docs/operations/backup-and-recovery.md) |
| Release checklist | [`docs/operations/release-checklist.md`](docs/operations/release-checklist.md) |
| Production-readiness report | [`docs/architecture/phase-3h-production-readiness.md`](docs/architecture/phase-3h-production-readiness.md) |
| Demo guide | [`docs/demo.md`](docs/demo.md) |

## Why FlowForge

FlowForge is interesting as an engineering project because the parts that are usually mocked are real:

- Jobs run on a **concurrent C++ engine** with bounded queues, explicit backpressure, and cooperative
  cancellation, not a loop over a list.
- **Workloads are orchestrated end to end**: one submission becomes one workload and N jobs, with
  progress derived from job state rather than a counter that can drift.
- **Failures are first-class**: retry policies, a restart-safe retry dispatcher, dead letters, and a
  persisted history of every attempt.
- **Data lands in PostgreSQL** through a repository abstraction, a connection pool, and versioned
  migrations that CI validates.
- **OCR ingestion** feeds the same pipeline as CSV, behind a provider abstraction.
- The **processing architecture is domain-agnostic**: Users, Products, and Categories share one
  extraction and workload path and differ only in small mapping and handler classes.
- A **full-stack dashboard** shows live progress, attempts, and readiness.
- It is **verified end to end**: 654 backend tests, 100-record acceptance tests against PostgreSQL
  and real OCR, a Docker runtime smoke test in CI, and recorded browser verification.

## Project context

Built as a full-stack systems engineering project demonstrating concurrent C++ backend design,
PostgreSQL persistence, workload orchestration, input processing, reliability engineering, and
modern web application development.

The development history, including the defects found through validation and how they were fixed,
is recorded in the [production-readiness report](docs/architecture/phase-3h-production-readiness.md)
and the [Phase 3G audit](docs/architecture/phase-3g-audit.md).
