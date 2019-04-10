AC_INIT(Virtual Host Metrics Daemon, 1.1, virt-tools-list@redhat.com, vhostmd)

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

PKG_CHECK_MODULES([LIBVIRT], [libvirt])

# Configure argument to support using xenstore
AC_ARG_WITH([xenstore],
            [  --without-xenstore   disable use of xenstore],
  [case "${withval}" in
   yes) with_xenstore=true;;
   no) with_xenstore=false;;
   *) AC_MSG_ERROR(bad value ${withval} for --with-xenstore) ;;
  esac],[AC_CHECK_HEADER(xenstore.h, with_xenstore=true)])
AM_CONDITIONAL(WITH_XENSTORE, test x$with_xenstore = xtrue)

# Configure argument to support type of init system
AC_ARG_WITH([init_script],
  [AS_HELP_STRING([--with-init-script],
    [Type of init script to install: systemv, systemd, check @<:@default=check@:>@])],
  [],
  [with_init_script=check])
init_systemv=no
init_systemd=no
if test "$with_init_script" = check; then
   if type systemctl >/dev/null 2>&1; then
      init_systemd=yes
   else
      init_systemv=yes
   fi
fi
case "${with_init_script}" in
   systemv) init_systemv=yes;;
   systemd) init_systemd=yes;;
   check) ;;
   *) AC_MSG_ERROR([Unknown initscript type $with_init_script]);;
esac
AM_CONDITIONAL([INIT_SCRIPT_SYSTEMV], test "$init_systemv" = "yes")
AM_CONDITIONAL([INIT_SCRIPT_SYSTEMD], test "$init_systemd" = "yes")

AC_OUTPUT(vhostmd/Makefile
          include/Makefile
          libmetrics/Makefile
          scripts/Makefile
          docs/Makefile
          docs/man/Makefile
          vm-dump-metrics/Makefile
          test/Makefile)

echo "You may now run make"
