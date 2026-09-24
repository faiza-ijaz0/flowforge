# Deployment

This document describes how to deploy FlowForge. It does not claim any specific deployment has been
performed — see `docs/architecture/phase-3h-production-readiness.md` for exactly what has and has
not been validated (in particular: Docker has never been runtime-tested, only statically validated —
§10 of that document).

## Prerequisites

- PostgreSQL 14+ (developed/tested against `postgres:16-bookworm`).
- A host that can run the `flowforge_server` binary (Linux x86_64 is what CI builds and tests; the
  server has also been built and run on Windows/MinGW during development — see
  `docs/development/getting-started.md` for the Windows-specific clang/g++ note).
- Node.js 22+ if deploying the dashboard outside Docker.
- `libpq` (PostgreSQL client library) available at runtime for the server binary.
- Tesseract OCR installed on the server host if image/screenshot processing is required — its
  absence is handled gracefully (image/screenshot uploads return a clear "not supported" error
  instead of crashing or silently failing), so it is optional, not required, for a working
  deployment.

## Environment variables

See `.env.example` for the complete, current list (17 variables as of Phase 3H — verified against
every `getenv_fn(...)` call site in `engine/src/infra/config.cpp`, no gaps found). Highlights:

- `FLOWFORGE_ENV` — `development | test | staging | production`. **`staging`/`production` refuse to
  start without `FLOWFORGE_DATABASE_URL` set** (`AppConfig::load` validates this explicitly) —
  there is no silent fallback to in-memory persistence in those environments.
- `FLOWFORGE_DATABASE_URL` — `postgres://user:password@host:port/dbname`. Never commit a real value;
  `.env` is git-ignored.
- `FLOWFORGE_CORS_ALLOWED_ORIGIN` — the dashboard's exact origin. Must match exactly (no wildcard
  support, by design) — set this to your real dashboard URL in any deployment where the dashboard
  is not on `http://localhost:3000`.
- `NEXT_PUBLIC_API_URL` (dashboard-side) — the server's externally-reachable URL.

**Production CORS**: set `FLOWFORGE_CORS_ALLOWED_ORIGIN` to your dashboard's real origin (e.g.
`https://flowforge.example.com`), not `http://localhost:3000`. An empty value disables CORS headers
entirely (useful only if the dashboard and API are served from the exact same origin, e.g. behind a
shared reverse-proxy path).

## Database setup and migrations

```bash
# 1. Point at your real database
export FLOWFORGE_DATABASE_URL=postgres://flowforge:REAL_PASSWORD@your-db-host:5432/flowforge

# 2. Apply every migration (idempotent -- safe to re-run)
./scripts/db-migrate.sh          # Linux/macOS/CI
# or
powershell -File scripts/db-migrate.ps1   # Windows
```

See `docs/operations/backup-and-recovery.md` for migration/rollback strategy. Migrations are never
run automatically by `docker compose up` or by the server binary itself — this is a deliberate,
explicit, operator-triggered step.

## Backend startup

```bash
FLOWFORGE_ENV=production \
FLOWFORGE_DATABASE_URL=postgres://... \
FLOWFORGE_CORS_ALLOWED_ORIGIN=https://your-dashboard.example.com \
FLOWFORGE_STRUCTURED_LOGGING=true \
./build/apps/server/flowforge_server
```

Startup fails fast and loudly (non-zero exit, a `critical`-level log line naming the failure) if:
PostgreSQL is unreachable, the worker pool fails to start, the scheduler fails to start, or the
retry dispatcher fails to start. There is no partial-startup state — either every component started
or the process exits.

## Dashboard startup

```bash
npm ci
npm run build --workspace=apps/dashboard
NEXT_PUBLIC_API_URL=https://your-api.example.com PORT=3000 node apps/dashboard/.next/standalone/apps/dashboard/server.js
```

(The `infra/docker/Dockerfile.dashboard` build produces exactly this standalone server layout.)

## Docker deployment

