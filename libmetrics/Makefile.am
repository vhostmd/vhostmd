INCLUDES = \
    -I../include

AM_CFLAGS = $(LIBXML_CFLAGS)
if WITH_XENSTORE
AM_CFLAGS += -DWITH_XENSTORE
endif

lib_LTLIBRARIES=libmetrics.la

libmetricsincdir=$(includedir)/vhostmd
libmetricsinc_HEADERS = libmetrics.h

libmetrics_la_SOURCES =  \
     libmetrics.c \
     vm_metrics.c \
     host_metrics.c \
     libmetrics.h

libmetrics_la_DEPENDENCIES = \
     libmetrics.h

