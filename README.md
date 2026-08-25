# FlowForge

FlowForge is a high-performance job processing and workflow orchestration platform built around a
concurrent C++ engine, with a REST API, PostgreSQL persistence, and a Next.js operator dashboard.

This repository is at **Phase 1: foundation**. The architecture, build system, domain model, and a
real (if intentionally narrow) end-to-end job-creation path are implemented and tested. Scheduling,
job execution, workflow orchestration, and PostgreSQL wiring are deliberately not yet implemented —
see [`docs/architecture/overview.md`](docs/architecture/overview.md) for exactly what's real versus
interface-only, and why.

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
| Database | PostgreSQL (schema defined; driver not yet wired) | See `docs/architecture/overview.md` §7 |
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

The schema is defined but no C++ code talks to PostgreSQL yet (see
`docs/architecture/overview.md` §7). To stand up a local database and apply the schema anyway:

```bash
docker compose up -d postgres
cp .env.example .env   # edit POSTGRES_* / FLOWFORGE_DATABASE_URL as needed
docker compose run --rm migrate
# or, without Docker:
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge ./scripts/db-migrate.sh
```

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
ASan+UBSan test run, `clang-format --dry-run`, dashboard lint/typecheck/build, and
`docker compose config` validation.

## Roadmap

**Phase 1 (this phase) — done:** monorepo structure, CMake build system with warnings/sanitizers/
clang-tidy support, domain model, `ThreadPool`/`BlockingQueue` concurrency primitives, typed
config + structured logging + in-memory metrics, `IJobRepository`/`IWorkflowRepository`/
`IWorkerRepository` interfaces with real in-memory implementations, `JobService` with full CRUD +
validation, a real HTTP API (`/health`, `/ready`, `/metrics`, `/api/v1/jobs*`,
`/api/v1/workflows`, `/api/v1/workers`), PostgreSQL schema + migration runner, Next.js dashboard
shell wired to real endpoints where they exist, Docker/Compose, CI.

**Phase 2 (next):**
- `libpqxx`-backed `IJobRepository` (and friends), replacing the in-memory implementations in
  production configuration.
- Scheduler + Executor + WorkerPool implementations on top of the existing `ThreadPool`/
  `BlockingQueue` primitives, with priority ordering and per-queue capacity.
- Job execution: a pluggable handler registry, timeout enforcement, cancellation of in-flight work.
- Retry/backoff wired end-to-end (the pure-function `RetryPolicy::compute_backoff` already exists
  and is tested; nothing calls it on a schedule yet).
- Dead-letter queue handling and operator tooling to inspect/replay dead-lettered jobs.
- Workflow execution: DAG validation (cycle detection), step sequencing.
- Worker process registration/heartbeating as a real deployable (`services/workers/`).
- API authentication/authorization.
- Prometheus-format `/metrics` and a real exporter.

**Later phases:** rate limiting, distributed job claiming across multiple server instances
(`SELECT ... FOR UPDATE SKIP LOCKED`), load/stress testing harness, audit log UI, log aggregation.
