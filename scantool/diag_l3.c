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
 * L3 code, interface to diagnostic protocols such as SAEJ1979 (ODB II), VAG,
 * etc
 *
 *
 *
 * Timers. As most L3 protocols run idle timers, the hard work is done here,
 *	The timer code calls the L3 timer for each L3 connection with the
 *	time difference between "now" and the timer in the L3 connection
 *	structure, so L3 can quickly check to see if it needs to do a retry
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l3.h"
#include "diag_l3_saej1979.h"
#include "diag_l3_vag.h"
#include "diag_l3_iso14230.h"


CVSID("$Id$");

int diag_l3_debug;

static const diag_l3_proto_t * const diag_l3_protocols[] =
{
	&diag_l3_j1979,
	&diag_l3_vag,
	&diag_l3_iso14230,
};

static struct diag_l3_conn	*diag_l3_list;


/*
 * Protocol start (connect a protocol on top of a L2 connection)
 * make sure to diag_l3_stop afterwards to free() the diag_l3_conn !
 * This adds the new l3 connection to the diag_l3_list linked-list
 */
struct diag_l3_conn *
diag_l3_start(const char *protocol, struct diag_l2_conn *d_l2_conn)
{
	struct diag_l3_conn *d_l3_conn = NULL;
	unsigned int i;
	int rv;
	const diag_l3_proto_t *dp;

	if (diag_l3_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,FLFMT "start protocol %s l2 %p\n",
			FL, protocol, (void *)d_l2_conn);


	/* Find the protocol */
	dp = NULL;
	for (i=0; i < ARRAY_SIZE(diag_l3_protocols); i++)
	{
		if (strcasecmp(protocol, diag_l3_protocols[i]->proto_name) == 0)
		{
			dp = diag_l3_protocols[i];	/* Found. */
			break;
		}
	}

	if (dp)
	{
		if (diag_l3_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,FLFMT "start protocol %s found\n",
				FL, dp->proto_name);
		/*
		 * Malloc us a L3
		 */
		if (diag_calloc(&d_l3_conn, 1))
			return diag_pseterr(DIAG_ERR_NOMEM);

		d_l3_conn->d_l3l2_conn = d_l2_conn;
		d_l3_conn->d_l3_proto = dp;

		/* Get L2 flags */
		(void)diag_l2_ioctl(d_l2_conn,
			DIAG_IOCTL_GET_L2_FLAGS,
			&d_l3_conn->d_l3l2_flags);

		/* Get L1 flags */
		(void)diag_l2_ioctl(d_l2_conn,
			DIAG_IOCTL_GET_L1_FLAGS,
			&d_l3_conn->d_l3l1_flags);

		/* Call the proto routine */
		rv = dp->diag_l3_proto_start(d_l3_conn);
		if (rv < 0)
		{
			free(d_l3_conn);
			d_l3_conn = NULL;
			return diag_pseterr(rv);
		}
		else
		{
			/*
			 * Set time to now
			 */
			(void)gettimeofday(&d_l3_conn->timer, NULL);
			/*
			 * And add to list
			 */
			d_l3_conn->next = diag_l3_list;
			diag_l3_list = d_l3_conn;
		}
	}

	if (diag_l3_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,FLFMT "start returns %p\n",
			FL, (void *)d_l3_conn);

	return d_l3_conn;
}

/*
 * Calls the appropriate protocol stop routine,
 * free() d_l3_conn, and remove from diag_l3_list
 */
int diag_l3_stop(struct diag_l3_conn *d_l3_conn)
{
	struct diag_l3_conn *dl, *dlast;
	int rv;

	const diag_l3_proto_t *dp = d_l3_conn->d_l3_proto;

	/* Remove from list */
	if (d_l3_conn == diag_l3_list)
	{
		/* 1st in list : make list point to the actual 2nd*/
		diag_l3_list = d_l3_conn->next;
	} else {
		for ( dl = diag_l3_list->next, dlast = diag_l3_list;
				dl ; dl = dl->next )
		{
			if (dl == d_l3_conn)
			{
				dlast->next = dl->next;
				break;
			}
			dlast = dl;
		}
	}

	rv = dp->diag_l3_proto_stop(d_l3_conn);

	free(d_l3_conn);
	if (rv)
		return diag_iseterr(rv);

	return 0;
}

