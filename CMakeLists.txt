###### General setup
#Note : this file is a work in progress,
#some checks may still be missing

cmake_minimum_required(VERSION 3.10)

#
# The following policies introduced in CMake 3.15 are needed to work
# around a flaw in the Visual Studio CMake integration. The used
# toolchain and platform definitions define /W3 and /MD (Release)
# respectively /MDd (Debug) by default during the compiler
# determination process.
# If the warning level or the C runtime option are changed afterwards
# these new options will not replace the already set ones but will be
# added additionally. This leads to compiler warning D9025 (overriding
# command line options) which can not be suppressed. These policies
# prevent the warning flags and runtime option set by default.
#
#  CMP0092 - warnings (https://cmake.org/cmake/help/v3.15/policy/CMP0092.html)
#  CMP0091 - runtime  (https://cmake.org/cmake/help/v3.15/policy/CMP0091.html)
#
if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif ()
if (POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW)
endif ()

project(freediag)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND WIN32)
	# don't go polluting c:\program files (default setting);
	# to use that anyway just run cmake with -DCMAKE_INSTALL_PREFIX="c:\program files" ...
	message("Forcing Win* install prefix to CMAKE_BINARY_DIR")
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Default install prefix" FORCE)
endif ()

message("Using install prefix : ${CMAKE_INSTALL_PREFIX}")

include (CheckLibraryExists)
include (CheckFunctionExists)
include (CheckSymbolExists)
include (CheckTypeSize)
include (CMakeDependentOption)
include (GNUInstallDirs)

set (PKGVERSIONMAJOR "1")
set (PKGVERSIONMINOR "09")
set (PKGVERSION "${PKGVERSIONMAJOR}.${PKGVERSIONMINOR}")
set (SCANTOOL_PROGNAME "freediag")
set (DIAG_TEST_PROGNAME "diag_test")
#that sets the command-line tool prompt.

# remove leading zeros from the version numbers to 
# avoid MinGW resource compiler (windres.exe) warnings 
# during Windows resource compile.
math (EXPR PKGVERSIONMAJOR_VALUE "${PKGVERSIONMAJOR}")
math (EXPR PKGVERSIONMINOR_VALUE "${PKGVERSIONMINOR}")


###### Compiler flags & build types
if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
	message(STATUS "No build type selected, defaulting to Debug")
	message(STATUS "Available build types : Debug, Profiling, Release, RelWithDebInfo")
	set(CMAKE_BUILD_TYPE "DEBUG" CACHE STRING
		"Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Profiling Release RelWithDebInfo MinSizeRel."
		FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Profiling" "Release"
		"MinSizeRel" "RelWithDebInfo")
endif ()


if (CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS_DEBUG "-g")
	set (CMAKE_C_FLAGS_PROFILING "-g -pg")
	set (CMAKE_C_FLAGS_RELEASE "-DNDEBUG")
	set (CMAKE_C_FLAGS_RELWITHDEBINFO "-gsplit-dwarf")
	add_compile_options(-Wall -Wextra -Wformat -pedantic -std=gnu99 -Wstrict-prototypes -Wsign-compare)
	add_compile_options(-Wredundant-decls)
elseif (CMAKE_BASE_NAME STREQUAL "bcc32c")
	#set all warnings as errors for Embarcadero clang based borland compiler.
	add_compile_options("-w!")
	#add static C runtime.
	link_libraries("cw32mt")
elseif (CMAKE_BASE_NAME STREQUAL "bcc32x")
	#set all warnings as errors for Embarcadero clang based compiler.
	add_compile_options("-Wall" "-Werror")
	#add static C runtime.
	link_libraries("cw32mt")
elseif (CMAKE_BASE_NAME STREQUAL "bcc64")
	#set all warnings as errors for Embarcadero clang based 64 bit compiler.
	add_compile_options("-Wall" "-Werror")
	#add static C runtime.
	link_libraries("cw64mt")
