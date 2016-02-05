#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(timelapse-deflicker, 1.0, sandeep.pn@outlook.com)
AM_INIT_AUTOMAKE(timelapse-deflicker, 1.0)
AC_OUTPUT(Makefile src/Makefile)
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdint.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([mkdir])

AC_OUTPUT
