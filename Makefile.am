SUBDIRS = vhostmd libmetrics scripts test vm-dump-metrics include docs

docdir=${prefix}/share/doc/${PACKAGE_TARNAME}
doc_DATA = vhostmd.xml vhostmd.dtd mdisk.xml README metric.dtd

EXTRA_DIST = $(doc_DATA) vhostmd.init vhostmd.spec autogen.sh

AUTOMAKE_OPTIONS=dist-bzip2

install-data-local: install-init-systemv install-init-systemd
	$(MKDIR_P) $(DESTDIR)/etc/vhostmd
	$(INSTALL_DATA) $(srcdir)/vhostmd.xml $(DESTDIR)/etc/vhostmd/vhostmd.conf
	$(INSTALL_DATA) $(srcdir)/vhostmd.dtd $(DESTDIR)/etc/vhostmd
	$(INSTALL_DATA) $(srcdir)/metric.dtd $(DESTDIR)/etc/vhostmd

uninstall-local: uninstall-init-systemv uninstall-init-systemd
	rm -f $(DESTDIR)/etc/vhostmd/vhostmd.conf
	rm -f $(DESTDIR)/etc/vhostmd/vhostmd.dtd
	rm -f $(DESTDIR)/etc/vhostmd/metric.dtd
	rmdir $(DESTDIR)/etc/vhostmd || :

if INIT_SCRIPT_SYSTEMV
install-init-systemv: $(srcdir)/vhostmd.init
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/init.d
	$(INSTALL_SCRIPT) $(srcdir)/vhostmd.init $(DESTDIR)/etc/init.d/vhostmd

uninstall-init-systemv:
	rm -f $(DESTDIR)$(sysconfdir)/init.d/vhostmd
	rmdir $(DESTDIR)$(sysconfdir)/init.d || :
else ! INIT_SCRIPT_SYSTEMV
install-init-systemv:
uninstall-init-systemv:
endif ! INIT_SCRIPT_SYSTEMV

if INIT_SCRIPT_SYSTEMD
SYSTEMD_UNIT_DIR = $(prefix)/lib/systemd/system
install-init-systemd: $(srcdir)/vhostmd.service
	$(MKDIR_P) $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	$(INSTALL_DATA) $(srcdir)/vhostmd.service $(DESTDIR)$(SYSTEMD_UNIT_DIR)/vhostmd.service

uninstall-init-systemd:
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/vhostmd.service
	rmdir $(DESTDIR)$(SYSTEMD_UNIT_DIR) || :
else ! INIT_SCRIPT_SYSTEMD
install-init-systemd:
uninstall-init-systemd:
endif ! INIT_SCRIPT_SYSTEMD
