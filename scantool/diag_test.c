/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017 fenugrec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * Diag library test harness
 * This is a stand-alone program !
 * Designed to exercise code paths not easy to test through the .ini-based testsuite.
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_l0.h"
#include "diag_l1.h"
#include "diag_l2.h"

struct test_item {
	const char *name;
	bool (*testfunc)(void);
};

bool test_dupmsg(void);
bool test_periodic(void);

static struct test_item test_list[] = {
	{"msg duplication", test_dupmsg},
	{"periodic timers", test_periodic}
};

bool test_dupmsg(void) {
	struct diag_msg *msg0, *msg1, *msg2;
	struct diag_msg *newchain;

	msg0 = diag_allocmsg(1);
	msg1 = diag_allocmsg(1);
	msg2 = diag_allocmsg(1);

	if (!msg0 || !msg1 || !msg2) {
		printf("alloc err\n");
		return 0;
	}

	msg0->next = msg1;
	msg1->next = msg2;
	msg1->rxtime = 1;
	msg2->rxtime = 2;

	newchain = diag_dupmsg(msg0);

	if ((newchain->rxtime != 0) ||
		(newchain->next->rxtime != 1) ||
		(newchain->next->next->rxtime != 2)) {
			printf("chain data / order mismatch\n");
			return 0;
	}
	diag_freemsg(msg0);
	diag_freemsg(newchain);
	return 1;
}

/********** construct a dummy L0 driver */
int d0_init(void) {
	return 0;
}
int d0_new(struct diag_l0_device *dl0d) {
	(void) dl0d;
	return 0;
}
struct cfgi *d0_getcfg(struct diag_l0_device *dl0d) {
	(void) dl0d;
	return NULL;
}
void d0_del(struct diag_l0_device *dl0d) {
	(void) dl0d;
}
int d0_open(struct diag_l0_device *dl0d, int l1_proto) {
	(void) dl0d;
	(void) l1_proto;
	return 0;
}
void d0_close(struct diag_l0_device *dl0d) {
	(void) dl0d;
}
uint32_t	d0_getflags(struct diag_l0_device *dl0d) {
	(void) dl0d;
	return 0;
}
int	d0_recv(struct diag_l0_device *dl0d,
	const char *subinterface, void *data, size_t len, unsigned int timeout) {
	(void) dl0d;
	(void) subinterface;
	(void) data;
	(void) len;
	(void) timeout;
	return 0;
}
int	d0_send(struct diag_l0_device *dl0d,
	const char *subinterface, const void *data, size_t len) {
	(void) dl0d;
	(void) subinterface;
	(void) data;
	(void) len;
	return 0;
}
int d0_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	(void) dl0d;
	(void) cmd;
	(void) data;
	return 0;
}

static struct diag_l0 dummy_dl0 = {
	.longname = "dummy L0",
	.shortname = "dummy L0",
	.l1proto_mask = -1,	//support everything
	.init = d0_init,
	._new = d0_new,
	._getcfg = d0_getcfg,
	._del = d0_del,
	._open = d0_open,
	._close = d0_close,
	._getflags = d0_getflags,
	._recv = d0_recv,
	._send = d0_send,
	._ioctl = d0_ioctl
};

#define TEST_PERIODIC_DURATION	800	//in ms
/** periodic callback test
 * Start an L2, let the periodic timer run a few times, then stop
 */
bool test_periodic(void) {
	struct diag_l0_device dl0d = {
		.dl0 = &dummy_dl0
	};
	struct diag_l2_conn *dl2c;
	unsigned long ts;

	if (diag_l2_open(&dl0d, DIAG_L1_RAW)) {
		printf("dl2open err\n");
		return 0;
	}

	ts = diag_os_getms() + TEST_PERIODIC_DURATION;	//anticipated endtime

	dl2c = diag_l2_StartCommunications(&dl0d, DIAG_L2_PROT_TEST, 0, 0, 0, 0);
	if (dl2c == NULL) {
		printf("startcomm err\n");
		diag_l2_close(&dl0d);
		return 0;
	}
	dl2c->tinterval = 0;	//force timer expiry on every timer callback
	while (diag_os_getms() < ts) {}

	diag_l2_StopCommunications(dl2c);
	diag_l2_close(&dl0d);
	return 1;
}

/** ret 1 if success */
static bool run_tests(void) {
	bool rv = 1;
	unsigned i;

	for (i=0; i < ARRAY_SIZE(test_list); i++) {
		printf("Testing %s:\t", test_list[i].name);
		if (!test_list[i].testfunc()) {
			rv = 0;
			printf("failed\n");
		} else {
			printf("ok\n");
		}
	}

	return rv;
}


int
main(int argc,  char **argv) {
	bool rv;
	(void) argc;
	(void) argv;

	if (diag_init()) {
		printf("error in initialization\n");
		return 0;
	}

	rv = run_tests();

	(void) diag_end();

	if (!rv) return -1;
	return 0;
}
