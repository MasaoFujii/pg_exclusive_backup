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
