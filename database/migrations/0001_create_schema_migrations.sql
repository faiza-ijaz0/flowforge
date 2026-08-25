-- Bootstrap table tracking which migrations have been applied. Every
-- other migration file assumes this table exists; scripts/db-migrate.sh
-- creates it automatically before applying anything else, but it is also
-- captured here as migration 0001 so the full schema history is visible
-- in one place and `psql -f` against a fresh database works unmodified.
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     TEXT PRIMARY KEY,
    applied_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
