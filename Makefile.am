INCLUDES = -DG_LOG_DOMAIN=\"MSU\"

AM_CFLAGS =	$(GLIB_CFLAGS)		\
		$(GIO_CFLAGS)		\
		$(GUPNP_CFLAGS)		\
		$(GUPNPAV_CFLAGS)	\
		-include config.h

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

media_service_upnp_sources = 	src/async.c		 \
				src/device.c		 \
				src/error.c		 \
				src/media-service-upnp.c \
				src/log.c		 \
				src/path.c		 \
				src/props.c		 \
				src/search.c		 \
				src/sort.c		 \
				src/task.c		 \
				src/upnp.c

media_service_upnp_headers =	src/async.h	\
				src/device.h	\
				src/error.h	\
				src/log.h	\
				src/path.h	\
				src/props.h	\
				src/search.h	\
				src/sort.h	\
				src/task.h	\
				src/upnp.h


bin_PROGRAMS = media-service-upnp

media_service_upnp_SOURCES =	$(media_service_upnp_headers)	\
				$(media_service_upnp_sources)

media_service_upnp_LDADD =	$(GLIB_LIBS)	\
				$(GIO_LIBS)	\
				$(GUPNP_LIBS)	\
				$(GUPNPAV_LIBS)


dms_info_sources = test/dms-info.c

noinst_PROGRAMS = dms-info
dms_info_SOURCES = $(dms_info_sources)

dms_info_CFLAGS =	$(GLIB_CFLAGS)	\
			$(GIO_CFLAGS)

dms_info_LDADD =	$(GLIB_LIBS)	\
			$(GIO_LIBS)


dbussessiondir = @DBUS_SESSION_DIR@
dbussession_DATA = src/com.intel.media-service-upnp.service

EXTRA_DIST = test/mediaconsole.py

MAINTAINERCLEANFILES =	Makefile.in		\
			aclocal.m4		\
			configure		\
			config.h.in		\
			config.h.in~		\
			build-aux/depcomp	\
			build-aux/compile	\
			build-aux/missing	\
			build-aux/install-sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = media-service-upnp.pc

CLEANFILES = $(pkgconfig_DATA) $(dbussession_DATA)
DISTCLEANFILES = $(pkgconfig_DATA) $(dbussession_DATA)

maintainer-clean-local:
	rm -rf build-aux
