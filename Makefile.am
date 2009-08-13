SUBDIRS = vhostmd libmetrics scripts test vm-dump-metrics include docs

docdir=${prefix}/share/doc/${PACKAGE_TARNAME}
doc_DATA = vhostmd.xml vhostmd.dtd mdisk.xml README metric.dtd

EXTRA_DIST = $(doc_DATA) vhostmd.init vhostmd.spec autogen.sh

AUTOMAKE_OPTIONS=dist-bzip2

install-data-local:
	$(mkinstalldirs) $(DESTDIR)/etc/vhostmd
	$(mkinstalldirs) $(DESTDIR)/etc/init.d
	$(mkinstalldirs) $(DESTDIR)/usr/sbin
	-@INSTALL@ -m 0644 $(srcdir)/vhostmd.xml $(DESTDIR)/etc/vhostmd/vhostmd.conf
	-@INSTALL@ -m 0644 $(srcdir)/vhostmd.dtd $(DESTDIR)/etc/vhostmd
	-@INSTALL@ -m 0644 $(srcdir)/metric.dtd $(DESTDIR)/etc/vhostmd
	-@INSTALL@ -m 0755 $(srcdir)/vhostmd.init $(DESTDIR)/etc/init.d/vhostmd

