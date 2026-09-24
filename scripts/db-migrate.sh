#!/usr/bin/env bash
# Applies every not-yet-applied SQL file in database/migrations/, in
# filename order, tracking progress in the schema_migrations table.
#
# This is a deliberately small, dependency-free migration runner (a
# handful of psql invocations) rather than a Node/Go migration framework:
# FlowForge's migrations are plain numbered SQL files, and pulling in a
# separate language's tooling just to run them would be more moving parts
# than the problem warrants. Revisit if down-migrations or branching
# migration history become necessary.
set -euo pipefail

DATABASE_URL="${FLOWFORGE_DATABASE_URL:-${DATABASE_URL:-}}"
if [[ -z "${DATABASE_URL}" ]]; then
  echo "error: set FLOWFORGE_DATABASE_URL (or DATABASE_URL) to a postgres:// connection string" >&2
  exit 1
fi

if ! command -v psql >/dev/null 2>&1; then
  echo "error: psql not found on PATH" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# FLOWFORGE_MIGRATIONS_DIR exists for tests/integration/check-migrations.sh, which
# points the runner at a copy containing a deliberately broken migration to
# prove a failure aborts non-zero. Operators never need to set it.
MIGRATIONS_DIR="${FLOWFORGE_MIGRATIONS_DIR:-${SCRIPT_DIR}/../database/migrations}"

psql "${DATABASE_URL}" -v ON_ERROR_STOP=1 -c \
  "CREATE TABLE IF NOT EXISTS schema_migrations (version TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now());" \
  >/dev/null

applied_count=0
for migration_path in "${MIGRATIONS_DIR}"/*.sql; do
  version="$(basename "${migration_path}" .sql)"
  already_applied="$(psql "${DATABASE_URL}" -t -A -c \
    "SELECT 1 FROM schema_migrations WHERE version = '${version}';")"

  if [[ "${already_applied}" == "1" ]]; then
    echo "skip    ${version} (already applied)"
    continue
  fi

  echo "apply   ${version}"
  # Under Git Bash on Windows, a native psql.exe cannot open /c/... paths;
  # cygpath -m yields the C:/... form it accepts, and MSYS argument
  # conversion must be off or it rewrites "\i C:/..." in transit.
  # No-op on Linux/macOS.
  if command -v cygpath >/dev/null 2>&1; then
    migration_path="$(cygpath -m "${migration_path}")"
    export MSYS2_ARG_CONV_EXCL="*"
  fi
  psql "${DATABASE_URL}" -v ON_ERROR_STOP=1 -1 \
    -c "\\i ${migration_path}" \
    -c "INSERT INTO schema_migrations (version) VALUES ('${version}');" \
    >/dev/null
  applied_count=$((applied_count + 1))
done

echo "done: ${applied_count} migration(s) applied"