elseif (MSVC)
    # 1. Set the warning level to 2.
	add_compile_options(/W2 /WX)
	# 2. Clear the default content of CMAKE_MSVC_RUNTIME_LIBRARY
	set(CMAKE_MSVC_RUNTIME_LIBRARY "")
	# 3. Set the compiler options for the static multithreaded version.
	add_compile_options($<$<CONFIG:>:/MT> $<$<CONFIG:Debug>:/MTd> $<$<CONFIG:Release>:/MT>)
	# 4. Disable the auto generated manifests.
	add_link_options(/MANIFEST:NO)
	if (MSVC_VERSION LESS 1910)
		#require at least winXP
		message(WARNING "Visual Studio version before 15 (2017) may not work ! Proceed at your risk !")
	endif ()
else ()
	message(WARNING "non-gcc compiler, verify compiler options !")
	#not sure what we should use on other compilers.
	#set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
endif ()

if (MINGW)
	#by default mingw links against a non-C99 compliant printf.
	add_definitions("-D__USE_MINGW_ANSI_STDIO=1")
endif ()

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

###### Type checks
check_type_size (uint8_t UINT8SIZE)
check_type_size (uint16_t UINT16SIZE)
check_type_size (uint32_t UINT32SIZE)
# these should look in stdint.h (C99) so we wont verify UINT8SIZE==8 etc.

###### Global function & library checks
find_library (HAVE_LIBREADLINE readline)

check_function_exists (strcasecmp HAVE_STRCASECMP)

###### Platform-specific function checks
if(WIN32)
	if (${CMAKE_SYSTEM_VERSION} VERSION_LESS 5.1)
		#require at least winXP
		message(WARNING "At least Win XP is required ! Proceed at your risk !")
	endif ()
	set (PLATF "-win32")	#for package name string generation only.
	#check for old Microsoft compliant kbhit() and getch()
	#exports in the C runtime.
	check_symbol_exists (_kbhit "conio.h" HAVE_STD_KBHIT)
	if (NOT HAVE_STD_KBHIT)
	    check_symbol_exists (kbhit "conio.h" HAVE_MS_KBHIT)
        if (NOT HAVE_MS_KBHIT)
            message(FATAL_ERROR "neither _kbhit nor kbhit function present.")
		endif ()
	endif ()
	check_symbol_exists (_getch "conio.h" HAVE_STD_GETCH)
	if (NOT HAVE_STD_GETCH)
	    check_symbol_exists (getch "conio.h" HAVE_MS_GETCH)
        if (NOT HAVE_MS_GETCH)
            message(FATAL_ERROR "neither _getch nor getch function present.")
		endif ()
	endif ()
else()
	check_function_exists (alarm HAVE_ALARM)
	check_function_exists (select HAVE_SELECT)
	check_function_exists (gettimeofday HAVE_GETTIMEOFDAY)
	find_package (Threads REQUIRED)

	#diag_os_unix needs some _POSIX_TIMERS functions wich
	#may require linking with -lrt
	set (CMAKE_REQUIRED_INCLUDE_FILES time.h)
	check_function_exists (timer_create HAVE_TIMER_CREATE)
	if (HAVE_TIMER_CREATE)
		# all set, nothing to do. So glibc is recent enough
		message (STATUS "Using glibc timer_create().")
	else ()
		#else, we try finding clock_gettime in librt
		check_library_exists (rt timer_create time.h HAVE_LIBRT)
		message (STATUS "Using timer_create() from librt.")
		if (HAVE_LIBRT)
			#set extra linker flag
			set (LIBRT rt)
		else ()
			message(FATAL_ERROR "librt not found !")
		endif ()
	endif ()
endif()


###### Build Options

option(BUILD_GUI "Enable scangui (default=no)" OFF)
option(BUILD_DIAGTEST "Build additional \"diag_test\" test suite (default=no)" OFF)

