
OBJFILES = scangui.o scanguiCallbacks.o scanguiInit.o
HDRS  = -I../include -I/usr/local/include \
        -I/usr/X11R6/include -I/usr/X11/include
LIBS  = -L/usr/local/lib -L/usr/X11R6/lib -L/usr/X11/lib -lfltk -lX11


all: scangui

clean:
	rm scangui *.o scangui.cxx scangui.h

install:
	echo "There is no install method for scangui yet"

scangui: ${OBJFILES}
	g++ -o scangui ${OBJFILES} ${LIBS}

scanguiInit.o : scanguiInit.cxx
	g++ -c -o scanguiInit.o ${HDRS} scanguiInit.cxx

scanguiCallbacks.o : scanguiCallbacks.cxx
	g++ -c -o scanguiCallbacks.o ${HDRS} scanguiCallbacks.cxx

scangui.o : scangui.cxx
	g++ -c -o scangui.o ${HDRS} scangui.cxx

scangui.cxx : scangui.fl
	fluid -c scangui.fl



