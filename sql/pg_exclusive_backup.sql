CREATE EXTENSION pg_exclusive_backup;

SELECT pg_start_backup('test');
SELECT pg_stop_backup();
