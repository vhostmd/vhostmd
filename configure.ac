AC_INIT(Virtual Host Metrics Daemon, 0.5, jfehlig@novell.com, vhostmd)

AC_CONFIG_SRCDIR(vhostmd/vhostmd.c)
AC_CONFIG_HEADERS([config.h])

AC_GNU_SOURCE
AM_INIT_AUTOMAKE

AC_PROG_LIBTOOL

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h paths.h stdlib.h string.h syslog.h unistd.h])

CFLAGS="-Wall $CFLAGS"

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([dup2 strdup strerror strtol])

topdir=`pwd`
AC_SUBST(topdir)

AC_CONFIG_FILES([Makefile])


# Checks for programs.
AM_PROG_CC_C_O

AC_C_CONST
AC_PROG_INSTALL

PKG_CHECK_MODULES([LIBXML], [libxml-2.0])

DEBUG_CFLAGS="-g -O0 -Werror -Wextra -Wimplicit -Wreturn-type -Wunused -Wformat -DENABLE_DEBUG "
# Add a configure argument to support debug mode
AC_ARG_ENABLE([debug],
[  --enable-debug     enable debugging mode],
  [
         CFLAGS="$CFLAGS $DEBUG_CFLAGS"
  ])

# Add a configure argument to support using xen library, libxenstat 
AC_ARG_ENABLE([libxenstat],
[  --enable-libxenstat     enable libxenstat usage],
  [case "${enableval}" in
   yes) libxenstat=true 
        CFLAGS="$CFLAGS -DLIBXENSTAT"
        ;;
   no) libxenstat=false ;;
   *) AC_MSG_ERROR(bad value ${enableval} for --enable-libxenstat) ;;
  esac],[AC_CHECK_HEADER(xenstat.h, libxenstat=true, libxenstat=false)])
AM_CONDITIONAL(LIBXENSTAT, test x$libxenstat = xtrue)

# Configure argument to support using xenctrl library for vm enumeration 
AC_ARG_ENABLE([xenctrl],
[  --enable-xenctrl      use xenctrl specific vm enumeration instead of libvirt],
  [case "${enableval}" in
   yes) xenctrl=true
        CFLAGS="$CFLAGS -DXENCTRL"
        ;;
   no) xenctrl=false ;;
   *) AC_MSG_ERROR(bad value ${enableval} for --enable-xenctrl) ;;
  esac],[AC_CHECK_HEADER(xenctrl.h, xenctrl=true, xenctrl=false,)])
AM_CONDITIONAL(XENCTRL, test x$xenctrl = xtrue)

libvirt=false
AM_CONDITIONAL(LIBVIRT, test x$libvirt = xtrue)
# Default to libvirt for vm enumeration if no other enabled
if test "x$xenctrl" = "xfalse"; then
  libvirt=true
  PKG_CHECK_MODULES([LIBVIRT], [libvirt])
  AM_CONDITIONAL(LIBVIRT, test x$libvirt = xtrue)
fi

# Configure argument to support using xenstore
AC_ARG_WITH([xenstore],
            [  --without-xenstore   disable use of xenstore],
  [case "${withval}" in
   yes) with_xenstore=true;;
   no) with_xenstore=false;;
   *) AC_MSG_ERROR(bad value ${withval} for --with-xenstore) ;;
  esac],[AC_CHECK_HEADER(xs.h, with_xenstore=true)])
AM_CONDITIONAL(WITH_XENSTORE, test x$with_xenstore = xtrue)

AC_OUTPUT(vhostmd/Makefile
          include/Makefile
          libmetrics/Makefile
          scripts/Makefile
          docs/Makefile
          docs/man/Makefile
          vm-dump-metrics/Makefile
          test/Makefile)

echo "You may now run make"
