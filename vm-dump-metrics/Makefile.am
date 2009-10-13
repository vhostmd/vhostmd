
INCLUDES = \
    -I../libmetrics

if WITH_XENSTORE
AM_CFLAGS = -DWITH_XENSTORE
endif

sbin_PROGRAMS = vm-dump-metrics
vm_dump_metrics_SOURCES = main.c
vm_dump_metrics_LDADD = ../libmetrics/libmetrics.la $(LIBXML_LIBS)
vm_dump_metrics_LDFLAGS = -static
if WITH_XENSTORE
vm_dump_metrics_LDADD += -lxenstore
endif

vm_dump_metrics_DEPENDENCIES = \
    ../libmetrics/libmetrics.h \
    ../libmetrics/libmetrics.la

