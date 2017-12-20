#! /bin/sh
set -u

# Terminal emulator
term=st
#term=9term

main=schedSetter

uid=1000
gid=1000

program=scantool
#program=dc

i="`basename "$main"`-`basename "$program"`StdInFIFO"

######################################################################

cd /tmp
mkfifo "$i" 2>/dev/null
test -p "$i" || {
	printf '%s\n' "$0: $i is not a FIFO." 1>&2
	exit 13
}

"$term" sh -c "cat >$i" &

sudo "$main" "$i" "$uid" "$gid" "$program"
