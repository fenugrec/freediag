#note : we can eventually replace genconfig.sh (for generating diag_config.c)
#either with a custom command and a "genconfig.c" program, or
#maybe with another configure_file() , foreach and a load of @var@s. or lists ?


### libdiag and libdyno (required for binaries)
add_library(libdiag STATIC diag_l0_me.c diag_l0_br.c diag_l0_elm.c diag_l0_sim.c
	diag_l0_dumb.c diag_config.c
	${OS_DIAGTTY} diag_l1.c diag_l2.c diag_l2_can.c diag_l2_raw.c
        diag_l2_iso9141.c diag_l2_iso9141.c diag_l2_iso14230.c
	diag_l2_saej1850.c diag_l2_vag.c diag_l2_mb1.c
	diag_l3.c diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c
	diag_os.c diag_general.c diag_dtc.c)
	
add_library(libdyno STATIC dyno.c)


### diag_test binary
if (WIN32)
	message(WARNING "Not building diag_test.exe on win32 because of alarm()")
else (WIN32)
	add_executable(diag_test diag_test.c)
	target_link_libraries(diag_test libdiag)
endif(WIN32)


### scantool binary
add_executable(scantool scantool.c scantool_cli.c scantool_debug.c scantool_set.c
	scantool_test.c scantool_diag.c scantool_vag.c scantool_dyno.c
	scantool_aif.c )
target_link_libraries(scantool libdiag libdyno)