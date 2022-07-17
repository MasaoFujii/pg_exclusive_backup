# pg_exclusive_backup
This extension provides functions for exclusive backup on PostgreSQL 15 or later.

pg_exclusive_backup is released under the [PostgreSQL License](https://opensource.org/licenses/postgresql), a liberal Open Source license, similar to the BSD or MIT licenses.

## Install

Download the source archive of pg_exclusive_backup from
[here](https://github.com/MasaoFujii/pg_exclusive_backup),
and then build and install it.

    $ cd pg_exclusive_backup
    $ make USE_PGXS=1 PG_CONFIG=/opt/pgsql-X.Y.Z/bin/pg_config
    $ su
    # make USE_PGXS=1 PG_CONFIG=/opt/pgsql-X.Y.Z/bin/pg_config install
    # exit

USE_PGXS=1 must be always specified when building this extension.
The path to [pg_config](http://www.postgresql.org/docs/devel/static/app-pgconfig.html)
(which exists in the bin directory of PostgreSQL installation)
needs be specified in PG_CONFIG.
However, if the PATH environment variable contains the path to pg_config,
PG_CONFIG doesn't need to be specified.

Note that pg_exclusive_backup requires PostgreSQL 15 or later.

## Functions

**CREATE EXTENSION pg_exclusive_backup** needs to be executed
in all the databases that you want to execute the functions that
this extension provides.

    =# CREATE EXTENSION pg_exclusive_backup;

Note that this CREATE EXTENSION command drops the built-in functions for non-exclusive backup, pg_backup_start and pg_backup_stop, from the database where the command is executed on. Those function remain dropped and unavailable in that database even after pg_exclusive_backup is removed from there by DROP EXTENSION command. To use them for non-exclusive backups, you need to connect to other databases where pg_exclusive_backup has never been installed on yet.

These functions cannot be executed during recovery.

### pg_lsn pg_start_backup(label text [, fast boolean ])
Prepares the server to begin an exclusive on-line backup. The only required parameter is an arbitrary user-defined label for the backup. (Typically this would be the name under which the backup dump file will be stored.) If the optional second parameter is given as true, it specifies executing pg_start_backup as quickly as possible. This forces an immediate checkpoint which will cause a spike in I/O operations, slowing any concurrently executing queries.

This function writes a backup label file (backup_label) and, if there are any links in the pg_tblspc/ directory, a tablespace map file (tablespace_map) into the database cluster's data directory, then performs a checkpoint, and then returns the backup's starting write-ahead log location. (The user can ignore this result value, but it is provided in case it is useful.)

This function is restricted to superusers by default, but other users can be granted EXECUTE to run the function.

### pg_lsn pg_stop_backup([ wait_for_archive boolean ])
Finishes performing an exclusive on-line backup. This function removes the backup label file and, if it exists, the tablespace map file created by pg_start_backup.

There is an optional second parameter of type boolean. If false, the function will return immediately after the backup is completed, without waiting for WAL to be archived. This behavior is only useful with backup software that independently monitors WAL archiving. Otherwise, WAL required to make the backup consistent might be missing and make the backup useless. By default or when this parameter is true, pg_stop_backup will wait for WAL to be archived when archiving is enabled.

This function also creates a backup history file in the write-ahead log archive area. The history file includes the label given to pg_start_backup, the starting and ending write-ahead log locations for the backup, and the starting and ending times of the backup. After recording the ending location, the current write-ahead log insertion point is automatically advanced to the next write-ahead log file, so that the ending write-ahead log file can be archived immediately to complete the backup.

This function returns the pg_lsn result that holds the backup's ending write-ahead log location (which again can be ignored).

This function is restricted to superusers by default, but other users can be granted EXECUTE to run the function.

### boolean pg_is_in_backup()
Returns true if an exclusive on-line backup is in progress.

### timestamp with time zone pg_backup_start_time()
Returns the start time of the current exclusive on-line backup if one is in progress, otherwise NULL.

## Notes

It's recommended that a non-exclusive backup is used unless you have special reason why an exclusive backup needs to be used. Before using pg_exclusive_backup, you should understand the reasons why an exclusive backup was marked deprecated in PostgreSQL 9.6 and eventually removed in 15.

An exclusive backup should be avoided because this type of backup can only be taken on a primary server and does not allow concurrent. Moreover, because it creates a backup label file, which can block restart of the primary server after a crash. On the other hand, the erroneous removal of this file from a backup or standby server is a common mistake, which can result in serious data corruption.

There are some differences an exclusive backup available in PostgreSQL 14 or before and one that pg_exclusive_backup provides.

In PostgreSQL 15 or later with pg_exclusive_backup, while the exclusive backup is in progress, whatever shutdown mode is, any shutdown request causes the server to shut down without removing backup_label and tablespace_map files. Those files must be removed manually from the database cluster before restarting the server. Otherwise the server restart may fail. Also smart shutdown does NOT make the server additionally wait and allow new connections until the exclusive backup is no longer active.

## Steps to take a backup

1. Ensure that WAL archiving is enabled and working.

2. Connect to the database where pg_exclusive_backup is installed on as a user with rights to run pg_start_backup (superuser, or a user who has been granted EXECUTE on the function) and issue the command:
   ```
   SELECT pg_start_backup('label');
   ```
   where ```label``` is any string you want to use to uniquely identify this backup operation. pg_start_backup creates a backup label file, called backup_label, in the cluster directory with information about your backup, including the start time and label string. The function also creates a tablespace map file, called tablespace_map, in the cluster directory with information about tablespace symbolic links in pg_tblspc/ if one or more such link is present. Both files are critical to the integrity of the backup, should you need to restore from it.

   By default, pg_start_backup can take a long time to finish. This is because it performs a checkpoint, and the I/O required for the checkpoint will be spread out over a significant period of time, by default half your inter-checkpoint interval (see checkpoint_completion_target). This is usually what you want, because it minimizes the impact on query processing. If you want to start the backup as soon as possible, use:
   ```
   SELECT pg_start_backup('label', true);
   ```
   This forces the checkpoint to be done as quickly as possible.

3. Perform the backup, using any convenient file-system-backup tool such as tar or cpio (not pg_dump or pg_dumpall.  It is neither necessary nor desirable to stop normal operation of the database while you do this. See [here](https://www.postgresql.org/docs/devel/continuous-archiving.html#BACKUP-LOWLEVEL-BASE-BACKUP-DATA) for things to consider during this backup.

   As noted above, if the server crashes during the backup it may not be possible to restart until the backup_label file has been manually deleted from the $PGDATA directory.  Note that it is very important to never remove the backup_label file when restoring a backup, because this will result in corruption. Confusion about when it is appropriate to remove this file is a common cause of data corruption when using this method; be very certain that you remove the file only on an existing primary and never when building a standby or restoring a backup, even if you are building a standby that will subsequently be promoted to a new primary.

4. Again connect to the database where pg_exclusive_backup is installed on as a user with rights to run pg_stop_backup (superuser, or a user who has been granted EXECUTE on the function), and issue the command:
   ```
   SELECT pg_stop_backup();
   ```
   This function terminates backup mode and performs an automatic switch to the next WAL segment. The reason for the switch is to arrange for the last WAL segment written during the backup interval to be ready to archive.

5. Once the WAL segment files active during the backup are archived, you are done. The file identified by pg_stop_backup's result is the last segment that is required to form a complete set of backup files. If archive_mode is enabled, pg_stop_backup does not return until the last segment has been archived. Archiving of these files happens automatically since you have already configured archive_command or archive_library. In most cases this happens quickly, but you are advised to monitor your archive system to ensure there are no delays. If the archive process has fallen behind because of failures of the archive command or archive library, it will keep retrying until the archive succeeds and the backup is complete.

   When using exclusive backup mode, it is absolutely imperative to ensure that pg_stop_backup completes successfully at the end of the backup. Even if the backup itself fails, for example due to lack of disk space, failure to call pg_stop_backup will leave the server in backup mode indefinitely, causing future backups to fail and increasing the risk of a restart failure during the time that backup_label exists.
