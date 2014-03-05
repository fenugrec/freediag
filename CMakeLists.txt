###### General setup
#Note : this file is a work in progress, it's probably missing
#some checks to make it more robust.


project(freediag)
cmake_minimum_required(VERSION 2.8)
#include (CheckLibraryExists) #maybe not needed just yet ,we can use "find_library" ?
include (CheckFunctionExists)

# don't go polluting c:\program files (this has to go before project()!)
set (CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")
message("Using install prefix : ${CMAKE_INSTALL_PREFIX}")

set (PKGVERSION "1.01")
set (PKGVERSIONMAJOR "1")
set (PKGVERSIONMINOR "01")
set (SCANTOOL_PROGNAME "scantool")
#that sets the command-line tool prompt.

# define CMAKE_ENABLED // used to toggle between autotools' config.h(.in) and our cconf.h(.in)
add_definitions(-DCMAKE_ENABLED)
add_definitions ("-Wall -Wextra")
# other method of enabling warnings :
# if (CMAKE_COMPILER_IS_GNUCC) :  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra" 
# else set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /W4")
#endif

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

###### Type checks
#check_type_size ()
# XXX do we need to check sizes ?
# we need stdint.h anyway so we'll have uint8_t anyway (the most important)...

###### Global function & library checks
find_library (HAVE_LIBREADLINE libreadline)

#XXX why do we even check some of these if the code never #ifdefs them ?

check_function_exists (gettimeofday HAVE_GETTIMEOFDAY)
check_function_exists (malloc HAVE_MALLOC)
check_function_exists (strcasecmp HAVE_STRCASECMP)
check_function_exists (strerror HAVE_STRERROR)
check_function_exists (strtol HAVE_STRTOL)
set (CMAKE_REQUIRED_INCLUDES "sys/time.h")
#we could use CMakePushCheckState to push / pop CMAKE_REQUIRED_INCLUDES
#but I think it has no influence on the rest of the build...
check_function_exists (timersub HAVE_TIMERSUB)

###### Platform-specific function checks

if(WIN32)
	set (OS_DIAGTTY "diag_tty_win.c")
	if (${CMAKE_SYSTEM_VERSION} VERSION_LESS 5.1)
		##require at least winXP
		message(WARNING "At least Win XP is required ! Proceed at your risk !")
	endif (${CMAKE_SYSTEM_VERSION} VERSION_LESS 5.1)
else(WIN32)
	#no need to check for these on WIN32 as the offending code (diag_os and diag_tty) was re-written
	check_function_exists (alarm HAVE_ALARM)
	check_function_exists (select HAVE_SELECT)
	set (OS_DIAGTTY "diag_tty_unix.c")
endif(WIN32)


###### Build Options

option(BUILDGUI "Enable scangui (default=no)" OFF)
option(OLDEV "Enable deprecated dev id (default=no)" OFF)
option(USE_RCFILE "Enable use of rc file in $home/ (default=disabled)" OFF)
option(USE_INIFILE "Enable ini file in ./ (default=enabled)" ON)


###### Includes
#proj_bin_dir = needed to find config.h (not in src_dir)
include_directories("${PROJECT_BINARY_DIR}")
#src_dir/include : to find freediag_aif.h
include_directories("${PROJECT_SOURCE_DIR}/include")


###### Generate cconf.h
if (USE_RCFILE)
	set (NUSE_RCFILE 1)
	#we have to "invert" the flag for cconf.h
	#TODO : Avoid not changing the code so the opposite of the flag isn't used, i.e. "#ifdef USE_RCFILE"
endif (USE_RCFILE)

configure_file (
  "${PROJECT_SOURCE_DIR}/cconf.h.in"
  "${PROJECT_BINARY_DIR}/cconf.h"
)
#parse cconf.h.in and produce cconf.h in builddir


###### parse sub-dirs
add_subdirectory(scantool)

if (BUILDGUI)
	#add_subdirectory(scangui)
	message(FATAL_ERROR "scangui can't be built through cmake yet")
endif (BUILDGUI)


###### package generation
#this adds "package" and "package_source" targets, i.e. we can "make package_source"
set (CPACK_GENERATOR "TGZ;ZIP")
set (CPACK_SOURCE_GENERATOR "TGZ")
set (CPACK_PACKAGE_VERSION_MAJOR ${PKGVERSIONMAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${PKGVERSIONMINOR})
set (CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PKGVERSION}")
set (CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PKGVERSION}-src")
set (CPACK_SOURCE_IGNORE_FILES "xcf"
	"Makefile\\\\.in$"
	"~$"
	"^${PROJECT_SOURCE_DIR}.*/autom4te.*"
	"^${PROJECT_SOURCE_DIR}.*/\\\\.deps/"
	"^${PROJECT_SOURCE_DIR}.*/CVS/" )


set (CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)

# I think this has to stay near the end, after CPack options
include (CPack)