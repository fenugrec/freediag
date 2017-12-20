// Copyright 2017 Neven Sajko <nsajko@gmail.com>. All Rights reserved.
// Use of this source code is governed by the Gnu Public License version 3,
// which can be found in the COPYING file.

// Debugging code.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h> // malloc
#include <string.h>

#include <sched.h>
#include <unistd.h>

enum { errCode = -1,
};

// Print e?[gu]id and supplementary groups.
extern int
printUserGroup(void) {
	int ret, i;
	gid_t *groups;

	printf("uid, gid: %4d %4d, %4d %4d\n", getuid(), geteuid(), getgid(),
	       getegid());

	ret = getgroups(0, 0);
	groups = malloc(ret * sizeof(*groups));
	if (groups == NULL) {
		return -1;
	}
	getgroups(ret, groups);
	printf("supplementary groups: ");
	for (i = 0; i < ret; i++) {
		printf("%ld ", groups[i]);
	}
	printf("\n");
	free(groups);

	return 0;
}

extern int
printRange(int min, int max) {
	int ret = printf("Scheduling policy valid priority ranges: %d-%d\n",
			 min, max);
	return ret;
}

extern int
printSchedInfo(int pid) {
	struct sched_param p;
	int ret = sched_getparam(pid, &p);
	if (ret == errCode) {
		printf("sched_getparam: %s\n", strerror(errno));
		return errCode;
	}
	printf("priority = %d\n", p.sched_priority);

	ret = sched_getscheduler(pid);
	if (ret == errCode) {
		printf("sched_getscheduler: %s\n", strerror(errno));
		return errCode;
	}
	printf("Scheduling policy: %d\n", ret);

	return 0;
}