if(WIN32)
	#nothing special
else()
	option(BUILD_SCHEDSET "Build optional \"schedSetter\" helper to set realtime priority (default=no)" OFF)
	CMAKE_DEPENDENT_OPTION(BUILD_SCHEDSET_DEBUG "Compile extra debugging code for \"schedSetter\"" OFF BUILD_SCHEDSET OFF)
endif()

option(USE_RCFILE "At startup, search $home/ for an rc file to load initial commands. (default=disabled)" OFF)
option(USE_INIFILE "At startup, search the current directory for an ini file to load initial commands. (default=enabled)" ON)


###### L0/L2 driver selection

if(DEFINED L0LIST)
		message("Using provided list of L0 : ${L0LIST}")
else()
		set(L0LIST "dumb" "br" "elm" "me" "sim" "dumbtest")
endif()

if(DEFINED L2LIST)
		message("Using provided list of L2 : ${L2LIST}")
else()
		set(L2LIST "iso9141" "can" "iso14230" "mb1" "raw" "saej1850" "vag" "d2")
endif()

# Append special L2 driver if required
if(BUILD_DIAGTEST)
	set(L2LIST ${L2LIST} test)
endif()

foreach (L0NAME IN LISTS L0LIST)
	option(USE_L0_${L0NAME} "Enable \"${L0NAME}\" L0 driver (default=enabled)" ON)
endforeach()

foreach (L2NAME IN LISTS L2LIST)
	option(USE_L2_${L2NAME} "Enable \"${L2NAME}\" L2 driver (default=enabled)" ON)
endforeach()

###### Includes
#proj_bin_dir = needed to find cconf.h (not in src_dir)
include_directories("${PROJECT_BINARY_DIR}")
#src_dir/include : to find freediag_aif.h
include_directories("${PROJECT_SOURCE_DIR}/include")


###### Generate cconf.h

configure_file (cconf.h.in cconf.h)
#parse cconf.h.in and produce cconf.h in builddir


### Test suite (WIP)
enable_testing()


###### install locations
if (WIN32)
	set (DOC_DESTDIR .
		CACHE PATH "Destination directory for documentation")
	set (BIN_DESTDIR .
		CACHE PATH "Destination directory for executables")
	set (EXAMPLES_DESTDIR .
		CACHE PATH "Destination directory for example .db and .ini files")
else ()
	set (DOC_DESTDIR ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
		CACHE PATH "Destination directory for documentation")
	set (BIN_DESTDIR ${CMAKE_INSTALL_BINDIR}
		CACHE PATH "Destination directory for executables")
	set (EXAMPLES_DESTDIR ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/examples
		CACHE PATH "Destination directory for example .db and .ini files")
endif ()

###### parse sub-dirs
add_subdirectory(scantool)
add_subdirectory(tests)

if (BUILD_GUI)
	find_package(FLTK)
	if (FLTK_FOUND)
		add_subdirectory(scangui)
	else ()
		message(WARNING "Cannot build scangui without FLTK !")
	endif()
endif ()

if (BUILD_SCHEDSET)
	add_subdirectory(schedSetter)
endif ()

###### files to install

install (DIRECTORY doc/ DESTINATION ${DOC_DESTDIR}/doc
	PATTERN "*xcf" EXCLUDE)
install (FILES "README" "CHANGES" "AUTHORS" "COPYING"
	DESTINATION ${DOC_DESTDIR})
install (FILES "README_v${PKGVERSIONMAJOR}_${PKGVERSIONMINOR}.txt"
		DESTINATION ${DOC_DESTDIR} OPTIONAL)

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
set (CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PKGVERSION}${PLATF}")
set (CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PKGVERSION}-src")
set (CPACK_SOURCE_IGNORE_FILES "xcf"
	"~$"
	"\\\\.git"
	)


set (CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)

# I think this has to stay near the end, after CPack options
include (CPack)
