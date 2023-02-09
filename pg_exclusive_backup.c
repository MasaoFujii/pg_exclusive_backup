/*-------------------------------------------------------------------------
 *
 * pg_exclusive_backup.c
 *   provides functions for exclusive backup on PostgreSQL 15 or later
 *
 * Portions Copyright (c) 2022, Fujii Masao
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
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

#if PG_VERSION_NUM >= 160000
#include "access/xlog_internal.h"
#include "utils/timestamp.h"
#endif	/* PG_VERSION_NUM >= 160000 */

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_start_backup);
PG_FUNCTION_INFO_V1(pg_stop_backup);
PG_FUNCTION_INFO_V1(pg_is_in_backup);
PG_FUNCTION_INFO_V1(pg_backup_start_time);

static bool BackupInProgress(bool ignore_failure);
static bool ReadFileToStringInfo(const char *filename, StringInfo buf,
								 bool missing_ok);
static void WriteStringInfoToFile(const char *filename, StringInfo buf);
static void ReplaceStringInfo(StringInfo buf, const char *replace,
							  const char *replacement);

#if PG_VERSION_NUM >= 160000
static void ParseBackupLabelToState(BackupState *state, char *backup_label);
#endif	/* PG_VERSION_NUM >= 160000 */

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

#if PG_VERSION_NUM >= 160000
	BackupState	*backup_state;
	char		*backup_label;
#endif	/* PG_VERSION_NUM >= 160000 */

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

#if PG_VERSION_NUM >= 160000
	backup_state = (BackupState *) palloc0(sizeof(BackupState));

	do_pg_backup_start(backupidstr, fast, NULL, backup_state,
					   &tblspc_map_file);
	startpoint = backup_state->startpoint;

	backup_label = build_backup_content(backup_state, false);
	appendStringInfoString(&label_file, backup_label);

	pfree(backup_state);
	pfree(backup_label);

#else
	startpoint = do_pg_backup_start(backupidstr, fast, NULL, &label_file,
									NULL, &tblspc_map_file);

#endif	/* PG_VERSION_NUM >= 160000 */

	/*
	 * Replace "BACKUP METHOD: streamed" with "... pg_start_backup"
	 * in the backup label because an exclusive backup should use
	 * "pg_start_backup" while do_pg_backup_start() always returns
	 * "streamed".
	 */
	ReplaceStringInfo(&label_file, "BACKUP METHOD: streamed",
					  "BACKUP METHOD: pg_start_backup");

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

#if PG_VERSION_NUM >= 160000
	BackupState	*backup_state;
#endif	/* PG_VERSION_NUM >= 160000 */

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
	ReadFileToStringInfo(BACKUP_LABEL_FILE, &label_file, false);

	durable_unlink(BACKUP_LABEL_FILE, ERROR);
	durable_unlink(TABLESPACE_MAP, DEBUG1);

#if PG_VERSION_NUM >= 160000
	backup_state = (BackupState *) palloc0(sizeof(BackupState));
	ParseBackupLabelToState(backup_state, label_file.data);

	do_pg_backup_stop(backup_state, waitforarchive);
	stoppoint = backup_state->stoppoint;

	pfree(backup_state);

#else
	stoppoint = do_pg_backup_stop(label_file.data, waitforarchive, NULL);

#endif	/* PG_VERSION_NUM >= 160000 */

	pfree(label_file.data);

	PG_RETURN_LSN(stoppoint);
}

/*
 * Returns true if an exclusive on-line backup is in progress.
 */
Datum
pg_is_in_backup(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(BackupInProgress(true));
}

/*
 * Returns start time of an online exclusive backup.
 */
