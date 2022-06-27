CREATE EXTENSION pg_exclusive_backup;

SELECT 1 FROM pg_start_backup('test', true);
SELECT pg_is_in_backup();

-- Suppress NOTICE message about WAL archiving that pg_stop_backup()
-- emits because its content can vary depending on WAL archiving
-- configuration in the server.
SET client_min_messages TO 'warning';
SELECT 1 FROM pg_stop_backup();
RESET client_min_messages;

SELECT pg_is_in_backup();
