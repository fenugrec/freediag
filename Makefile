
all:
	(cd scantool ; make)
	(cd scangui  ; make)

clean:
	(cd scantool ; make clean)
	(cd scangui  ; make clean)

install:
	(cd scantool ; make install)
	(cd scangui  ; make install)

