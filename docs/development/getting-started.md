# Getting started

This is the contributor-facing companion to the root [README.md](../../README.md) build/test
instructions — read that first for commands. This page covers day-to-day workflow notes that don't
belong in the README.

## Windows-specific: compiler choice

If you're on Windows using the [WinLibs](https://winlibs.com/) UCRT+LLVM distribution (GCC + clang
bundled together), **configure CMake with `g++`, not `clang++`**. clang targeting
`x86_64-w64-mingw32` against this libstdc++ build has a reproducible linker bug affecting any binary
that transitively uses `std::call_once` (which `std::future`, and consequently `ThreadPool::submit`,
uses internally) — see `docs/architecture/overview.md` §11 for the full explanation and the exact
error signature. clang is still useful on Windows for `clang-format` and `clang-tidy`; just don't use
it as `CMAKE_CXX_COMPILER` there.

## Linux/macOS: clang 19 or newer

With libstdc++, clang must be **version 19 or newer**: libstdc++ only exposes `std::expected`
(behind `flowforge::Result`, used throughout) to clang ≥ 19. Older clang fails with
`no member named 'unexpected' in namespace 'std'` — this is why CI installs `clang-19` rather than
Ubuntu 24.04's default clang 18, and why the server image builds on Debian trixie.

## PostgreSQL setup

FlowForge builds a real, libpqxx-backed persistence layer (`FLOWFORGE_WITH_POSTGRES`, default `ON`)
whenever `libpq` is available on the machine — see `docs/architecture/overview.md` §7 for the
architecture. If `libpq` isn't found, CMake configure prints a `WARNING` and disables it for that
configure; the build still succeeds, just with in-memory repositories only.

**Linux (Debian/Ubuntu):**

```bash
sudo apt-get install libpq-dev
```

**macOS (Homebrew):**

```bash
brew install libpq
```