int diag_l3_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	const diag_l3_proto_t *dp = d_l3_conn->d_l3_proto;

	(void)gettimeofday(&d_l3_conn->timer, NULL);
	rv = dp->diag_l3_proto_send(d_l3_conn, msg);

	return rv;
}

int diag_l3_recv(struct diag_l3_conn *d_l3_conn, int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle)
{
	const diag_l3_proto_t *dp = d_l3_conn->d_l3_proto;

	return dp->diag_l3_proto_recv(d_l3_conn, timeout,
		rcv_call_back, handle);
}

//TODO : check if *buf should be uint8_t instead ?
char *diag_l3_decode(struct diag_l3_conn *d_l3_conn,
	struct diag_msg *msg, char *buf, const size_t bufsize)
{
	const diag_l3_proto_t *dp = d_l3_conn->d_l3_proto;

	return dp->diag_l3_proto_decode(d_l3_conn, msg, buf, bufsize);
}


// diag_l3_ioctl : call the diag_l3_proto_ioctl AND diag_l2_ioctl !?
// But why L2 ?
int diag_l3_ioctl(struct diag_l3_conn *d_l3_conn, int cmd, void *data)
{
	int rv = 0;
	const diag_l3_proto_t *dp = d_l3_conn->d_l3_proto;

	/* Call the L3 ioctl routine */
	if (dp->diag_l3_proto_ioctl)
		rv = dp->diag_l3_proto_ioctl(d_l3_conn, cmd, data);

	if (rv < 0)
		return rv;

	/* And now the L2 ioctl routine, which will call the L1 one etc */
	rv = diag_l2_ioctl(d_l3_conn->d_l3l2_conn, cmd, data);

	return rv;
}


/*
 * Note: This is called regularly from a signal handler.
 * (see diag_os.c)
 * XXX This calls non async-signal-safe functions!
 */

void diag_l3_timer(void)
{
	/*
	 * Regular timer routine
	 * Call protocol specific timer
	 */
	struct diag_l3_conn *conn;
	struct timeval now;

	(void)gettimeofday(&now, NULL);	/* XXX NOT ASYNC SIGNAL SAFE */

	for (conn = diag_l3_list ; conn ; conn = conn->next )
	{
		/* Call L3 timer routine for this connection */
		const diag_l3_proto_t *dp = conn->d_l3_proto;

		if (dp->diag_l3_proto_timer) {
			struct timeval diff;
			int ms;

			timersub(&now, &conn->timer, &diff);
			ms = diff.tv_sec * 1000 + diff.tv_usec / 1000;

			(void)dp->diag_l3_proto_timer(conn, ms);
		}
	}
}


int diag_l3_base_start(UNUSED(struct diag_l3_conn *d_l3_conn))
{
	return 0;
}


int diag_l3_base_stop(UNUSED(struct diag_l3_conn *d_l3_conn))
{
	return 0;
}

/*
 * Send a Message doing all the handshaking needed
 */

int diag_l3_base_send(UNUSED(struct diag_l3_conn *d_l3_conn),
	UNUSED(struct diag_msg *msg))
{
	return 0;
}

/*
 * Receive a Message frame (building it as we get small amounts of data)
 *
 * - timeout expiry will cause return before complete packet
 *
 * Successful packet receive will call the callback routine with the message
 */

int
diag_l3_base_recv(UNUSED(struct diag_l3_conn *d_l3_conn),
	UNUSED(int timeout),
	UNUSED(void (* rcv_call_back)(void *handle ,struct diag_msg *)),
	UNUSED(void *handle))
{
	return 0;
}
