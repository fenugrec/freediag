# Process this file with autoconf to produce a configure script.
AC_INIT(freediag, 1.0.0, freediag-discuss@lists.sourceforge.net)
AC_CONFIG_HEADERS(config.h)
AC_CONFIG_SRCDIR(diag.h)

AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB(ncurses,tparm,TERMLIB=-lncurses,
  AC_CHECK_LIB(termcap,tgetent,TERMLIB=-ltermcap))
saved_LIBS="$LIBS"
LIBS="$LIBS $TERMLIB"
AC_CONFIG_FILES(Makefile)
AC_OUTPUT

