### build source file lists (useful later)
set (SCANGUI_SRCS scangui.cxx scangui.h scanguiCallbacks.cxx scanguiInit.cxx )
set (SCANGUI_HEADERS scangui.h)

# scangui binary
add_custom_command(OUTPUT scangui.cxx scangui.h
		   COMMAND ${FLTK_FLUID_EXECUTABLE} -c ${CMAKE_CURRENT_SOURCE_DIR}/scangui.fl
                   DEPENDS scangui.fl)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(scangui ${SCANGUI_SRCS} ${SCANGUI_HEADERS})
target_link_libraries(scangui ${FLTK_LIBRARIES})
install(TARGETS scangui DESTINATION bin)