/*
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
 * L2 driver for "raw" interface (just sends and receives data without
 * modifying it)
 *
 */


#include <string.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_err.h"
#include "diag_tty.h"

#include "diag_l2_raw.h" /* prototypes for this file */




int
diag_l2_proto_raw_startcomms( struct diag_l2_conn *d_l2_conn,
UNUSED(flag_type flags),
unsigned int bitrate,
target_type target,
source_type source)
{
	int rv;
	struct diag_serial_settings set;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	rv=diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, &set);

	if (!rv)
		return diag_iseterr(DIAG_ERR_GENERAL);

	//set tgt and src address in d_l2_conn
	d_l2_conn->diag_l2_destaddr=target;
	d_l2_conn->diag_l2_srcaddr=source;

	return 0;
}

/*
*/

int
diag_l2_proto_raw_stopcomms(UNUSED(struct diag_l2_conn* pX))
{
	return 0;
}

/*
 * Just send the data, with no processing etc
 * ret 0 if ok
 */
int
diag_l2_proto_raw_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_send %p, msg %p len %d called\n",
				FL, (void *)d_l2_conn, (void *)msg, msg->len);

	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		msg->data, msg->len, d_l2_conn->diag_l2_p4min);

	return rv? diag_iseterr(rv):0 ;
}

/*
*/
int
diag_l2_proto_raw_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle)
{
	uint8_t rxbuf[MAXRBUF];
	struct diag_msg msg={0};	//local message structure that will disappear when we return
	int rv;

	/*
 	 * Read data from fd
	 */
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		rxbuf, sizeof(rxbuf), timeout);

	if (rv <= 0 || rv > 255)		/* Failure, or 0 bytes (which cant happen) */
		return rv;

	msg.len = (uint8_t) rv;
	msg.data = rxbuf;
	/* This is raw, unframed data; we don't set .fmt */
	msg.next = NULL;
	msg.idata=NULL;
	msg.rxtime = diag_os_chronoms(0);

	if (diag_l2_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr, FLFMT "l2_proto_raw_recv: handle=%p\n", FL,
			(void *)handle);	//%pcallback! we won't try to printf the callback pointer.
	}

	/*
	 * Call user callback routine
	 */
	if (callback)
		callback(handle, &msg);


	return 0;
}

/*
*/
struct diag_msg *
diag_l2_proto_raw_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;
	uint8_t rxbuf[MAXRBUF];

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0)
	{
		*errval = rv;
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/* And wait for response */
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d,
		0, rxbuf, sizeof(rxbuf), 1000);

	if (rv <= 0 || rv>255)
	{
		*errval = rv;
	}
	else
	{
		/*
		 * Ok, alloc a message
		 */
		rmsg = diag_allocmsg((size_t)rv);
		if (rmsg == NULL)
			return diag_pseterr(DIAG_ERR_NOMEM);
		memcpy(&rmsg->data, rxbuf, (size_t)rv);	/* Data */
		rmsg->fmt = 0;
		rmsg->rxtime = diag_os_chronoms(0);
	}
	return rmsg;
}

static const struct diag_l2_proto diag_l2_proto_raw = {
	DIAG_L2_PROT_RAW, 0,
	diag_l2_proto_raw_startcomms,
	diag_l2_proto_raw_stopcomms,
	diag_l2_proto_raw_send,
	diag_l2_proto_raw_recv,
	diag_l2_proto_raw_request,
	NULL
};

int diag_l2_raw_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_raw);
}
