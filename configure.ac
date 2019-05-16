#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([virt_cacard], [1], [pl.palant@redhat.com])
AC_CONFIG_SRCDIR([src/connection.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES([CACARD], [libcacard])
AC_SUBST(CACARD_CFLAGS)
AC_SUBST(CACARD_LIBS)


PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.22])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h unistd.h ],
                 [],
                 [AC_MSG_ERROR([unable to find all required headers])
])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([memset socket])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
