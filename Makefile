MODULE_big = pg_exclusive_backup
OBJS = \
	$(WIN32RES) \
	pg_exclusive_backup.o

EXTENSION = pg_exclusive_backup
DATA = pg_exclusive_backup--1.0.sql
PGFILEDESC = "pg_exclusive_backup - provides functions for exclusive backup on PostgreSQL 15 or later"

REGRESS = pg_exclusive_backup

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_exclusive_backup
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