```bash
cp .env.example .env   # edit for your real environment
docker compose up --build -d
docker compose run --rm migrate   # migrations are explicit, not automatic
```

Starts PostgreSQL, the server (`:8080`), and the dashboard (`:3000`). Both `server` and `dashboard`
containers run as non-root users and have `HEALTHCHECK` instructions (server: `GET /ready`;
dashboard: `GET /`) — `docker compose ps` will show `healthy`/`unhealthy` accordingly once the stack
is actually running somewhere Docker is available.

**Docker has not been runtime-validated in this project's CI or local development as of Phase
3H** — CI's `docker-validate` job only runs `docker compose config --quiet` (static config
parsing), never an actual `docker compose up`. If you deploy via Docker, this is the first time
these Dockerfiles/compose file will have actually been exercised end-to-end; watch the container
logs and `docker compose ps` health status closely on first deploy, and consider filing/fixing
anything you find (see the production-readiness report's "Deferred Work").

## Health checks and readiness checks

- `GET /health` — liveness only (`{"status":"ok"}`, always 200 if the process can respond at all).
  Use for "is the process alive" checks (e.g. a process supervisor's restart trigger).
- `GET /ready` — real readiness: checks PostgreSQL, the scheduler, the worker pool, and the retry
  dispatcher. Returns **503** the instant any one is unavailable — never a lying 200. Use this for
  load-balancer/orchestrator traffic-routing decisions (don't send traffic to an instance that
  isn't actually ready to do work), not `/health`.
- `GET /metrics` — plain-text metrics (not Prometheus exposition format — see
  `docs/architecture/execution-model.md` §20.1 for why that's a deliberate, documented simplification
  rather than an oversight).

## Log handling

`FLOWFORGE_STRUCTURED_LOGGING=true` emits one JSON object per log line — set this in any deployment
feeding logs to an aggregator (CloudWatch, Datadog, ELK, etc.) that expects structured input.
`FLOWFORGE_LOG_LEVEL` controls verbosity (`trace|debug|info|warn|error|critical|off`); `info` is a
reasonable production default. Every 5xx HTTP response body is scrubbed to a generic message before
leaving the process — the real diagnostic detail (including the original exception text) only ever
reaches the log stream, never the client.

## Shutdown

`App::stop()` performs an explicit, ordered graceful shutdown: HTTP listener stops accepting new
connections, then the retry dispatcher stops (so it stops handing retried jobs to the scheduler),
then the scheduler stops (so it stops handing new jobs to the worker pool), then the worker pool
stops — draining and finishing whatever it already has queued before joining its threads. Send
`SIGTERM`/`SIGINT` (handled by `apps/server/src/main.cpp`) rather than `SIGKILL` to get this
behavior; a hard kill will not run the drain sequence and may leave in-flight jobs in `running`
status indefinitely (they will need manual investigation on restart — nothing currently detects and
resets a stuck `running` job).

## Backup considerations

See `docs/operations/backup-and-recovery.md`. Summary: back up PostgreSQL on whatever schedule your
data-loss tolerance requires; there is no other durable state to back up.

## Known limitations

- No authentication/authorization — deploy only behind a trusted network boundary (VPN, private
  subnet, or an auth-enforcing reverse proxy) unless you add one yourself. See
  `docs/architecture/phase-3h-production-readiness.md` §12.
- No distributed/multi-instance worker coordination — `LocalWorkerPool` and `RetryDispatcher` are
  both single-process. Running multiple `flowforge_server` instances against the same database is
  untested and not currently a supported topology (each instance's scheduler/worker pool/retry
  dispatcher would operate independently against shared `jobs` rows with no coordination).
- Docker deployment is described here but has not been runtime-validated (§ above).
- A malformed (non-UUID) ID in a lookup URL currently returns HTTP 500 rather than 400 — see the
  production-readiness report §13. Not a security issue; a minor API-contract rough edge.

This deployment guide describes a **production-style, single-instance** deployment. It does not
describe (because FlowForge does not implement) Kubernetes manifests, autoscaling, or multi-region
topology — do not infer support for those from this document.
