# pg_exclusive_backup
This extension provides functions for exclusive backup on PostgreSQL.

pg_exclusive_backup is released under the [PostgreSQL License](https://opensource.org/licenses/postgresql), a liberal Open Source license, similar to the BSD or MIT licenses.

Note that pg_exclusive_backup requires PostgreSQL 15 or later.

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

## Functions

Note that **CREATE EXTENSION pg_exclusive_backup** needs to be executed
in all the databases that you want to execute the functions that
this extension provides.

    =# CREATE EXTENSION pg_exclusive_backup;

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
