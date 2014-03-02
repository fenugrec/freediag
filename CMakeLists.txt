project(freediag)
cmake_minimum_required(VERSION 2.8)

#TODO : build options (see configure.ac)
set (PKGVERSION "1.0.0")

# define CMAKE_ENABLED // used to toggle between autotools' config.h(.in) and our cconf.h(.in)
add_definitions(-DCMAKE_ENABLED)

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

if(WIN32)
	set (OS_DIAGTTY "diag_tty_win.c")
else(WIN32)
	set (OS_DIAGTTY "diag_tty.c")
	#TODO : rename to diag_tty_unix.c in CVS
endif(WIN32)


option(BUILDGUI "Enable scangui (default=no)" OFF)
option(OLDEV "Enable deprecated dev id (default=no)" OFF)
option(USE_RCFILE "Enable use of rc file in $home/ (default=disabled)" OFF)
option(USE_INIFILE "Enable ini file in ./ (default=disabled)" OFF)

add_subdirectory(scantool)
if (BUILDGUI)
	#add_subdirectory(scangui)
	message(FATAL_ERROR "scangui can't be built through cmake yet")
endif (BUILDGUI)


###### INCLUDES
#proj_bin_dir = requis pour config.h (not in src_dir)
include_directories("${PROJECT_BINARY_DIR}")
#src_dir/include : freediag_aif.h
include_directories("${PROJECT_SOURCE_DIR}/include")


###### cconf.h
if (USE_RCFILE)
	set (NUSE_RCFILE 1)
	#we have to "invert" the flag for cconf.h
	#TODO : Avoid not changing the code so the opposite of the flag isn't used, i.e. "#ifdef USE_RCFILE"
endif (USE_RCFILE)

configure_file (
  "${PROJECT_SOURCE_DIR}/cconf.h.in"
  "${PROJECT_BINARY_DIR}/cconf.h"
)
#should parse cconf.h.in and produce buildir/cconf.h



message("fini de parser root cmakelists.txt")