INCLUDES = \
	-DSYSCONF_DIR=\""$(DESTDIR)/etc"\" \
    -I../include

sbin_PROGRAMS = vhostmd
vhostmd_SOURCES = vhostmd.c util.c metric.c virt-util.c
vhostmd_CFLAGS = $(LIBXML_CFLAGS) $(LIBVIRT_CFLAGS)
vhostmd_LDADD = -lm $(LIBXML_LIBS) $(LIBVIRT_LIBS)

if WITH_XENSTORE
vhostmd_SOURCES += xenstore-update.c
vhostmd_LDADD += -lxenstore
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


