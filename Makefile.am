mediaconsole_sources = \
		src/upnp.c \
		src/media-service-upnp.c \
		src/error.c \
		src/props.c \
		src/search.c \
		src/path.c \
		src/task.c \
		src/device.c \
		src/async.c \
		src/sort.c

mediaconsole_headers = \
		src/upnp.h \
		src/error.h \
		src/props.h \
		src/search.h \
		src/path.h \
		src/task.h \
		src/device.h \
		src/async.h \
		src/sort.h


bin_PROGRAMS = media-service-upnp
media_service_upnp_SOURCES = $(mediaconsole_headers) $(mediaconsole_sources)
media_service_upnp_CPPFLAGS = $(GLIB_CFLAGS)  $(GIO_CFLAGS) $(GUPNP_CFLAGS) $(GUPNPAV_CFLAGS)
media_service_upnp_LDADD = $(GLIB_LIBS) $(GIO_LIBS) $(GUPNP_LIBS) $(GUPNPAV_LIBS)

dms_info_sources = \
		test/dms-info.c

noinst_PROGRAMS = dms-info
dms_info_SOURCES = $(dms_info_sources)
dms_info_CPPFLAGS = $(GLIB_CFLAGS)  $(GIO_CFLAGS)
dms_info_LDADD = $(GLIB_LIBS) $(GIO_LIBS)

dbussessiondir = @DBUS_SESSION_DIR@
dbussession_DATA = src/com.intel.media-service-upnp.service

EXTRA_DIST = test/mediaconsole.py

MAINTAINERCLEANFILES =	Makefile.in \
			aclocal.m4 \
			configure \
			config.h.in \
			config.h.in~ \
			build-aux/depcomp \
			build-aux/compile \
			build-aux/missing \
			build-aux/install-sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = media-service-upnp.pc
DISTCLEANFILES = $(pkgconfig_DATA)
