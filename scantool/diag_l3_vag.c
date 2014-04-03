/*
 * !!! INCOMPLETE !!!!
 *
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
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
 * Diag
 *
 * L3 driver for Volkswagen Audi Group (VAG) protocol (on ISO9141 interface
 * with 5 baud init using specific keywords)
 *
 *
 * XXX NOT YET WRITTEN, is it the same thing as KWP1281 ?
 */

#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "diag_vag.h"
#include "diag_l3_vag.h"


CVSID("$Id$");

/*
 * Insert the L3 layer on top of the layer 2 connection
 *
 */
static int
diag_l3_vag_start(struct diag_l3_conn *d_l3_conn)
{
	struct diag_l2_data l2data;
	struct diag_l2_conn *d_l2_conn;

	/* 5 baud init has been done, make sure we got the correct keybytes */
	d_l2_conn = d_l3_conn->d_l3l2_conn;

	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_GET_L2_DATA, (void *)&l2data);

	if (diag_l2_debug & DIAG_DEBUG_INIT)
		fprintf(stderr, FLFMT "start L2 KB 0x%X 0x%X need 0x01 0x8A\n",
			FL, l2data.kb1, l2data.kb2);

	if (l2data.kb1 != 0x01)
		return diag_iseterr(DIAG_ERR_WRONGKB);
	if (l2data.kb2 != 0x8A)
		return diag_iseterr(DIAG_ERR_WRONGKB);

	/* OK, ISO 9141 keybytes are correct ! */

	/* Get the initial stuff the ECU tells us */

	return 0;
}


/*
 * This is called without just the VW protocol data
 */

static char *
diag_l3_vag_decode(UNUSED(struct diag_l3_conn *d_l3_conn),
struct diag_msg *msg, char *buf, size_t bufsize)
{
	char buf2[128];
	char buf3[16];
	const char *s;
	int i;

	/* XXX What is supposed to go here for arguments?
	 * The "sprintf(" that follows had no arguments for the format, I added the "0, 0".
	 */

	fprintf(stderr, FLFMT "Obviously broken code !\n", FL);

	snprintf(buf, bufsize, "Block Len 0x%X, Counter 0x%X ", 0, 0);

	switch (msg->data[2])
	{
	case 0x05:
		s = "Clear DTCs";
		break;
	case 0x06:
		s = "End Comms";
		break;
	case 0x07:
		s = "Request DTCs";
		break;
	case 0x08:
		s = "Read Data (single)";
		break;
	case 0x09:
		s = "Ack";
		break;
	case 0xF6:
		s = "ASCII Data";
		break;
	case 0xFC:
		s = "Hex Data";
		break;
	default:
		/* XXX The argument Was "buf[2]" and not msg->data[2], which must be wrong. */
		snprintf(buf3, sizeof(buf3), "0x%X", msg->data[2]);
		s = buf3;
		break;
	}
	snprintf(buf2, sizeof(buf2), "Command: %s: ", s);
	smartcat(buf, bufsize, buf2);

	snprintf(buf2, sizeof(buf2), "Data : ");
	smartcat(buf, bufsize, buf2);

	for (i=3; i < msg->data[0]; i++)
	{
		snprintf(buf2, sizeof(buf2), "0x%X ", msg->data[i]);
		smartcat(buf, bufsize, buf2);
	}
	smartcat(buf, bufsize, "\n");

	return buf;
}

const diag_l3_proto_t diag_l3_vag = {
	"VAG", diag_l3_vag_start, diag_l3_base_stop,
	diag_l3_base_send, diag_l3_base_recv, NULL, diag_l3_base_request,
	diag_l3_vag_decode, NULL
};
