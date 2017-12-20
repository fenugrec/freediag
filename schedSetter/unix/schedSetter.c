// Copyright 2017 Neven Sajko <nsajko@gmail.com>. All Rights reserved.
// Use of this source code is governed by the Gnu Public License version 3,
// which can be found in the COPYING file.

// Runs a process as real time, but unprivileged.
// The schedSetter program sets scheduling priority of an executed process to
// realtime, then switches it to and fro on user input on a given FIFO special
// file (see below). The first is meant to give isolation between a realtime
// process and the privileged process capable of setting realtime scheduling
// policies, preventing a remote root exploit following a remote code
// execution. The second is just a convenience meant for revoking real-time
// scheduling from a non-malicious process - some engineering using seccomp-bpf
// (on Linux) would be necessary to enable us to reliably take back realtime
// scheduling from the executed process, if it is malicious.
//
// Invoke with root privilege and four arguments:
// schedSetter fifo uid gid command
// For example; if sudo gives root rights, stdinFIFO is an appropriate FIFO
// special file, "1000" are an appropriate user's UID and GID, and the target
// executable is called scantool; it could be called with:
// sudo schedSetter stdinFIFO 1000 1000 scantool
//
// See schedSetterWrapper.sh.
//
// The scheduling policy switching function of schedSetter is controlled by
// writing to the FIFO special file, any input triggers a switch; so if the
// file is in the usual line blocking mode, you should just press enter; in
// non-blocking mode any character should work.

// Building
//
// For a regular build, just run:
//
// "$CC" "$CFLAGS" -l rt -o schedSetter schedSetter.c
//
// For a debugging build, uncomment the "debugging.h" include and other commented out code
// that is preceded with a "Debugging" comment, and run:
//
// "$CC" "$CFLAGS" -l rt -o schedSetter schedSetter.c debugging.c

// TODO: would it make sense to also set the Linux ioprio stuff?

//#include "debugging.h"

// setgroups is not a POSIX function, but seems to be a de-facto standard on Unixy
// operating systems? On Linux it is required to include <grp.h> and define _BSD_SOURCE,
// Glibc also wants us to define _DEFAULT_SOURCE.
#define _BSD_SOURCE     // for setgroups on Linux
#define _DEFAULT_SOURCE // Glibc ...
#include <grp.h>        // setgroups

#include <ctype.h>  // isdigit
#include <stdio.h>  // fgetc
#include <stdlib.h> // atol
#include <string.h> // memcpy

#include <sched.h>
#include <sys/mman.h> // mlockall
#include <unistd.h>   // fork, exec

// Set given uid and gid, drop supplementary groups. If uid is zero, use getuid
// and getgid.
//
// A return value less than zero indicates an error.
static int
dropPrivileges(uid_t uid, gid_t gid) {
	/*
	if (setuid(0) < 0) {
	}
	*/

	if (uid == 0) {
		uid = getuid();
		gid = getgid();
	}
	if (setgroups(0, NULL) < 0) {
		return -1;
	}
	if (setgid(gid) < 0) {
		return -1;
	}
	if (setuid(uid) < 0) {
		return -1;
	}

	/*
	// Debugging
	if (setuid(0) < 0) {
	}
	*/

	return 0;
}

static int
f(char inputFile[], uid_t uid, gid_t gid, char targetCommand[]) {
	int oldPolicy, policy = SCHED_FIFO;
	struct sched_param oldParam, param;
	int min, max;

	int pid;

	int returnValue;

	FILE *inputF;

	// Fork and execute target process.
	pid = fork();
	if (pid < 0) {
		return 3;
	}
	if (pid == 0) {
		/*
		// Debugging
		if (printUserGroup() < 0) {
			return 30;
		}
		*/

		if (dropPrivileges(uid, gid) < 0) {
			return 31;
		}

		/*
		// Debugging
		if (printUserGroup() < 0) {
			return 32;
		}
		*/

		execlp(targetCommand, targetCommand, (char *)NULL);
		return 4;
	}

	// Get valid priorities range.
	min = sched_get_priority_min(policy);
	if (min < 0) {
		return 5;
	}
	max = sched_get_priority_max(policy);
	if (max < 0) {
		return 6;
	}

	/*
	// Debugging
	if (printRange(min, max) < 0) {
		return 7;
	}
	if (printSchedInfo(0) < 0) {
		return 8;
	}
	*/

	// Get process scheduler policy.
	oldPolicy = sched_getscheduler(0);
	if (oldPolicy < 0) {
		return 9;
	}

	// Get process sched_param.
	returnValue = sched_getparam(0, &oldParam);
	if (returnValue < 0) {
		return 10;
	}

	// Set own priority to max.
	param.sched_priority = max;
	if (sched_setscheduler(0, policy, &param) < 0) {
		return 11;
	}

	/*
	// Debugging
	if (printSchedInfo(0) < 0) {
		return 12;
	}
	*/

	// TODO: MCL_FUTURE is irrelevant here, right?
	// TODO: Move this to the beginning of the function?
	if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
		return 13;
	}

	// Try setting target process priority, go from higher to lower values.
	if (max < min) {
		// This should not happen unless there is a bug in the
		// sched_get_priority_ functions.
		return 14;
	}
	if (max == min) {
		param.sched_priority = max;
		if (sched_setscheduler(pid, policy, &param) < 0) {
			return 15;
		}
	} else {
		for (max--; min <= max; max--) {
			param.sched_priority = max;
			if (0 <= sched_setscheduler(pid, policy, &param)) {
				break;
			}
		}
		if (max < min) {
			return 15;
		}
	}

	/*
	// Debugging
	if (printSchedInfo(pid) < 0) {
		return 16;
	}
	*/

	inputF = fopen(inputFile, "r");
	if (inputF == NULL) {
		return 17;
	}

	// Switch target process scheduling settings on inputFile input.
	//
	// TODO?: with another thread we could use <sys/wait.h> to terminate
	// when the child does likewise.
	for (;;) {
		int tmpPolicy;
		struct sched_param tmpParam;

		fgetc(inputF);

		if (sched_setscheduler(pid, oldPolicy, &oldParam) < 0) {
			return 0;
		}

		tmpPolicy = oldPolicy;
		oldPolicy = policy;
		policy = tmpPolicy;

#define MEMCPY(dest, src) memcpy((dest), (src), sizeof(*(src)));
		MEMCPY(&tmpParam, &oldParam);
		MEMCPY(&oldParam, &param);
		MEMCPY(&param, &tmpParam);
	}
}

static long
checkedAtol(char s[]) {
	int i;
	for (i = 0; s[i] != '\0'; i++) {
		if (!isdigit(s[i])) {
			return -1;
		}
	}
	return atol(s);
}

int
main(int argc, char *argv[]) {
	long uid, gid;

	if (argc != 5) {
		return 1;
	}
	uid = checkedAtol(argv[2]);
	gid = checkedAtol(argv[3]);
	if (uid < 0 || gid < 0) {
		return 2;
	}

	return f(argv[1], uid, gid, argv[4]);
}
