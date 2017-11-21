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

/** ret 1 if success */
static bool run_tests(void) {
	bool rv = 1;

	if (!test_dupmsg()) {
		rv = 0;
		printf("test_dupmsg failed\n");
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
