
INCLUDES = \
    -I../libmetrics

if WITH_XENSTORE
AM_CFLAGS = -DWITH_XENSTORE
endif

sbin_PROGRAMS = vm-dump-metrics
vm_dump_metrics_SOURCES = main.c
vm_dump_metrics_LDADD = ../libmetrics/libmetrics.la $(LIBXML_LIBS) -lxenstore
vm_dump_metrics_LDFLAGS = -static

vm_dump_metrics_DEPENDENCIES = \
    ../libmetrics/libmetrics.h \
    ../libmetrics/libmetrics.la

