/*-------------------------------------------------------------------------
 *
 * pg_exclusive_backup.c
 *   provides functions for exclusive backup
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "utils/builtins.h"
#include "utils/pg_lsn.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_start_backup);
PG_FUNCTION_INFO_V1(pg_stop_backup);

/*
 * pg_start_backup: set up for taking an on-line backup dump
 *
 * Essentially what this does is to create a backup label file in $PGDATA,
 * where it will be archived as part of the backup dump.  The label file
 * contains the user-supplied label string (typically this would be used
 * to tell where the backup dump will be stored) and the starting time and
 * starting WAL location for the dump.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_start_backup(PG_FUNCTION_ARGS)
{
	PG_RETURN_LSN(InvalidXLogRecPtr);
}

/*
 * pg_stop_backup: finish taking an on-line backup dump
 *
 * We write an end-of-backup WAL record, and remove the backup label file
 * created by pg_start_backup, creating a backup history file in pg_wal
 * instead (whence it will immediately be archived). The backup history file
 * contains the same info found in the label file, plus the backup-end time
 * and WAL location. Before 9.0, the backup-end time was read from the backup
 * history file at the beginning of archive recovery, but we now use the WAL
 * record for that and the file is for informational and debug purposes only.
 *
 * Note: different from CancelBackup which just cancels online backup mode.
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_stop_backup(PG_FUNCTION_ARGS)
{
	PG_RETURN_LSN(InvalidXLogRecPtr);
}