**Windows:** install PostgreSQL itself (this also provides `libpq`) via the
[official installer](https://www.postgresql.org/download/windows/) or:

```powershell
winget install PostgreSQL.PostgreSQL.17
```

Then make sure `libpq.dll` is reachable at runtime — add PostgreSQL's `bin` directory (e.g.
`C:\Program Files\PostgreSQL\17\bin`) to `PATH`, **after** the compiler's `bin` directory.
PostgreSQL's `bin` also contains its own `libwinpthread-1.dll`, a different build from the
toolchain's. If Windows loads that copy first, FlowForge binaries run against a threading runtime
they were not built with: during the Phase 3I release run, `ThreadPoolTest.RunsAllSubmittedTasks`
deadlocked inside that DLL's `pthread_cond_signal` (every thread was blocked inside
`libwinpthread`; no FlowForge lock was held). With the toolchain first on `PATH`, the same
concurrency tests passed 30 repeats in a row. See `docs/architecture/overview.md` §11 ("Toolchain
notes") for why Windows/MinGW needs `libpq.lib` specifically rather than `libpq.a`, and why that's
handled automatically by `CMakeLists.txt` rather than something you need to configure by hand.

### Local database

```bash
# Docker (recommended):
docker compose up -d postgres

# Or connect to any PostgreSQL instance (including a local install) and create
# a role/database matching .env.example's FLOWFORGE_DATABASE_URL, e.g.:
psql -U postgres -c "CREATE ROLE flowforge LOGIN PASSWORD 'flowforge';"
psql -U postgres -c "CREATE DATABASE flowforge OWNER flowforge;"
```

### Migrations

```bash
cp .env.example .env   # edit FLOWFORGE_DATABASE_URL if needed
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge ./scripts/db-migrate.sh
# Windows PowerShell: .\scripts\db-migrate.ps1 (same env var)
```

Idempotent — safe to re-run; already-applied migrations are skipped (tracked in the
`schema_migrations` table).

### Running the server against PostgreSQL

```bash
FLOWFORGE_ENV=development \
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge \
  ./build/apps/server/flowforge_server
```

`FLOWFORGE_DATABASE_URL` is the only thing that decides the persistence backend (see
`docs/architecture/overview.md` §7.7) — unset it (the default in `development`/`test`) to run against
in-memory repositories instead. Staging/production always require it (`AppConfig::load` fails startup
otherwise).

### Running PostgreSQL integration tests

The engine's PostgreSQL repository tests (`engine/tests/persistence/postgres/`) and the server's
restart-persistence acceptance test (`HttpServerPostgresPersistenceTest` in
`apps/server/tests/http_server_test.cpp`) are **opt-in**: they run against a real PostgreSQL instance,
never a mock, and skip (report `SKIPPED`, not silently "pass") unless a test database is configured.

```bash
# Use a dedicated, disposable database -- these tests TRUNCATE every table they touch.
FLOWFORGE_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge_test ./scripts/db-migrate.sh

export FLOWFORGE_TEST_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge_test
ctest --test-dir build --output-on-failure
```

`FLOWFORGE_TEST_DATABASE_URL` is checked first, falling back to `FLOWFORGE_DATABASE_URL` if unset, so
CI (which only sets one) doesn't need both.

### Running in-memory-only tests

This is the default — no PostgreSQL, no environment variable needed:

```bash
ctest --test-dir build --output-on-failure
```

## Verifying the server is running (Phase 2B-5)

Once `flowforge_server` is running (see the root [`README.md`](../../README.md), "Running the
server"), three endpoints tell you what's actually going on -- see
[`execution-model.md`](../architecture/execution-model.md) §20.4 for the full contract:

```bash
curl http://localhost:8080/health   # liveness -- always {"status":"ok"} if the process is up
curl http://localhost:8080/ready    # readiness -- reflects real dependency state, see below
curl http://localhost:8080/metrics  # plain-text counters/gauges/histograms
```

`/health` and `/ready` answer different questions on purpose. `/health` only means "the process is
alive and the HTTP server can respond" -- it never checks PostgreSQL or anything else, so an
unhealthy database can't take your liveness probe (and therefore your whole process, via a restart
loop) down with it. `/ready` means "this process can actually accept and process work right now":
it checks PostgreSQL connectivity, the scheduler, the worker pool, and the retry dispatcher, and
returns a real `503` -- not a lying `200` -- the moment any of them isn't usable:

```json
// GET /ready when everything is healthy -- HTTP 200
{"status":"ok","environment":"development","uptime_seconds":42,
 "checks":{"database":"ok","scheduler":"ok","worker_pool":"ok","retry_dispatcher":"ok"}}

// GET /ready when PostgreSQL is unreachable -- HTTP 503
{"status":"unavailable","environment":"development","uptime_seconds":42,
 "checks":{"database":"unavailable","scheduler":"ok","worker_pool":"ok","retry_dispatcher":"ok"}}
```

If `FLOWFORGE_DATABASE_URL` is unset (in-memory persistence, the local development default),
`database` always reports `"ok"` -- there's no external dependency to be unavailable.

## Repository layout for contributors

- Adding a new engine source file? Add it to `engine/CMakeLists.txt`'s explicit source list (not a
  glob — see the comment there for why) and mirror the `include/flowforge/<area>/` /
  `src/<area>/` split.
- Adding a new engine unit test? Add the `.cpp` to `engine/tests/CMakeLists.txt`'s source list.
  `gtest_discover_tests` picks up new `TEST`/`TEST_F` cases automatically once the file is listed.
- New HTTP route? Add a `register_*_routes` function under `apps/server/src/http/routes/`, wire it
  into `App::register_routes` (`apps/server/src/http/app.cpp`), and add coverage in
  `apps/server/tests/http_server_test.cpp`.
- New database table? Add the next-numbered file to `database/migrations/`; never edit an already
  merged migration file — add a new one that alters the table instead (see any production migration
  workflow for why: applied migrations are effectively immutable history).

## Editor setup

`compile_commands.json` is generated at `build/compile_commands.json` on every configure
(`CMAKE_EXPORT_COMPILE_COMMANDS ON` in the root `CMakeLists.txt`) — point clangd/VS Code's C++
extension at it for accurate IntelliSense/diagnostics.
