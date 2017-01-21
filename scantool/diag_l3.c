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
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "utlist.h"

int diag_l3_debug;

static struct diag_l3_conn	*diag_l3_list;


struct diag_l3_conn *
diag_l3_start(const char *protocol, struct diag_l2_conn *d_l2_conn)
{
	struct diag_l3_conn *d_l3_conn = NULL;
	unsigned int i;
	int rv;
	const struct diag_l3_proto *dp;

	assert(d_l2_conn != NULL);

	if (diag_l3_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,FLFMT "start protocol %s l2 %p\n",
			FL, protocol, (void *)d_l2_conn);


	/* Find the protocol */
	dp = NULL;
	for (i=0; diag_l3_protocols[i]; i++) {
		if (strcasecmp(protocol, diag_l3_protocols[i]->proto_name) == 0)
		{
			dp = diag_l3_protocols[i];	/* Found. */
			break;
		}
	}

	if (dp) {
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
		if (rv < 0) {
			free(d_l3_conn);
			return diag_pseterr(rv);
		}
		/*
		 * Set time to now
		 */
		d_l3_conn->timer=diag_os_getms();

		/*
		 * And add to list
		 */
		LL_PREPEND(diag_l3_list, d_l3_conn);

	}

	if (diag_l3_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,FLFMT "start returns %p\n",
			FL, (void *)d_l3_conn);

	return d_l3_conn;
}


int diag_l3_stop(struct diag_l3_conn *d_l3_conn)
{
	int rv;

	assert(d_l3_conn != NULL);

	const struct diag_l3_proto *dp = d_l3_conn->d_l3_proto;

	/* Remove from list */
	LL_DELETE(diag_l3_list, d_l3_conn);

	rv = dp->diag_l3_proto_stop(d_l3_conn);

	free(d_l3_conn);

	return rv? diag_iseterr(rv):0;
}

int diag_l3_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	const struct diag_l3_proto *dp = d_l3_conn->d_l3_proto;

	rv = dp->diag_l3_proto_send(d_l3_conn, msg);

	if (!rv)
		d_l3_conn->timer=diag_os_getms();

	return rv? diag_iseterr(rv):0;
}

int diag_l3_recv(struct diag_l3_conn *d_l3_conn, unsigned int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle)
{
	const struct diag_l3_proto *dp = d_l3_conn->d_l3_proto;
	int rv;

	rv=dp->diag_l3_proto_recv(d_l3_conn, timeout,
		rcv_call_back, handle);

	if (rv==0)
		d_l3_conn->timer=diag_os_getms();

	if (rv == DIAG_ERR_TIMEOUT) return rv;
	return rv? diag_iseterr(rv):0;
}


void diag_l3_decode(struct diag_l3_conn *d_l3_conn,
	struct diag_msg *msg, char *buf, const size_t bufsize)
{
	const struct diag_l3_proto *dp = d_l3_conn->d_l3_proto;

	dp->diag_l3_proto_decode(d_l3_conn, msg, buf, bufsize);
	return;
}


int diag_l3_ioctl(struct diag_l3_conn *d_l3_conn, unsigned int cmd, void *data)
{
	const struct diag_l3_proto *dp = d_l3_conn->d_l3_proto;

	/* Call the L3 ioctl routine if applicable */
	if (dp->diag_l3_proto_ioctl)
		return dp->diag_l3_proto_ioctl(d_l3_conn, cmd, data);

	/* Otherwise L2 ioctl */
	return diag_l2_ioctl(d_l3_conn->d_l3l2_conn, cmd, data);
}


struct diag_msg *
diag_l3_request(struct diag_l3_conn *dl3c, struct diag_msg *txmsg, int *errval)
{
	struct diag_msg *rxmsg;
	const struct diag_l3_proto * dl3p = dl3c->d_l3_proto;

	if (diag_l3_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "_request dl3c=%p msg=%p called\n",
				FL, (void *)dl3c, (void *)txmsg);

	/* Call protocol specific send routine */
	if (dl3p->diag_l3_proto_request)
		rxmsg = dl3p->diag_l3_proto_request(dl3c, txmsg, errval);
	else
		rxmsg = NULL;

	if (diag_l2_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "_request returns %p, err %d\n",
				FL, (void *)rxmsg, *errval);
	}

	if (rxmsg==NULL) {
		return diag_pseterr(*errval);
	}
		//update timers
	dl3c->timer = diag_os_getms();

	return rxmsg;
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
	unsigned long now=diag_os_getms();

	LL_FOREACH(diag_l3_list, conn) {
		/* Call L3 timer routine for this connection */
		const struct diag_l3_proto *dp = conn->d_l3_proto;

		//skip connection if L1 does the keepalive stuff
		if (conn->d_l3l1_flags & DIAG_L1_DOESKEEPALIVE)
			continue;

		if (dp->diag_l3_proto_timer) {
			unsigned long diffms;

			diffms = now - conn->timer;

			(void) dp->diag_l3_proto_timer(conn, diffms);
		}
	}
}


/* Base implementations for some functions */


int diag_l3_base_start(UNUSED(struct diag_l3_conn *d_l3_conn))
{
	return 0;
}


int diag_l3_base_stop(UNUSED(struct diag_l3_conn *d_l3_conn))
{
	return 0;
}

int diag_l3_base_send(struct diag_l3_conn *d_l3_conn,
	UNUSED(struct diag_msg *msg))
{
	d_l3_conn->timer=diag_os_getms();
	return 0;
}


int
diag_l3_base_recv(struct diag_l3_conn *d_l3_conn,
	UNUSED(unsigned int timeout),
	UNUSED(void (* rcv_call_back)(void *handle ,struct diag_msg *)),
	UNUSED(void *handle))
{
	d_l3_conn->timer=diag_os_getms();
	return 0;
}

//this implementation is rather naive and untested. It simply forwards the
//txmsg straight to the L2 request function and returns the response msg
//as-is.
struct diag_msg * diag_l3_base_request(struct diag_l3_conn *dl3c,
	struct diag_msg* txmsg, int* errval) {

	struct diag_msg *rxmsg = NULL;

	*errval=0;

	rxmsg = diag_l2_request(dl3c->d_l3l2_conn, txmsg, errval);

	if (rxmsg == NULL) {
		return diag_pseterr(*errval);
	}
	dl3c->timer=diag_os_getms();

	return rxmsg;
}
