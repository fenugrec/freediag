/*
 *	freediag - Vehicle Diagnostic Utility
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
 * L2 driver for ISO 9141 (and ISO 9141-2) interface
 *
 * NOTE: this is only the startcommunications routine, raw routines are
 * used for read/write
 *
 * NOTE: ISO9141/9141-2 do not specify any formatting of the data sent, except
 * that ISO9141-2 says if the address is 0x33 then SAEJ1979 protocol is used.
 *
 * Therefore it is the responsibility of layers above this to format the
 * whole frame, unlike in the ISO14230 L2 code which does this
 *
 */

#include <unistd.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_raw.h"

#include "diag_l2_iso9141.h" /* prototypes for this file */

CVSID("$Id$");

int
diag_l2_proto_9141_sc_common(struct diag_l2_conn *d_l2_conn, int bitrate,
target_type target, 
source_type source __attribute__((unused)), 
int kb1, int kb2)
{
	struct diag_l1_initbus_args in;
	char cbuf[MAXRBUF];
	int rv;
	struct diag_serial_settings set;

	/*
	 * If 0 has been specified, use the a suitable default
	 */
	if (bitrate == 0)
		bitrate = 10400;

	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	rv = diag_l1_setspeed(d_l2_conn->diag_link->diag_l2_dl0d, &set);

	if (rv < 0)
		return (rv);

	/* Flush unread input, then wait for idle bus. */
	(void)diag_tty_iflush(d_l2_conn->diag_link->diag_l2_dl0d);
	diag_os_millisleep(300);

	/*
	 * Do 5Baud init
	 */
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = target;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0)
		return(rv);

	/*
	 * The L1 device has read the 0x55, and may have changed the
	 * speed that we are talking to the ECU at
	 */

	/* Mode bytes are in 7O1 parity, read as 8N1 and ignore parity */
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		&cbuf[0], 1, 100);
	if (rv < 0)
		return(rv);
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		&cbuf[1], 1, 100);
	if (rv < 0)
		return(rv);

	d_l2_conn->diag_l2_kb1 = cbuf[0];
	d_l2_conn->diag_l2_kb2 = cbuf[1];

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_9141_sc_common con %p kb1 0x%x kb2 0x%x\n",
				FL, d_l2_conn, 
				cbuf[0] & 0xff, cbuf[1] & 0xff);
	/*
	 * Check the received keybytes if user asked us to check
	 */
	if (kb1 != 0 && cbuf[0] != kb1)
		return(diag_iseterr(DIAG_ERR_WRONGKB));

	if (kb2 != 0 && cbuf[1] != kb2)
			return(diag_iseterr(DIAG_ERR_WRONGKB));

	return(0);
}

static int
diag_l2_proto_9141_startcomms( struct diag_l2_conn	*d_l2_conn,
flag_type flags,
int bitrate, target_type target, source_type source)
{
	int rv;
	struct diag_serial_settings set;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_9141_startcomms conn %p\n",
				FL, d_l2_conn);
	/*
	 * If 0 has been specified, use the correct speed
	 * for 9141 protocol
	 */
	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	diag_l1_setspeed( d_l2_conn->diag_link->diag_l2_dl0d, &set);


	/* Don't do 5 baud init if monitor mode */
	if ( (flags & DIAG_L2_TYPE_INITMASK) ==  DIAG_L2_TYPE_MONINIT)
		rv = 0;
	else
		rv = diag_l2_proto_9141_sc_common( d_l2_conn, bitrate,
			target, source, 0, 0);

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_9141_startcomms returns %d\n",
				FL, rv);
	return (rv);
}


/*
 * Read data, attempts to get complete set of responses (using timeout stuff)
 */
