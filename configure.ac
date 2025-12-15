dnl $Id: configure.in,v 1.5 2002/07/14 00:00:25 npsimons Exp $
dnl Copyright (C) 2002, Nathan Paul Simons (npsimons@hardcorehackers.com)
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
dnl
dnl Alternatively, the GPL can be found at
dnl http://www.gnu.org/copyleft/gpl.html

dnl AC_INIT
AC_INIT([twclone],[0.0.2])
AC_CONFIG_HEADERS([autoconf.h])
AM_INIT_AUTOMAKE

AC_SUBST(VERSION)

ISODATE=`date +%Y-%m-%d`
AC_SUBST(ISODATE)

AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_INSTALL
AC_PROG_CC

dnl Checks for libraries.
AC_CHECK_LIB(m, pow, , AC_MSG_ERROR("I need a math library."))
AC_CHECK_LIB(pthread, pthread_create, , AC_MSG_ERROR("I need a pthread library."))

dnl Checks for header files.
AC_CHECK_HEADERS(ctype.h errno.h math.h netdb.h netinet/in.h pthread.h stdio.h stdlib.h string.h sys/ipc.h sys/msg.h sys/poll.h sys/socket.h sys/time.h sys/types.h termios.h time.h unistd.h, , AC_MSG_ERROR("Missing essential header!"))

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Checks for library functions.


AC_CONFIG_FILES([Makefile bin/Makefile])
AC_OUTPUT
