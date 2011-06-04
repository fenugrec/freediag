#Makefile.am
#*_SOURCES manually filled (info from orig. makefile,
#.h files from "gcc -MM *.c"
#this is probably why the orig. makefile had a ".depend" target...

AM_CPPFLAGS = -I../include
bin_PROGRAMS=scantool diag_test
scantool_SOURCES=scantool.c scantool_cli.c scantool_debug.c scantool_set.c \
	scantool_test.c scantool_diag.c scantool_vag.c scantool_dyno.c \
	scantool_aif.c \
	scantool.h scantool_aif.h scantool_cli.h \
	diag.h diag_os.h diag_dtc.h diag_l1.h diag_l2.h diag_l3.h \
	diag_err.h diag_tty.h dyno.h diag_vag.h
scantool_LDADD=libdiag.a libdyno.a

diag_test_SOURCES=diag_test.c diag.h diag_os.h diag_err.h diag_iso14230.h \
	diag_tty.h diag_l1.h diag_l2.h
diag_test_LDADD=libdiag.a

noinst_LIBRARIES=libdiag.a libdyno.a

#libdiag.a.: diag_config.c
libdiag_a_SOURCES=diag_config.c \
	diag_l0_se.c diag_l0_me.c diag_l0_vw.c diag_l0_br.c diag_l0_elm.c diag_l0_sim.c\
	diag_l0_dumb.c\
	diag_tty.c \
	diag_l1.c \
	diag_l2.c diag_l2_can.c diag_l2_raw.c \
        diag_l2_iso9141.c diag_l2_iso9141.c diag_l2_iso14230.c \
	diag_l2_saej1850.c diag_l2_vag.c diag_l2_mb1.c \
	diag_l3.c diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c \
	diag_os.c diag_general.c diag_dtc.c \
	diag.h diag_os.h diag_dtc.h diag_err.h diag_l1.h diag_l2.h \
	diag_iso14230.h diag_tty.h diag_l2_can.h diag_l2_iso14230.h \
	diag_l2_raw.h diag_l2_mb1.h diag_l2_saej1850.h diag_vag.h diag_l2_vag.h \
	diag_l3_saej1979.h diag_mb1.h
nodist_libdiag_a_SOURCES=diag_config.c

#do not distribute diag_config.c as it will be built (see below)
BUILT_SOURCES=diag_config.c
DISTCLEANFILES=diag_config.c
diag_config.c: l0config l2config genconfig.sh
	./genconfig.sh > diag_config.c

libdyno_a_SOURCES=dyno.c diag.h diag_os.h diag_err.h dyno.h