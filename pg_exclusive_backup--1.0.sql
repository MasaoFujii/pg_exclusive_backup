-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_exclusive_backup" to load this file. \quit

CREATE FUNCTION pg_catalog.pg_start_backup(text, boolean DEFAULT false)
RETURNS pg_lsn
AS 'MODULE_PATHNAME', 'pg_start_backup'
LANGUAGE C VOLATILE STRICT;

REVOKE EXECUTE ON FUNCTION pg_catalog.pg_start_backup(text, boolean) FROM PUBLIC;

CREATE FUNCTION pg_catalog.pg_stop_backup()
RETURNS pg_lsn
AS 'MODULE_PATHNAME', 'pg_stop_backup'
LANGUAGE C VOLATILE STRICT;

REVOKE EXECUTE ON FUNCTION pg_catalog.pg_stop_backup() FROM PUBLIC;

CREATE FUNCTION pg_catalog.pg_is_in_backup()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_is_in_backup'
LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION pg_catalog.pg_backup_start_time()
RETURNS timestamp with time zone
AS 'MODULE_PATHNAME', 'pg_backup_start_time'
LANGUAGE C VOLATILE STRICT;

UPDATE pg_catalog.pg_proc SET oid = 999999999
WHERE proname = 'pg_backup_start' AND prosrc = 'pg_backup_start' AND oid < 12000;
DROP FUNCTION IF EXISTS pg_backup_start(text, boolean);
UPDATE pg_catalog.pg_proc SET oid = 999999999
WHERE proname = 'pg_backup_stop' AND prosrc = 'pg_backup_stop' AND oid < 12000;
DROP FUNCTION IF EXISTS pg_backup_stop(boolean);