Datum
pg_backup_start_time(PG_FUNCTION_ARGS)
{
	Datum		xtime;
	char		backup_start_time[30];
	StringInfoData label_file;
	char	   *ptr;

	initStringInfo(&label_file);
	if (!ReadFileToStringInfo(BACKUP_LABEL_FILE, &label_file, true))
	{
		pfree(label_file.data);
		PG_RETURN_NULL();
	}

	/* Parse the START TIME line */
	ptr = strstr(label_file.data, "START TIME:");
	if (sscanf(ptr, "START TIME: %25[^\n]\n", backup_start_time) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));

	/* Convert the time string read from file to TimestampTz form */
	xtime = DirectFunctionCall3(timestamptz_in,
								CStringGetDatum(backup_start_time),
								ObjectIdGetDatum(InvalidOid),
								Int32GetDatum(-1));

	pfree(label_file.data);

	PG_RETURN_DATUM(xtime);
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
 *
 * Return true if the specified file has been successfully read. Return false
 * only when missing_ok is true and the specified file is missing.
 */
static bool
ReadFileToStringInfo(const char *filename, StringInfo buf, bool missing_ok)
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

		if (missing_ok)
			return false;
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

	return true;
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

/*
 * Replace all occurrences of "replace" in "buf" with "replacement".
 * The StringInfo will be suitably enlarged if necessary.
 *
 * Note: this is optimized on the assumption that most calls will find
 * no more than one occurrence of "replace", and quite likely none.
 */
static void
ReplaceStringInfo(StringInfo buf, const char *replace, const char *replacement)
{
	int			pos = 0;
	char	   *ptr;

	while ((ptr = strstr(buf->data + pos, replace)) != NULL)
	{
		/* Must copy the remainder of the string out of the StringInfo */
		char	   *suffix = pstrdup(ptr + strlen(replace));

		/* Truncate StringInfo at start of found string ... */
		buf->len = ptr - buf->data;
		/* ... and append the replacement (this restores the trailing '\0') */
		appendStringInfoString(buf, replacement);
		/* Next search should start after the replacement */
		pos = buf->len;
		/* Put back the remainder of the string */
		appendStringInfoString(buf, suffix);
		pfree(suffix);
	}
}

#if PG_VERSION_NUM >= 160000
/*
 * Parse contents of backup_label file to build the backup state.
 */
static void
ParseBackupLabelToState(BackupState *state, char *backup_label)
{
	char		strfbuf[128];
	char		xlogfilename[MAXFNAMELEN];
	char		*ptr;
	uint32		hi,
		lo;
	TimestampTz	ts;

	ptr = backup_label;
	if (!ptr || sscanf(ptr, "START WAL LOCATION: %X/%X (file %24s)\n",
					   &hi, &lo, xlogfilename) != 3)
		goto fail;
	state->startpoint = ((uint64) hi) << 32 | lo;
	ptr = strchr(ptr, '\n') + 1;	/* %n is not portable enough */

	if (!ptr || sscanf(ptr, "CHECKPOINT LOCATION: %X/%X\n",
					   &hi, &lo) != 2)
		goto fail;
	state->checkpointloc = ((uint64) hi) << 32 | lo;
	ptr = strchr(ptr, '\n') + 1;

	if (!ptr || sscanf(ptr, "BACKUP METHOD: %s\n", strfbuf) != 1)
		goto fail;
	ptr = strchr(ptr, '\n') + 1;

	if (!ptr || sscanf(ptr, "BACKUP FROM: %s\n", strfbuf) != 1)
		goto fail;
	state->started_in_recovery = (strcmp(strfbuf, "standby") == 0);
	ptr = strchr(ptr, '\n') + 1;

	if (!ptr || sscanf(ptr, "START TIME: %s\n", strfbuf) != 1)
		goto fail;
	ts = DirectFunctionCall3(timestamptz_in,
							 CStringGetDatum(strfbuf),
							 ObjectIdGetDatum(InvalidOid),
							 Int32GetDatum(-1));
	state->starttime = timestamptz_to_time_t(ts);
	ptr = strchr(ptr, '\n') + 1;

	if (!ptr || sscanf(ptr, "LABEL: %s\n", state->name) != 1)
		goto fail;
	ptr = strchr(ptr, '\n') + 1;

	if (!ptr || sscanf(ptr, "START TIMELINE: %u\n", &(state->starttli)) != 1)
		goto fail;

	return;

fail:
	ereport(ERROR,
			(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
			 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));
}
#endif	/* PG_VERSION_NUM >= 160000 */