int
diag_l2_proto_9141_int_recv(struct diag_l2_conn *d_l2_conn, int timeout)
{
	int rv;
	int tout;
	int state;
	struct diag_msg	*tmsg;

#define ST_STATE1	1	/* Start */
#define ST_STATE2	2	/* Interbyte */
#define ST_STATE3	3	/* Inter message */

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_9141_int_recv offset %x\n",
				FL, d_l2_conn->rxoffset);

	state = ST_STATE1;
	tout = timeout;

	/* Clear out last received message if not done already */
	if (d_l2_conn->diag_msg)
	{
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	/*
	 * Protocol says
	 * 	Inter-byte gap in a message < p1max
	 *	Inter-message gap < p3min
	 * We are a bit more flexible than that, see below
	 */
	while (1)
	{
		if (state == ST_STATE1)
			tout = timeout;
		else if (state == ST_STATE2)
		{
			/*
			 * Inter byte timeout within a message
			 * Spec says p1max is the maximum, but in fact
			 * we give ourselves up-to p2min minus a little bit
			 */
			tout = d_l2_conn->diag_l2_p2min - 2;
			if (tout < d_l2_conn->diag_l2_p1max)
				tout = d_l2_conn->diag_l2_p1max;
		}
		else if (state == ST_STATE3)
		{
			/* This is the timeout waiting for any more
			 * responses from the ECU. Spec says min is p2max
			 * but in fact as we are the tester, (normally),
			 * we can wait a little longer, in fact that "longer"
			 * is caused by the fact we've already waited in state2
			 * a while. Can't wait too long because if we are in
			 * monitor mode we'll get confused
			 */
			tout = d_l2_conn->diag_l2_p2max;
		}

		/* Receive data into the buffer */
#if FULL_DEBUG
		fprintf(stderr, FLFMT "before recv, state %d timeout %d, rxoffset %d\n",
			FL, state, tout, d_l2_conn->rxoffset);
#endif
		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			&d_l2_conn->rxbuf[d_l2_conn->rxoffset],
			sizeof(d_l2_conn->rxbuf) - d_l2_conn->rxoffset,
			tout);
#if FULL_DEBUG
		fprintf(stderr,
			FLFMT "after recv, rv %d rxoffset %d\n",
			FL, rv, d_l2_conn->rxoffset);
#endif
		if (rv == DIAG_ERR_TIMEOUT)
		{
			/* Timeout, end of message, or end of responses */
			switch (state)
			{
			case ST_STATE1:
				/*
				 * 1st read, if we got 0 bytes, just return
				 * the timeout error
				 */
				if (d_l2_conn->rxoffset == 0)
					break;
				/*
				 * Otherwise see if there are more bytes in
				 * this message
				 */
				state = ST_STATE2;
				continue;
			case ST_STATE2:
				/*
				 * End of that message, maybe more to come
				 * Copy data into a message
				 */
				tmsg = diag_allocmsg((size_t)d_l2_conn->rxoffset);
				tmsg->len = d_l2_conn->rxoffset;
				tmsg->fmt |= DIAG_FMT_FRAMED ;
				memcpy(tmsg->data, d_l2_conn->rxbuf,
					(size_t)d_l2_conn->rxoffset);
				(void)gettimeofday(&tmsg->rxtime, NULL);
				d_l2_conn->rxoffset = 0;
				/*
				 * ADD message to list
				 */
				diag_l2_addmsg(d_l2_conn, tmsg);

				state = ST_STATE3;
				continue;
			case ST_STATE3:
				/*
				 * No more messages, but we did get one
				 */
				rv = d_l2_conn->diag_msg->len;
				break;
			}
			if (state == ST_STATE3)
				break;
		}
		if (rv < 0)
		{
			/* Error */
			break;
		}

		/* Data received OK */
		d_l2_conn->rxoffset += rv;

		if (d_l2_conn->rxoffset && (d_l2_conn->rxbuf[0] == '\0'))
		{
			/*
			 * We get this when in
			 * monitor mode and there is
			 * a fastinit, pretend it didn't exist
			 */
			d_l2_conn->rxoffset--;
			if (d_l2_conn->rxoffset)
				memcpy(&d_l2_conn->rxbuf[0], &d_l2_conn->rxbuf[1],
					(size_t)d_l2_conn->rxoffset);
			continue;
		}
		if ( (state == ST_STATE1) || (state == ST_STATE3) )
		{
			/* Got some data in state1/3, now were in a message */
			state = ST_STATE2;
		}
	}
	return(rv);
}

int
diag_l2_proto_9141_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle)
{
	int rv;

	rv = diag_l2_proto_9141_int_recv(d_l2_conn, timeout);
	if ((rv >= 0) && d_l2_conn->diag_msg)
	{
		if (diag_l2_debug & DIAG_DEBUG_READ)
			fprintf(stderr, FLFMT "rcv callback calling %p(%p)\n", FL,
				callback, handle);
		/*
	 	 * Call user callback routine
		 */
		if (callback)
			callback(handle, d_l2_conn->diag_msg);

		/* No longer needed */
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}
	return(rv);
}

/*
 * Just send the data, with no processing etc, but insert the
 * inter-frame delay (p2).
 * Note, we copy the 1st two bytes as the source and dest address
 * into the l2_conn info, so that in ISO9141-2 code below it has the
 * addresses for doing idle timeout
 */
int
diag_l2_proto_9141_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;
	int sleeptime;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_send 0p%p 0p%p called\n",
				FL, d_l2_conn, msg);

	/*
	 * Make sure enough time between last receive and this send
	 * In fact, because of the timeout on recv(), this is pretty small, but
	 * we take the safe road and wait the whole of p3min plus whatever
	 * delay happened before
	 */
	sleeptime = d_l2_conn->diag_l2_p3min;
	if (sleeptime > 0)
		diag_os_millisleep(sleeptime);

	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		msg->data, msg->len, d_l2_conn->diag_l2_p4min);

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "about to return %d\n", FL, rv);

	return(rv);
}

struct diag_msg *
diag_l2_proto_9141_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0)
	{
		*errval = rv;
		return(NULL);
	}

	/* And wait for response */

	rv = diag_l2_proto_9141_int_recv(d_l2_conn, 1000);
	if ((rv >= 0) && d_l2_conn->diag_msg)
	{
		/* OK */
		rmsg = d_l2_conn->diag_msg;
		d_l2_conn->diag_msg = NULL;
	}
	else
	{
		/* Error */
		*errval = DIAG_ERR_TIMEOUT;
		rmsg = NULL;
	}
	return(rmsg);
}

static const struct diag_l2_proto diag_l2_proto_9141 = {
	DIAG_L2_PROT_ISO9141, 0,
	diag_l2_proto_9141_startcomms,
	diag_l2_proto_raw_stopcomms,
	diag_l2_proto_9141_send,
	diag_l2_proto_9141_recv,
	diag_l2_proto_9141_request,
	NULL
};


int diag_l2_9141_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_9141);
}

