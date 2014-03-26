###### General setup
#Note : this file is a work in progress,
#some checks may still be missing

cmake_minimum_required(VERSION 2.8)

# don't go polluting c:\program files ; this has to go before project()
set (CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Default install prefix")
set (TEMP_INSTPREFIX ${CMAKE_INSTALL_PREFIX})
message("Using install prefix : ${CMAKE_INSTALL_PREFIX}")

project(freediag)

#check if install prefix was clobbered
if ( NOT(${CMAKE_INSTALL_PREFIX} STREQUAL ${TEMP_INSTPREFIX}))
	message(WARNING "Default install prefix has been corrupted ! please report this.")
	#note : if this happens, the alternative is something like
	#if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	# set(installprefix .etc... FORCE)
	#endif()
	#placed after project().
endif ()

#include (CheckLibraryExists) #maybe not needed just yet ,we can use "find_library" ?
include (CheckFunctionExists)
include (CheckTypeSize)

set (PKGVERSIONMAJOR "1")
set (PKGVERSIONMINOR "03")
set (PKGVERSION "${PKGVERSIONMAJOR}.${PKGVERSIONMINOR}")
set (SCANTOOL_PROGNAME "scantool")
#that sets the command-line tool prompt.

# define CMAKE_ENABLED to toggle between autotools' config.h(.in) and our cconf.h(.in)
# This can be removed if autotools is ever phased out
add_definitions (-DCMAKE_ENABLED)

###### Compiler flags & build types
if (NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, defaulting to Debug")
	set(CMAKE_BUILD_TYPE "DEBUG")
endif ()


if (CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS_DEBUG "-pedantic -std=gnu99 -Wstrict-prototypes -Wsign-compare -Wredundant-decls")
	set (CMAKE_C_FLAGS_RELEASE "-DNDEBUG")
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra" )
#else ()
	#not sure what we should use on other compilers.
	#set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
endif ()

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

###### Type checks
check_type_size (char CHARSIZE)
check_type_size (uint8_t UINT8SIZE)
check_type_size (uint16_t UINT16SIZE)
check_type_size (uint32_t UINT32SIZE)
# these should look in stdint.h (C99) so we wont verify UINT8SIZE==8 etc.
# but char should be the same as uint8; the codebase isn't quite
# ready to deal with chars != 8bits.
if (NOT(CHARSIZE EQUAL UINT8SIZE))
	message (SEND_ERROR "Warning : your platform has  CHARSIZE=${CHARSIZE}, UINT8SIZE=${UINT8SIZE} !")
endif ()

###### Global function & library checks
find_library (HAVE_LIBREADLINE libreadline)

check_function_exists (gettimeofday HAVE_GETTIMEOFDAY)
#check_function_exists (malloc HAVE_MALLOC)
#no use in checking for malloc() since we don't have a fallback solution...
check_function_exists (strcasecmp HAVE_STRCASECMP)

set (CMAKE_REQUIRED_INCLUDES "sys/time.h")
#we could use CMakePushCheckState to push / pop CMAKE_REQUIRED_INCLUDES
#but I think it has no influence on the rest of the build...
check_function_exists (timersub HAVE_TIMERSUB)

###### Platform-specific function checks

if(WIN32)
	set (OS_DIAGTTY "diag_tty_win.c")
	if (${CMAKE_SYSTEM_VERSION} VERSION_LESS 5.1)
		#require at least winXP
		message(WARNING "At least Win XP is required ! Proceed at your risk !")
	endif (${CMAKE_SYSTEM_VERSION} VERSION_LESS 5.1)
else()
	#no need to check for these on WIN32 as the offending code (diag_os and diag_tty) was re-written
	check_function_exists (alarm HAVE_ALARM)
	check_function_exists (select HAVE_SELECT)
	set (OS_DIAGTTY "diag_tty_unix.c")
endif()


###### Build Options

option(BUILDGUI "Enable scangui (default=no)" OFF)
option(OLDEV "Enable deprecated dev id (default=no)" OFF)
option(USE_RCFILE "Enable use of rc file in $home/ (default=disabled)" OFF)
option(USE_INIFILE "Enable ini file in ./ (default=enabled)" ON)
option(TRY_POSIX "Try POSIX implementations. (default=disabled)" OFF)

###### Includes
#proj_bin_dir = needed to find cconf.h (not in src_dir)
include_directories("${PROJECT_BINARY_DIR}")
#src_dir/include : to find freediag_aif.h
include_directories("${PROJECT_SOURCE_DIR}/include")


###### Generate cconf.h

configure_file (cconf.h.in cconf.h)
#parse cconf.h.in and produce cconf.h in builddir


###### parse sub-dirs
add_subdirectory(scantool)

if (BUILDGUI)
	#add_subdirectory(scangui)
	message(FATAL_ERROR "scangui can't be built through cmake yet")
endif ()

###### misc files to include in bin packages
install (DIRECTORY doc/ DESTINATION doc
	PATTERN "CVS" EXCLUDE
	PATTERN "*xcf" EXCLUDE)
install (FILES "README_v${PKGVERSIONMAJOR}_${PKGVERSIONMINOR}.txt"
		"CHANGES" "AUTHORS" "COPYING"
	DESTINATION . OPTIONAL)

###### package generation
#this adds "package" and "package_source" targets, i.e. we can "make package_source"
if (WIN32)
	set (CPACK_GENERATOR "ZIP")
else ()
	set (CPACK_GENERATOR "TGZ;ZIP")
endif ()

set (CPACK_SOURCE_GENERATOR "TGZ")
set (CPACK_PACKAGE_VERSION_MAJOR ${PKGVERSIONMAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${PKGVERSIONMINOR})
set (CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PKGVERSION}-${CMAKE_SYSTEM_NAME}")
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