INCLUDES = \
	-DSYSCONF_DIR=\""$(DESTDIR)/etc"\" \
    -I../include

sbin_PROGRAMS = vhostmd
vhostmd_SOURCES = vhostmd.c util.c metric.c xenstore-update.c
vhostmd_CFLAGS = $(LIBXML_CFLAGS)
vhostmd_LDADD = -lm $(LIBXML_LIBS)

if XENCTRL
vhostmd_SOURCES += xenctrl-util.c
vhostmd_LDADD += -lxenctrl -lxenstore
endif

if LIBVIRT
vhostmd_SOURCES += virt-util.c
vhostmd_LDADD += $(LIBVIRT_LIBS)
vhostmd_CFLAGS += $(LIBVIRT_CFLAGS)
endif

if LIBXENSTAT
vhostmd_SOURCES += xen-metrics.c
vhostmd_LDADD += ../xenstat/libxenstat/src/libxenstat.a
endif

valgrind:
	$(MAKE) CHECKER='valgrind --quiet --leak-check=full --suppressions=$(srcdir)/.valgrind.supp' tests

tests: $(sbin_PROGRAMS)
	@(echo '## regression tests')
	@($(CHECKER) ./vhostmd -d -v)


