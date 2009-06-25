bin_PROGRAMS = scangui$(EXEEXT)
scangui_SOURCES = scangui.cxx scangui.h scanguiCallbacks.cxx scanguiInit.cxx

scangui_CPPFLAGS = -Wall \
 -I$(top_srcdir)/include $(FLTK_CXXFLAGS)

scangui_LDFLAGS = $(FLTK_LDFLAGS)
 
BUILT_SOURCES=scangui.h scangui.cxx
CLEANFILES=$(BUILT_SOURCES)

.fl.cxx:
	fluid -c $<

.fl.h:
	fluid -c $<