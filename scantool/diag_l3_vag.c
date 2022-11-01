/*
 * !!! INCOMPLETE !!!!
 *
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * Copyright (C) 2004 Steve Meisner <meisner@users.sourceforge.net>
 * Copyright (C) 2014-2015 fenugrec <fenugrec@users.sourceforge.net>
 * Copyright (C) 2015 - 2016 Tomasz Kaźmierczak <tomek-k@users.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *************************************************************************
 *
 * Diag
 *
 * L3 driver for Volkswagen Aktiengesellschaft (VAG) protocol (on ISO9141 interface
 * with 5 baud init using specific keywords)
 *
 *
 * XXX NOT YET WRITTEN
 */

#include <stdint.h>
#include <stdio.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "diag_vag.h"
#include "diag_l3_vag.h"

/*
 * Insert the L3 layer on top of the layer 2 connection
 *
 */
static int
diag_l3_vag_start(struct diag_l3_conn *d_l3_conn) {
	struct diag_l2_data l2data;
	struct diag_l2_conn *d_l2_conn;

	/* 5 baud init has been done, make sure we got the correct keybytes */
	d_l2_conn = d_l3_conn->d_l3l2_conn;

	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_GET_L2_DATA, (void *)&l2data);

	DIAG_DBGM(diag_l3_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "start L3 KB 0x%X 0x%X need 0x01 0x8A\n",
		FL, l2data.kb1, l2data.kb2);

	if (l2data.kb1 != 0x01) {
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}
	if (l2data.kb2 != 0x8A) {
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}

	/* OK, ISO 9141 keybytes are correct ! */

	/* Get the initial stuff the ECU tells us */

	return 0;
}


/*
 * This is called without just the VW protocol data
 */

void
diag_l3_vag_decode(UNUSED(struct diag_l3_conn *d_l3_conn),
struct diag_msg *msg, char *buf, size_t bufsize) {
	char buf2[128];
	char buf3[16];
	const char *s;

	fprintf(stderr, FLFMT "Obviously broken code !\n", FL);

	switch (msg->type) {
	case DIAG_VAG_CMD_DTC_CLEAR:
		s = "Clear DTCs";
		break;
	case DIAG_VAG_CMD_END_COMMS:
		s = "End Comms";
		break;
	case DIAG_VAG_CMD_DTC_RQST:
		s = "Request DTCs";
		break;
	case DIAG_VAG_CMD_READ_DATA:
		s = "Read Data (single)";
		break;
	case DIAG_VAG_RSP_ASCII:
		s = "ASCII Data";
		break;
	case DIAG_VAG_RSP_HEX:
		s = "Hex Data";
		break;
	default:
		snprintf(buf3, sizeof(buf3), "0x%X", msg->type);
		s = buf3;
		break;
	}
	snprintf(buf2, sizeof(buf2), "Command: %s: ", s);
	smartcat(buf, bufsize, buf2);

	snprintf(buf2, sizeof(buf2), "Data : ");
	smartcat(buf, bufsize, buf2);

	for (int i=3; i < msg->data[0]; i++) {
		snprintf(buf2, sizeof(buf2), "0x%X ", msg->data[i]);
		smartcat(buf, bufsize, buf2);
	}
	smartcat(buf, bufsize, "\n");

	return;
}

const struct diag_l3_proto diag_l3_vag = {
	"VAG",
	diag_l3_vag_start,
	diag_l3_base_stop,
	diag_l3_base_send,
	diag_l3_base_recv,
	NULL,	//ioctl
	diag_l3_base_request,
	diag_l3_vag_decode,
	NULL	//timer
};
