MODULE_big = pg_recovery
OBJS = \
        $(WIN32RES) \
        recover.o

EXTENSION = pg_recovery
DATA = pg_recovery--1.0.sql
PGFILEDESC = "Read WAL and Perform a Redo"
REGRESS = pg_recovery

ifndef OLD_INSTALL
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_recovery
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
