#
# spec file for package vhostmd (Version 1.0)
#
# Copyright (c) 2009 Novell Inc.
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#
# Please submit bugfixes or comments via http://bugzilla.novell.com
#


Name:          vhostmd
ExclusiveArch: %ix86 x86_64
License:       GPL
BuildRequires: pkg-config libxml2 libxml2-devel libvirt-devel xen-devel
Summary:       Virtualization Host Metrics Daemon
Version:       0.2      
Release:       1
Group:         System/Daemons
BuildRoot:     %{_tmppath}/%{name}-build
PreReq:        coreutils, %insserv_prereq
AutoReqProv:   on
Source0:       vhostmd-%{version}.tar.bz2

%description 
Daemon vhostmd provides a "metrics communication channel" between a host and
its hosted virtual machines, allowing limited introspection of host
resource usage from within virtual machines.


Authors:
--------
    Jim Fehlig <jfehlig@novell.com>
    Pat Campbell <plc@novell.com>



%package -n    vm-dump-metrics
Summary:       Virtualization Host Metrics Dump 
Group:         System/Monitoring

%description -n vm-dump-metrics
Executable to dump all available virtualization host metrics to stdout
or alternativly an argumented file.


Authors:
--------
    Jim Fehlig <jfehlig@novell.com>
    Pat Campbell <plc@novell.com>


%package -n    vm-dump-metrics-devel
Summary:       Virtualization Host Metrics Dump development 
Group:         Development/Libraries/C and C++
Requires:      vhostmd-vm-dump-metrics* = %{version}

%description -n vm-dump-metrics-devel
Header and libraries necessary for metrics gathering development


Authors:
--------
    Jim Fehlig <jfehlig@novell.com>
    Pat Campbell <plc@novell.com>

%prep
%setup

%build
autoreconf -fi
%configure
make

%preun
# Start of pre-remove script
%{stop_on_removal vhostmd}
# exit 0
# End of pre-remove script

%postun
# Start post-remove script
%restart_on_update vhostmd
%{insserv_cleanup}

%install
# init scripts
make DESTDIR=$RPM_BUILD_ROOT install
ln -s /etc/init.d/vhostmd $RPM_BUILD_ROOT/usr/sbin/rcvhostmd

%post -n vm-dump-metrics-devel
# Start post-insall script
sbin/ldconfig

%postun -n vm-dump-metrics-devel
# Start post-remove script
sbin/ldconfig

%files
%defattr(-,root,root)
%dir /etc/vhostmd
%dir /usr/share/vhostmd
%dir /usr/share/vhostmd/scripts
%dir /usr/share/doc/vhostmd
/usr/sbin/vhostmd
/usr/sbin/rcvhostmd
/usr/share/vhostmd/scripts/pagerate.pl
%config(noreplace) /etc/vhostmd/vhostmd.conf
%config /etc/vhostmd/vhostmd.dtd
%config /etc/vhostmd/metric.dtd
/etc/init.d/vhostmd
/usr/share/doc/vhostmd/vhostmd.dtd
/usr/share/doc/vhostmd/metric.dtd
/usr/share/doc/vhostmd/vhostmd.xml
/usr/share/doc/vhostmd/mdisk.xml
/usr/share/doc/vhostmd/README
/usr/share/man/man8/vhostmd.8.gz

%files -n vm-dump-metrics
%defattr(-,root,root)
/usr/sbin/vm-dump-metrics
/usr/share/man/man1/vm-dump-metrics.1.gz

%files -n vm-dump-metrics-devel
%defattr(-,root,root)
%{_libdir}/libmetrics.a
%{_libdir}/libmetrics.la
%{_libdir}/libmetrics.so
%{_libdir}/libmetrics.so.0
%{_libdir}/libmetrics.so.0.0.0
%dir /usr/include/vhostmd
/usr/include/vhostmd/libmetrics.h

%changelog -n vhostmd
