#Not applicable on win*
if (WIN32)
	message(STATUS "Not compiling schedSetter on Windows")
	return()
endif()

set (SCHEDSET_SRC unix/schedSetter.c)

if (BUILD_SCHEDSET_DEBUG)
	add_definitions(-DSCHEDSET_DEBUG)
	set (SCHEDSET_SRC ${SCHEDSET_SRC} unix/debugging.c)
endif()

add_executable(schedSetter ${SCHEDSET_SRC})
