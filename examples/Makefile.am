noinst_PROGRAMS = basic recursive coordinator task-ls

if ENABLE_GTK
noinst_PROGRAMS += progress-widgets progress-groups progress-tasks progress-chains
endif

INCLUDES = -I$(top_srcdir)
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

basic_sources = basic.c
recursive_sources = recursive.c
coordinator_sources = coordinator.c

task_ls_sources = task-ls.c

progress_widgets_sources = progress-widgets.c
progress_groups_sources = progress-groups.c
progress_tasks_sources = progress-tasks.c
progress_chain_sources = progress-chain.c
