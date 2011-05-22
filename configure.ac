#Process this with autoconf for magic.

AC_PREREQ(2.62)		#But would probably work with earlier versions
AC_INIT(freediag, 1.0.0, freediag-discuss@lists.sourceforge.net,,freediag.sourceforge.net)
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

# Checks for libraries.

AC_CHECK_LIB(m,sin)
AC_CHECK_LIB(ncurses,tparm,,AC_CHECK_LIB(termcap,tgetent))


# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h sys/ioctl.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for functions.
AC_FUNC_MALLOC

AC_CHECK_FUNCS([alarm gettimeofday memset select strcasecmp strerror strrchr strtol])

AC_ARG_ENABLE(scangui,
	[AS_HELP_STRING([--enable-scangui],[Enable scangui (default=no)])],
	[buildgui=yes],[buildgui=no]
	)
	
AC_ARG_ENABLE(rcfile,
	[AS_HELP_STRING([--enable-rcfile],[Enable use of rc file in $home/ (default=disabled)])],
	,AC_DEFINE(DONT_USE_RCFILE,,[Disable loading of .rc file])
	)
	
AC_ARG_ENABLE(inifile,
	[AS_HELP_STRING([--enable-inifile],[Enable ini file in ./ (default=disabled)])],
	AC_DEFINE(USE_INIFILE,,[Enable loading of .ini file]),
	)
	
AS_IF([test "x$buildgui" = xyes],
		[
#		AC_PROG_CXX		#invoking this conditionally will not work (...)
		AC_FUNC_FORK
		AC_CHECK_FUNCS([dup2])
		AC_CHECK_PROG(has_fltk,fluid,yes,no)
		#assume the presence of fluid (fltk compiler) implies a complete fltk package,
		#including headers and libraries.
		if test "$has_fltk" != "yes"; then
			AC_MSG_ERROR([Error: fluid compiler not found. Verify fltk installation])
		fi
		AC_PATH_X	#check for X, assume lib X11 will be present (....)
		FLTK_CXXFLAGS="`fltk-config --cxxflags`"
		FLTK_LDFLAGS="`fltk-config --ldflags`"
		AC_SUBST(FLTK_CXXFLAGS)
		AC_SUBST(FLTK_LDFLAGS)
		AC_MSG_NOTICE([scangui will be built])],
		[AC_MSG_NOTICE([scangui will not be built])]
	)


AM_CONDITIONAL([BUILDSCANGUI], [test "x$buildgui" = xyes])

AC_CONFIG_FILES([Makefile scantool/Makefile scangui/Makefile])

AC_OUTPUT
