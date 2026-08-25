-- Minimal development-only seed data. Not applied automatically by
-- scripts/db-migrate.sh -- run explicitly:
--   psql "$FLOWFORGE_DATABASE_URL" -f database/seeds/dev_seed.sql
-- Safe to re-run: uses ON CONFLICT DO NOTHING against the natural key.
INSERT INTO queues (name, capacity, priority) VALUES
    ('default', 1024, 0),
    ('emails', 512, 5),
    ('reports', 256, 0)
ON CONFLICT (name) DO NOTHING;
