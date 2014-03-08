#note : we can eventually replace genconfig.sh (for generating diag_config.c)
#either with a custom command and a "genconfig.c" program, or
#maybe with another configure_file() , foreach and a load of @var@s. or lists ?

### step 1 : make source file lists (useful later)
set (LIBDIAG_SRCS diag_l0_me.c diag_l0_br.c diag_l0_elm.c diag_l0_sim.c
	diag_l0_dumb.c diag_config.c
	${OS_DIAGTTY} diag_l1.c diag_l2.c diag_l2_can.c diag_l2_raw.c
		diag_l2_iso9141.c diag_l2_iso9141.c diag_l2_iso14230.c
	diag_l2_saej1850.c diag_l2_vag.c diag_l2_mb1.c
	diag_l3.c diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c
	diag_os.c diag_general.c diag_dtc.c )
set (LIBDYNO_SRCS dyno.c)
set (DIAGTEST_SRCS diag_test.c)
set (SCANTOOL_SRCS scantool.c scantool_cli.c scantool_debug.c scantool_set.c
	scantool_test.c scantool_diag.c scantool_vag.c scantool_dyno.c
	scantool_aif.c)
#and GLOB all the headers. This is *only* so that the headers end up in
#the file list of IDE projects (at least Code::blocks)
#but could be removed without impacting the build process
file (GLOB SCANTOOL_HEADERS *.h)



### step 2 : set CURFILE
#for the various debugging fprintf()s, __FILE__ gives a super long path
#but we're really only interested in the filename; the following should
# " #define CURFILE [xyz]" for each source file.
# See http://stackoverflow.com/questions/237542/learning-the
# -source-codes-filename-at-compile-time/22161316

foreach (F IN LISTS LIBDIAG_SRCS;LIBDYNO_SRCS;
	LIBDYNO_SRCS;SCANTOOL_SRCS)
	get_filename_component (BNAME ${F} NAME)
	set_source_files_properties (${F} PROPERTIES
		COMPILE_DEFINITIONS "CURFILE=\"${BNAME}\"")
endforeach()


### Now define targets.

### libdiag and libdyno (required for binaries)

add_library(libdiag STATIC ${LIBDIAG_SRCS})
add_library(libdyno STATIC ${LIBDYNO_SRCS})
# these


### diag_test binary

if (WIN32)
	message(WARNING "Not building diag_test.exe on win32 because of alarm()")
else (WIN32)
	add_executable(diag_test ${DIAGTEST_SRCS})
	target_link_libraries(diag_test libdiag)
	install(TARGETS diag_test DESTINATION bin)
endif(WIN32)


### scantool binary

add_executable(scantool  ${SCANTOOL_SRCS} ${SCANTOOL_HEADERS})

target_link_libraries(scantool libdiag libdyno)
install(TARGETS scantool DESTINATION bin)

### install targets
# defined mainly to simplify packaging
