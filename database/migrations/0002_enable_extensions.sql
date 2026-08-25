-- pgcrypto provides gen_random_uuid(). It became part of PostgreSQL core
-- in v16 but is still required as an extension on v13-15, which are
-- common enough in the wild that we enable it explicitly rather than
-- assuming core support.
CREATE EXTENSION IF NOT EXISTS pgcrypto;
