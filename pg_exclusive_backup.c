/*-------------------------------------------------------------------------
 *
 * pg_exclusive_backup.c
 *   provides functions for exclusive backup
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/stat.h>

#include "access/xlog.h"
#include "storage/fd.h"
#include "storage/lwlock.h"
#include "utils/builtins.h"
#include "utils/pg_lsn.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_start_backup);
PG_FUNCTION_INFO_V1(pg_stop_backup);

static bool BackupInProgress(bool ignore_failure);
static void ReadFileToStringInfo(const char *filename, StringInfo buf);
static void WriteStringInfoToFile(const char *filename, StringInfo buf);

/*
 * Set up for taking an exclusive on-line backup dump.
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
	text	   *backupid = PG_GETARG_TEXT_PP(0);
	bool		fast = PG_GETARG_BOOL(1);
	char	   *backupidstr;
	XLogRecPtr	startpoint;
	StringInfoData label_file;
	StringInfoData tblspc_map_file;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("pg_start_backup cannot be executed during recovery.")));

	if (BackupInProgress(false))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("exclusive backup is already in progress")));

	backupidstr = text_to_cstring(backupid);

	initStringInfo(&label_file);
	initStringInfo(&tblspc_map_file);

	/*
	 * XXX BACKUP METHOD in the backup label that do_pg_backup_start()
	 * returns is always "streamed" (not "pg_start_backup").
	 */
	startpoint = do_pg_backup_start(backupidstr, fast, NULL, &label_file,
									NULL, &tblspc_map_file);

	/*
	 * While executing do_pg_backup_start(), pg_start_backup() may be
	 * called by different session. To handle this case, confirm that
	 * exclusive backup not in progress before creating backup_label file.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

	if (BackupInProgress(false))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("exclusive backup is already in progress")));

	/*
	 * XXX backup_label file may remain unexpectedly when tablespace_map
	 * file fails to be created and pg_start_backup() reports an error.
	 */
	WriteStringInfoToFile(BACKUP_LABEL_FILE, &label_file);
	if (tblspc_map_file.len > 0)
		WriteStringInfoToFile(TABLESPACE_MAP, &tblspc_map_file);

	LWLockRelease(ControlFileLock);

	pfree(label_file.data);
	pfree(tblspc_map_file.data);

	PG_RETURN_LSN(startpoint);
}

/*
 * Finish taking an exclusive on-line backup dump.
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
	bool		waitforarchive = PG_GETARG_BOOL(0);
	XLogRecPtr	stoppoint;
	StringInfoData label_file;

	if (RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("recovery is in progress"),
				 errhint("pg_stop_backup cannot be executed during recovery.")));

	if (!BackupInProgress(false))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("exclusive backup not in progress")));

	initStringInfo(&label_file);
	ReadFileToStringInfo(BACKUP_LABEL_FILE, &label_file);

	durable_unlink(BACKUP_LABEL_FILE, ERROR);
	durable_unlink(TABLESPACE_MAP, DEBUG1);

	stoppoint = do_pg_backup_stop(label_file.data, waitforarchive, NULL);

	pfree(label_file.data);

	PG_RETURN_LSN(stoppoint);
}

/*
 * Check if exclusive backup is in progress.
 */
static bool
BackupInProgress(bool ignore_failure)
{
	struct stat statbuf;

	if (stat(BACKUP_LABEL_FILE, &statbuf))
	{
		if (errno != ENOENT && !ignore_failure)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not stat file \"%s\": %m",
							BACKUP_LABEL_FILE)));
		return false;
	}

	return true;
}

/*
 * Read file to StringInfo.
 */
static void
ReadFileToStringInfo(const char *filename, StringInfo buf)
{
	struct stat statbuf;
	FILE	   *fp;
	int			r;

	if (stat(filename, &statbuf))
	{
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not stat file \"%s\": %m", filename)));
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));
	}

	fp = AllocateFile(filename, "r");
	if (!fp)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));

	enlargeStringInfo(buf, statbuf.st_size + 1);
	r = fread(buf->data, statbuf.st_size, 1, fp);
	buf->data[statbuf.st_size] = '\0';

	if (r != 1 || ferror(fp) || FreeFile(fp))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));
}

/*
 * Write StringInfo to file.
 */
static void
WriteStringInfoToFile(const char *filename, StringInfo buf)
{
	FILE	   *fp;

	fp = AllocateFile(filename, "w");
	if (!fp)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m", filename)));

	if (fwrite(buf->data, buf->len, 1, fp) != 1 ||
		fflush(fp) != 0 ||
		pg_fsync(fileno(fp)) != 0 ||
		ferror(fp) ||
		FreeFile(fp))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write file \"%s\": %m", filename)));
}