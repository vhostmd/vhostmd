SUBDIRS = vhostmd libmetrics scripts test vm-dump-metrics include docs

docdir=${prefix}/share/doc/${PACKAGE_TARNAME}
doc_DATA = vhostmd.xml vhostmd.dtd mdisk.xml README metric.dtd

EXTRA_DIST = $(doc_DATA) vhostmd.init vhostmd.spec autogen.sh

AUTOMAKE_OPTIONS=dist-bzip2

install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/vhostmd
	$(MKDIR_P) $(DESTDIR)/etc/init.d
	$(MKDIR_P) $(DESTDIR)/usr/sbin
	$(INSTALL_DATA) $(srcdir)/vhostmd.xml $(DESTDIR)/etc/vhostmd/vhostmd.conf
	$(INSTALL_DATA) $(srcdir)/vhostmd.dtd $(DESTDIR)/etc/vhostmd
	$(INSTALL_DATA) $(srcdir)/metric.dtd $(DESTDIR)/etc/vhostmd
	$(INSTALL_SCRIPT) $(srcdir)/vhostmd.init $(DESTDIR)/etc/init.d/vhostmd
