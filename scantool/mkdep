#!/bin/sh

# mkdep program, outputs to .depend

if [ $# = 0 ] ; then
	echo 'usage: mkdep [cc_flags] file ...'
	exit 1
fi

cc -M "$@" > .depend

if [ $? != 0 ]; then
	echo 'mkdep: compile failed.'
	rm -f .depend
	exit 1
fi

exit 0
