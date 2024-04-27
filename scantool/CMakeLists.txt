## TOCHECK in docs : why not add_x($varname ...) ? (from IRC discussion)
## 

add_library(diag STATIC)
add_library(cli STATIC)
add_library(dyno STATIC)
add_library(freediagcli STATIC)


# diag_test binary
add_executable(${DIAG_TEST_PROGNAME})
target_link_libraries(diag_test diag)

# freediag binary
add_executable(${SCANTOOL_PROGNAME}  ${SCANTOOL_SRCS} ${SCANTOOL_HEADERS})


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

# WIP : unset so that a parent scope that submodule's freediag doesn't keep appending stuff to
# the cache variable when re-generating
#unset(CMAKE_C_CPPCHECK CACHE)

option(FREEDIAG_USE_CPPCHECK "Run cppcheck on freediag sources " Off)

if (FREEDIAG_USE_CPPCHECK)
   find_program (FREEDIAG_CPPCHECK NAMES cppcheck)
   # ugly
   if (NOT (FREEDIAG_CPPCHECK MATCHES "FREEDIAG_CPPCHECK-NOTFOUND"))
	   list (
		APPEND FREEDIAG_CPPCHECK
			"--enable=warning"
			"--enable=style"
			"--enable=performance"
			"--enable=information"
			"--suppress=constParameter"
			"--suppress=missingInclude"
			"--suppress=missingIncludeSystem"
			"--suppress=unmatchedSuppression"
			"--suppress=variableScope"
			"--inline-suppr"
			"-I${CMAKE_BINARY_DIR}"
			"-D__cppcheck__"
	   )
	   if (WIN32)
		  list (
			 APPEND FREEDIAG_CPPCHECK
			 "--platform=win32A"
			 "-D_WIN32"
			 #
			 # This is to prevent cppcheck to include windows.h.
			 # The header leads to problems with the borland compilers.
			 #
			 "-D_WINDOWS_"
		  )
	   else ()
		  list (
			 APPEND FREEDIAG_CPPCHECK
			 "--platform=unix32"
			 "-D__unix__"
		  )
	   endif ()
	  ## enable cppcheck on all targets
	  set_property(TARGET
		  diag cli dyno freediagcli ${SCANTOOL_PROGNAME} ${DIAG_TEST_PROGNAME}
		  PROPERTY C_CPPCHECK ${FREEDIAG_CPPCHECK}
		  )
   endif()
endif ()

#and now generate diag_config.c ! (output in the build directory)
configure_file ( diag_config.c.in diag_config.c)


### select conditional source files

if(WIN32)
	set (OS_DIAGTTY "diag_tty_win.c")
	set (OS_DIAGOS "diag_os_win.c")
	set (FREEDIAG_RC "freediag.rc")
	set (DIAG_TEST_RC "diag_test.rc")
	enable_language (RC)
else()
	set (OS_DIAGTTY "diag_tty_unix.c")
	set (OS_DIAGOS "diag_os_unix.c")
	set (FREEDIAG_RC "")
	set (DIAG_TEST_RC "")
endif()


### build source file lists (useful later)
set (LIBDIAG_SRCS ${DL0_SRCS} ${DL2_SRCS}
	${CMAKE_CURRENT_BINARY_DIR}/diag_config.c
	${OS_DIAGTTY} ${OS_DIAGOS}
	diag_l0.c diag_l1.c diag_l2.c diag_l3.c
	diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c
	diag_l7_d2.c diag_l7_kwp71.c
	diag_general.c diag_dtc.c diag_cfg.c)
set (LIBDYNO_SRCS dyno.c)
set (DIAGTEST_SRCS diag_test.c ${DIAG_TEST_RC})
set (LIBCLI_SRCS libcli.c)
set (CLI_SRCS scantool_cli.c scantool_diag.c scantool_set.c
	scantool_debug.c)
set (SCANTOOL_SRCS scantool.c
	scantool_test.c scantool_vag.c scantool_850.c scantool_dyno.c
	scantool_850/dtc.c scantool_850/ecu.c
	scantool_850/frobbed.c scantool_850/xiaotec.c
	scantool_obd.c ${FREEDIAG_RC})


### set target source files
target_sources(diag PRIVATE ${LIBDIAG_SRCS})
target_sources(dyno PRIVATE ${LIBDYNO_SRCS})
target_sources(cli PRIVATE ${LIBCLI_SRCS})
target_sources(freediagcli PRIVATE ${CLI_SRCS})
target_sources(freediag PRIVATE ${SCANTOOL_SRCS})
target_sources(diag_test PRIVATE ${DIAGTEST_SRCS})


### set CURFILE
#for the various debugging fprintf()s, __FILE__ gives a super long path
#but we're really only interested in the filename; the following defines
# CURFILE=xyz for each source file.
# See http://stackoverflow.com/questions/237542/learning-the
# -source-codes-filename-at-compile-time/22161316

foreach (F IN LISTS LIBDIAG_SRCS;LIBDYNO_SRCS;
	DIAGTEST_SRCS;LIBCLI_SRCS;CLI_SRCS;SCANTOOL_SRCS)
	get_filename_component (BNAME ${F} NAME)
	set_source_files_properties (${F} PROPERTIES
		COMPILE_DEFINITIONS "CURFILE=\"${BNAME}\"")
endforeach()



if (NOT WIN32)
	#link to libmath (m); not required on win32 (msvcrt provides sin() etc)
	#link to pthread (win* uses native API)
	target_link_libraries(diag m ${CMAKE_THREAD_LIBS_INIT})
else()
	# WIN needs winmm for timeBeginPeriod()
	target_link_libraries(diag winmm)
endif ()

#if required, link with -lrt (see top-level CMakeLists.txt)
target_link_libraries(diag ${LIBRT})



target_link_libraries(freediagcli diag cli)
if (HAVE_LIBREADLINE)
	#this was optional
	target_link_libraries(freediagcli readline)
endif ()

target_link_libraries(freediag dyno freediagcli)


install(TARGETS diag_test DESTINATION ${BIN_DESTDIR})
install(TARGETS freediag DESTINATION ${BIN_DESTDIR})


### misc install & copy targets

#install carsim .db files and sample .ini file
file (GLOB CARSIM_EXAMPLES *.db)
install (FILES ${CARSIM_EXAMPLES} DESTINATION ${EXAMPLES_DESTDIR})

if (USE_INIFILE)
	install (FILES freediag.ini DESTINATION ${EXAMPLES_DESTDIR})
endif ()
