# tests/integration (placeholder)

Reserved for integration tests that require a **real PostgreSQL instance** (via
`docker compose up -d postgres` + `scripts/db-migrate.sh`) once a `libpqxx`-backed
`IJobRepository`/`IWorkflowRepository`/`IWorkerRepository` implementation exists (see
`docs/architecture/overview.md` §6).

This is distinct from `apps/server/tests/http_server_test.cpp`, which is already a real
HTTP-to-repository integration test today — it just runs against the in-memory repository
implementations rather than PostgreSQL, so it needs no external service and runs in CI on every push.
