
#Based on the L0 and L2 options selected (see root CMakeLists.txt)
#generate the "zone" string lists for diag_config.c.in, and
#select appropriate source files

#generate zone 1 and 3 lines with L0 names
foreach (L0NAME IN LISTS L0LIST)
	if (USE_L0_${L0NAME})
		set (DIAG_CONFIG_ZONE1 "${DIAG_CONFIG_ZONE1}extern const struct diag_l0 diag_l0_${L0NAME};\n")
		set (DIAG_CONFIG_ZONE3 "${DIAG_CONFIG_ZONE3}\t&diag_l0_${L0NAME},\n")
		set (DL0_SRCS ${DL0_SRCS} "diag_l0_${L0NAME}.c")
		message(STATUS "Adding l0 driver ${L0NAME}")
	endif()
endforeach()

#generate zone 2 and 4 lines with L2 names
foreach (L2NAME IN LISTS L2LIST)
	if (USE_L2_${L2NAME})
		set (DIAG_CONFIG_ZONE2 "${DIAG_CONFIG_ZONE2}extern const struct diag_l2_proto diag_l2_proto_${L2NAME};\n")
		set (DIAG_CONFIG_ZONE4 "${DIAG_CONFIG_ZONE4}\t&diag_l2_proto_${L2NAME},\n")
		set (DL2_SRCS ${DL2_SRCS} "diag_l2_${L2NAME}.c")
		message(STATUS "Adding l2 driver ${L2NAME}")
	endif()
endforeach()

#and now generate diag_config.c ! (output in the build directory)
configure_file ( diag_config.c.in diag_config.c)


### select conditional source files

if(WIN32)
	set (OS_DIAGTTY "diag_tty_win.c")
	set (OS_DIAGOS "diag_os_win.c")
else()
	set (OS_DIAGTTY "diag_tty_unix.c")
	set (OS_DIAGOS "diag_os_unix.c")
endif()

if (NISPROG)
	message (STATUS "*****Compiling with nisprog !!!!!!*****")
endif()


### build source file lists (useful later)
set (LIBDIAG_SRCS ${DL0_SRCS} ${DL2_SRCS}
	${CMAKE_CURRENT_BINARY_DIR}/diag_config.c
	${OS_DIAGTTY} ${OS_DIAGOS}
	diag_l0.c diag_l1.c diag_l2.c diag_l3.c
	diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c
	diag_general.c diag_dtc.c diag_cfg.c)
set (LIBDYNO_SRCS dyno.c)
set (DIAGTEST_SRCS diag_test.c)
set (SCANTOOL_SRCS scantool.c scantool_cli.c scantool_diag.c scantool_set.c
	scantool_test.c scantool_debug.c scantool_vag.c scantool_dyno.c
	scantool_obd.c scantool_aif.c)
#and GLOB all the headers. This is *only* so that the headers end up in
#the file list of IDE projects (at least Code::blocks)
#but could be removed without impacting the build process
file (GLOB SCANTOOL_HEADERS *.h)



### set CURFILE
#for the various debugging fprintf()s, __FILE__ gives a super long path
#but we're really only interested in the filename; the following defines
# CURFILE=xyz for each source file.
# See http://stackoverflow.com/questions/237542/learning-the
# -source-codes-filename-at-compile-time/22161316

foreach (F IN LISTS LIBDIAG_SRCS;LIBDYNO_SRCS;
	DIAGTEST_SRCS;SCANTOOL_SRCS)
	get_filename_component (BNAME ${F} NAME)
	set_source_files_properties (${F} PROPERTIES
		COMPILE_DEFINITIONS "CURFILE=\"${BNAME}\"")
endforeach()


### Now define targets.

# libdiag and libdyno (required for binaries)

add_library(diag STATIC ${LIBDIAG_SRCS})

if (NOT WIN32)
	#link to libmath (m); not required on win32 (msvcrt provides sin() etc)
	#link to pthread (win* uses native API)
	target_link_libraries(diag m ${CMAKE_THREAD_LIBS_INIT})
endif ()

#if required, link with -lrt (see top-level CMakeLists.txt)
target_link_libraries(diag ${LIBRT})

add_library(dyno STATIC ${LIBDYNO_SRCS})


# diag_test binary

add_executable(diag_test ${DIAGTEST_SRCS})
target_link_libraries(diag_test diag)
install(TARGETS diag_test DESTINATION bin)

# scantool binary

add_executable(scantool  ${SCANTOOL_SRCS} ${SCANTOOL_HEADERS})

target_link_libraries(scantool diag dyno)
if (HAVE_LIBREADLINE)
	#this was optional
	target_link_libraries(scantool readline)
endif ()
install(TARGETS scantool DESTINATION bin)



## Test suite (WIP)
# scantool tests; these are arranged roughly from slowest to fastest
set(SCANTOOL_TESTS
#tty_patgen requires oscilloscope validation
#	tty_patgen
#tty_timing requires manual verification of output
#	tty_timing
#dumb halfdup tests are very long (15-25 sec each):
	l0_dumb_halfdup
	l0_dumb_halfdup_2
	cli_1
	l0_carsim_1
	l0_carsim_2
	l0_carsim_3
	l0_carsim_4
	l2_14230_fast
	l2_j1850p_crc
	l2_9141_reconst
	l2_14230_negresp
	l2_j1850_mrx
	l2_raw_01
	l3_j1979_9141_1
	)
set(TESTSRC "${CMAKE_SOURCE_DIR}/tests")

foreach (TF_ITER IN LISTS SCANTOOL_TESTS)
	add_test(NAME ${TF_ITER}
		WORKING_DIRECTORY ${TESTSRC}
		COMMAND ${CMAKE_COMMAND}
		-DTEST_PROG=$<TARGET_FILE:scantool>
		-DTESTFDIR=${TESTSRC}
		-DTESTF=${TF_ITER}
		-P ${TESTSRC}/runcli.cmake
		)

	message(STATUS "Adding test \"${TF_ITER}\"")
endforeach()

### install & copy targets
# defined mainly to simplify packaging

file (GLOB CARSIM_EXAMPLES *.db)
#copy carsim db files and ini file to build dir, add install target as required
file (COPY ${CARSIM_EXAMPLES} DESTINATION .)
install (FILES ${CARSIM_EXAMPLES} DESTINATION bin)

if (USE_INIFILE)
	file (COPY scantool.ini DESTINATION .)
	install (FILES scantool.ini DESTINATION bin)
endif ()
