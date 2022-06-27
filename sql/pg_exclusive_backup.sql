CREATE EXTENSION pg_exclusive_backup;

SELECT 1 FROM pg_start_backup('test', true);
SELECT pg_is_in_backup();
SELECT 1 FROM pg_stop_backup();
SELECT pg_is_in_backup();
