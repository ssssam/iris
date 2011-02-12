noinst_PROGRAMS = basic recursive coordinator task-ls

if ENABLE_GTK
noinst_PROGRAMS += \
	progress-widgets-gtk \
	progress-groups-gtk \
	progress-tasks-gtk \
	progress-chains-gtk
endif

INCLUDES = -I$(top_srcdir)/iris
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

if ENABLE_GTK
INCLUDES += -I$(top_srcdir)/iris-gtk
LDADD += $(top_builddir)/iris-gtk/libiris-gtk-1.0.la

AM_CFLAGS += $(GTK_CFLAGS)
AM_LDFLAGS += $(GTK_LIBS)
endif

basic_sources = basic.c
recursive_sources = recursive.c
coordinator_sources = coordinator.c

task_ls_sources = task-ls.c

progress_widgets_gtk_sources = progress-widgets-gtk.c
progress_groups_gtk_sources = progress-groups-gtk.c
progress_tasks_gtk_sources = progress-tasks-gtk.c
progress_chain_gtk_sources = progress-chain-gtk.c
