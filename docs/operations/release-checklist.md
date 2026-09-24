# Release checklist

Run through this before tagging/shipping a FlowForge release. Each item names the exact command or
check — this is meant to be actually run, not just read.

## Git

- [ ] `git status` is clean (only intentional, reviewed changes staged).
- [ ] `git diff --stat` reviewed — no unrelated files touched.
- [ ] `git diff` reviewed in full for anything touching secrets, credentials, or unrelated config.
- [ ] Commit message accurately describes the change (no "fix stuff" — see this repo's existing
      commit history for the expected level of detail).

## Tests

- [ ] `ctest --test-dir build --output-on-failure` — full backend suite passes with **0 failures**.
- [ ] PostgreSQL-backed tests actually ran, not skipped — check for `GTEST_SKIP` in the output; if
      present, `FLOWFORGE_TEST_DATABASE_URL` is not set and you are not getting real coverage.
- [ ] The six `*BulkPostgresTest` 100+-record acceptance tests specifically passed
      (`--gtest_filter='*BulkPostgresTest*'`) — these are the strongest signal that the
      upload→execute→persist pipeline actually works end-to-end.
- [ ] `npm run lint --workspace=apps/dashboard`, `npm run typecheck --workspace=apps/dashboard`,
      `npm run build --workspace=apps/dashboard` all clean.
- [ ] `clang-format --dry-run --Werror` clean across `engine`, `apps/server`, `benchmarks`.

## Build

- [ ] A clean `cmake -B build ... && cmake --build build -j` succeeds with no warnings promoted to
      errors (`FLOWFORGE_WARNINGS_AS_ERRORS=ON` matches CI).
- [ ] `flowforge_server` binary starts and reaches "application ready to accept work" in its logs
      against a real (even if local/disposable) PostgreSQL database.

## Migrations

- [ ] Every new migration file follows the existing numbered-SQL convention (never edit an
      already-applied migration — see `docs/operations/backup-and-recovery.md`, "Migration
      strategy").
- [ ] `scripts/db-migrate.sh` (or `.ps1` on Windows) run against a **fresh** throwaway database and
      confirmed to create the expected tables (`\dt` in `psql`) — don't trust the script's own
      "done: N migration(s) applied" output alone; verify the schema actually changed.
- [ ] The same script run a second time against the now-migrated database applies 0 migrations
      (idempotency check).

## Environment / configuration

- [ ] `.env.example` still lists every environment variable the server reads (`grep -oP
      'getenv_fn\("\K[^"]+' engine/src/infra/config.cpp` and diff against `.env.example`).
- [ ] No real credentials, API keys, or connection strings anywhere in the diff or in
      `.env.example`.
- [ ] `FLOWFORGE_CORS_ALLOWED_ORIGIN` in your actual deployment config is the real dashboard
      origin, not `http://localhost:3000`.
- [ ] `FLOWFORGE_ENV=production` (or `staging`) with `FLOWFORGE_DATABASE_URL` unset correctly
      refuses to start (`AppConfigTest.ProductionRequiresDatabaseUrl` covers this — re-confirm
      manually if you touched `config.cpp`).

## Secrets

- [ ] `git diff` contains no passwords, tokens, or private keys.
- [ ] `.env` (the real one, if it exists locally) is git-ignored — `git check-ignore .env` prints
      the path.

## Database

- [ ] Backup taken of any database you're about to migrate that holds data you cannot afford to
      lose (see `docs/operations/backup-and-recovery.md`).
- [ ] Migrations applied to the target database (`scripts/db-migrate.sh`/`.ps1`).

## Health / readiness

- [ ] `curl $API_URL/health` returns `{"status":"ok"}`.
- [ ] `curl $API_URL/ready` returns `200` with every check (`database`, `scheduler`, `worker_pool`,
      `retry_dispatcher`) `"ok"` — not just a 200 status, actually inspect the body.

## Docker (if deploying via Docker)

- [ ] CI's `docker-validate` job is green for the commit being released (it builds both images,
      starts the stack with migrations, waits for healthchecks, and runs the smoke test with OCR).
- [ ] `NEXT_PUBLIC_API_URL` was set for the *build* of the dashboard image you are deploying.
- [ ] After deploying: `python3 tests/e2e/smoke-test.py --api-url <api> --dashboard-url <dashboard>`
      passes against the real environment (add `--ocr` if image processing is in use). Note it
      writes 95–195 real product rows (`PROD-*`/`PROD*` SKUs) — run it against staging, or accept
      those rows.

## Backup / monitoring / logging

- [ ] A backup schedule exists for the target database (see
      `docs/operations/backup-and-recovery.md`) — FlowForge does not provide one automatically.
- [ ] `FLOWFORGE_STRUCTURED_LOGGING=true` set if logs feed an aggregator.
- [ ] `FLOWFORGE_LOG_LEVEL` set appropriately for the environment (`info` for production is a
      reasonable default; avoid `debug`/`trace` in production — they are not designed to scrub
      sensitive payload content).
- [ ] `/metrics` reachable by whatever scraper/dashboard you use to watch the service (no
      Prometheus exposition format yet — see `docs/architecture/execution-model.md` §20.1).

## Rollback considerations

- [ ] You know how you would roll back the **application** (previous binary/image + `git revert`).
- [ ] You understand FlowForge has **no schema rollback tooling** — rolling back a release that
      included a new migration means either (a) the new migration is additive and safe to leave
      applied even when running the previous binary, or (b) you restore from a pre-migration
      backup. Decide which before shipping, not after something goes wrong.

## Known limitations to communicate

- [ ] Anyone deploying this release outside a trusted network has been told: no
      authentication/authorization exists.
- [ ] Anyone relying on multi-instance deployment has been told: not currently supported
      (single-process worker pool/retry dispatcher).
