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
 * L2 diagnostic interface, generic routines
 *
 * This sits "under" the L2 per-protocol (such as ISO 14230, SAE J1979)
 *  - understands the protocol format,
 *  - removes the "half duplex" echos from half duplex interfaces
 *  - pads messages as needed,
 *  - and sends "tester present" messages at the correct intervals to keep
 *	the link to an ECU alive
 *
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_tty.h"
#include "diag_err.h"

#include "diag_l2.h"

/* */

CVSID("$Id$");

int diag_l2_debug;

/*
 * List of supported L2 protcols, added with "diag_l2_add_protocol".
 */

struct diag_l2_node {
	const struct diag_l2_proto *l2proto;
	struct diag_l2_node *next;
} *l2proto_list;

int
diag_l2_add_protocol(const struct diag_l2_proto *l2proto) {
	int rv;

	struct diag_l2_node *last_node, *new_node;

	if (l2proto_list == NULL) {
		if ( (rv = diag_calloc(&l2proto_list, 1)) )
			return rv;

		l2proto_list->l2proto = l2proto;
		return 0;
	}

	for (last_node = l2proto_list; last_node != NULL ; last_node = last_node->next)
		if (last_node->l2proto == l2proto)
			return diag_iseterr(DIAG_ERR_GENERAL);	/* Already there. */

	if ( (rv = diag_calloc(&new_node, 1)) )
		return rv;

	/* Find the last non-NULL node...*/

	for (last_node = l2proto_list; last_node->next != NULL ;
				last_node = last_node->next)
	/* Do Nothing */ ;

	new_node->l2proto = l2proto;
	last_node->next = new_node;

	return 0;
}

/*
 * Values for diag_state
 *
 * The state values are mainly used by the timer code to determine if
 * keepalive timers are needed.
 */
#define DIAG_L2_STATE_CLOSED		0	/* Not in use (but not free for anyones use !!) */
#define DIAG_L2_STATE_SENTCONREQ	1	/* Sent connection request (waiting for response/reject) */
#define DIAG_L2_STATE_OPEN		2	/* Up and running */
#define DIAG_L2_STATE_CLOSING		3	/* Sent close request (possibly), waiting for response/timeout */


/*
 * The list of connections, not searched often so no need to hash
 * (maybe when we actually finish supporting talking to more than
 * one ECU on one interface at once)
 */
#define CONBYIDSIZE 256

static struct diag_l2_conn	*diag_l2_connections;
static struct diag_l2_conn	*diag_l2_conbyid[CONBYIDSIZE];	/* Look up by ECU address */

static int diag_l2_init_done;	/* Init done */

/*
 * The list of L1 devices we have open, not used often so no need to hash
 */
static struct diag_l2_link *diag_l2_links;

/*
 * Find our link to the L1 device, by name
 */
static struct diag_l2_link *
diag_l2_findlink(const char *dev_name)
{
	struct diag_l2_link *dl2l = diag_l2_links;

	while (dl2l)
	{
		if ( strcmp(dl2l -> diag_l2_name , dev_name) == 0)
			return dl2l;
		dl2l = dl2l -> next;
	}
	return NULL;
}

/*
 * Remove a link, caller should free it
 */
static int
diag_l2_rmlink(struct diag_l2_link *d)
{
	struct diag_l2_link *dl2l, *d_l2_last;

	dl2l = diag_l2_links;
	d_l2_last = NULL;

	while (dl2l)
	{
		if (dl2l == d)
		{
			if (d_l2_last)
				d_l2_last = d->next;
			else
				diag_l2_links = d->next;
			break;
		}
		d_l2_last = dl2l;
		dl2l = dl2l -> next;
	}
	return 0;
}

/*
 *
 * remove a L2 connection from our list
 * - up to the caller to have shut it down properly first
 * diag_l2_rmconn XXX Currently not used?
 */

static int diag_l2_rmconn(UNUSED(struct diag_l2_conn *d))
{
	struct diag_l2_conn	*d_l2_conn = diag_l2_connections;
	struct diag_l2_conn	*d_l2_last_conn = NULL;

	while (d_l2_conn)
	{
		if (d_l2_conn == d)
		{
			/* Remove it from list */
			if (d_l2_last_conn)
				d_l2_last_conn->next = d->next ;
			else
				diag_l2_connections = d->next;

			break;
		}
		d_l2_last_conn = d_l2_conn;
		d_l2_conn = d_l2_conn->next;
	}
	return 0;
}

/*
 * Called regularly to check timeouts etc (call at least once per
 * second)
 * Note: This is called from a signal handler.
 *
 * XXX Uses functions not async-signal-safe.
 * Note that this does nothing now except handle the timeouts.
 * Thus, it is now easy to eliminate "diag_l2_timer" and replace it
 * with posix timers.
 */
void
diag_l2_timer(void)
{
	struct diag_l2_conn	*d_l2_conn;
	struct timeval now;

	(void)gettimeofday(&now, NULL);	/* XXX Not async safe */

	for (d_l2_conn = diag_l2_connections;
		d_l2_conn; d_l2_conn = d_l2_conn -> next)
	{
		int expired = 0;

		/*
		 * If in monitor mode, we don't do anything as we're
		 * just listening
		 */
		if ((d_l2_conn->diag_l2_type & DIAG_L2_TYPE_INITMASK)
			== DIAG_L2_TYPE_MONINIT)
		{
			continue;
		}

		/* Check the send timers vs the p3max timer */
		expired = timercmp(&now, &d_l2_conn->diag_l2_expiry, >);

		/* If expired, call the timeout routine */
		if (expired && d_l2_conn->l2proto->diag_l2_proto_timeout)
			d_l2_conn->l2proto->diag_l2_proto_timeout(d_l2_conn);
	}
}

/*
 * Add a message to the message list on the L2 connection
 */
void
diag_l2_addmsg(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	struct diag_msg *tmsg = d_l2_conn->diag_msg;

	if (d_l2_conn->diag_msg == NULL)
	{
		d_l2_conn->diag_msg = msg;
		d_l2_conn->diag_msg->mcnt = 1;
		return;
	}
	/* Add to end of list */
	while (tmsg)
	{
		if (tmsg->next == NULL)
		{
			msg->next = NULL;
			tmsg->next = msg;
			d_l2_conn->diag_msg->mcnt ++;
			break;
		}
		tmsg = tmsg->next;
	}
}

/************************************************************************/
/*  PUBLIC Interface starts here					*/
/************************************************************************/

/*
 * Init called to initialise local structures, and same for layers below
 */
int diag_l2_init()
{
	if (diag_l2_debug & DIAG_DEBUG_INIT)
		fprintf(stderr,FLFMT "diag_l2_init called\n", FL);

	if ( diag_l2_init_done )
		return 0;
	diag_l2_init_done = 1;

	memset(diag_l2_conbyid, 0, CONBYIDSIZE);

	/*
	 * And go do the layer 1 init
	 */
	return diag_l1_init();
}

//diag_l2_end : opposite of diag_l2_init !
int diag_l2_end() {
	diag_l2_init_done=0;
	return diag_l1_end();
}

/*
 * Close/kill a L1link
 */
static int
diag_l2_closelink(struct diag_l2_link **pdl2l)
{
	if (pdl2l && *pdl2l) {
		struct diag_l2_link *dl2l = *pdl2l;

		if (diag_l2_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr,FLFMT "diag_l2_closelink %p called\n",
				FL, dl2l);

		if (dl2l)
		{
			/* Clear out this link */
			diag_l2_rmlink(dl2l);	/* Take off list */
			diag_l1_close(&dl2l->diag_l2_dl0d);
			free(dl2l);
		}

		*pdl2l = 0;
	}


	return 0;
}

/*
 * Open a link to a Layer 1 device, returns dl0d, if device is already
 * open, close and then re-open it (as we need to pass it a new "protocol"
 * field if the l1 protocol is different
 *
 * The subinterface indicates the device to use
 *
 * We need to tell L1 what protocol we are going to use, because some L1
 * interfaces are smart and can support multiple protocols, also L2 needs
 * to know later (and asks L1)
 */
struct diag_l0_device *
diag_l2_open(const char *dev_name, const char *subinterface, int L1protocol)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l2_link *dl2l;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_open %s subinterface %s L1proto %d called\n",
			FL, dev_name, subinterface, L1protocol);

	dl2l = diag_l2_findlink(dev_name);

	if (dl2l)
	{
		/* device open */
		if (dl2l->diag_l2_l1protocol != L1protocol)
		{
			/* Wrong L1 protocol, close link */
			diag_l2_closelink(&dl2l);
		}
		else
		{
			/* Device was already open, with correct protocol  */
		return dl2l->diag_l2_dl0d;
		}
	}


	/* Else, create the link */
	if ((rv=diag_calloc(&dl2l, 1)))
		return (struct diag_l0_device *)diag_pseterr(rv);

	dl0d = diag_l1_open(dev_name, subinterface, L1protocol);
	if (dl0d == 0)	//pointer to 0 => failure
	{
		rv=diag_geterr();
		free(dl2l);
		return (struct diag_l0_device *)diag_pseterr(rv);	//forward error to next level
	}

	diag_l0_set_dl2_link(dl0d, dl2l);	/* Associate ourselves with this */

	dl2l->diag_l2_dl0d = dl0d;
	dl2l->diag_l2_l1flags = diag_l1_getflags(dl0d);
	dl2l->diag_l2_l1type = diag_l1_gettype(dl0d);
	dl2l->diag_l2_l1protocol = L1protocol;

	strcpy(dl2l->diag_l2_name, dev_name);

	/*
	 * Put ourselves at the head of the list.
	 */
	dl2l->next = diag_l2_links;
	diag_l2_links = dl2l;

	return dl2l->diag_l2_dl0d;
}

/*
 * Close a L2 interface, the caller
 * must have closed all the connections relating to this or they will
 * just get left hanging using resources. We dont kill the L1, it may
 * be useful later
 *
 * XXX, this needs some consideration, one dl0d can be used for more than
 * one L2, so closing by dl0d isnt appropriate, and so this routine does
 * nothing. However, all we have is the link unless a StartCommunications()
 * has been done
 */
int
diag_l2_close(struct diag_l0_device *dl0d)
{

	if (diag_l2_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr,FLFMT "Entering diag_l2_close for dl0d=%p\n",
			FL, dl0d);

	/* XXX */

	return 0;
}

/*
 * startCommunication routine, establishes a connection to
 * an ECU by sending fast/slow start (or whatever the protocol is),
 * and sets all the timer parameters etc etc
 */
struct diag_l2_conn *
diag_l2_StartCommunications(struct diag_l0_device *dl0d, int L2protocol, uint32_t type,
	int bitrate, target_type target, source_type source)
{
	struct diag_l2_conn	*d_l2_conn;
	struct diag_l2_node *node;

	struct diag_l2_link *dl2l;
	int rv;
	int reusing = 0;
	flag_type flags;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_startCommunications dl0d %p L2proto %d type %x baud %d target 0x%x src 0x%x called\n",
			FL, dl0d, L2protocol, type ,
			bitrate, target&0xff, source&0xff);

	/*
	 * Check connection doesn't exist already, if it does, then use it
	 * but reinitialise ECU - when checking connection, we look at the
	 * target and the dl0d
	 */
	d_l2_conn = diag_l2_conbyid[target];
	while (d_l2_conn)
	{
		/*
		 * Find if there's an entry for this link (or may be talking
		 * to ECU with this ID on another channel !!
		 */
		if (d_l2_conn->diag_link->diag_l2_dl0d == dl0d)
		{
			reusing = 1;
			break;
		}
		d_l2_conn = d_l2_conn -> diag_l2_next ;
	}

	if (d_l2_conn == NULL)
	{
		/* New connection */
		if (diag_calloc(&d_l2_conn, 1))
			return 0;

		reusing = 0;
	}

	dl2l = diag_l0_dl2_link(dl0d);
	if (dl2l == NULL)
		return NULL;

	/* Link to the L1 device info that we keep (name, type, flags, dl0d) */
	d_l2_conn->diag_link = dl2l;

	/* Look up the protocol we want to use */

	d_l2_conn->l2proto = 0;

	for (node = l2proto_list; node != NULL; node = node->next) {
		if (node->l2proto->diag_l2_protocol == L2protocol) {
			d_l2_conn->l2proto = node->l2proto;
			break;
		}
	}

	if (d_l2_conn->l2proto == 0) {
		fprintf(stderr,
			FLFMT "Protocol %d not installed.\n", FL, L2protocol);
		return NULL;
	}

	d_l2_conn->diag_l2_type = type ;
	d_l2_conn->diag_l2_srcaddr = source ;
	d_l2_conn->diag_l2_destaddr = target ;

	/*
	 * We are going to assume that the ISO default timing values
	 * are general suitable defaults
	 */
	d_l2_conn->diag_l2_p1min = ISO_14230_TIM_MIN_P1;
	d_l2_conn->diag_l2_p1max = ISO_14230_TIM_MAX_P1;
	d_l2_conn->diag_l2_p2min = ISO_14230_TIM_MIN_P2;
	d_l2_conn->diag_l2_p2max = ISO_14230_TIM_MAX_P2;
	d_l2_conn->diag_l2_p2emin = ISO_14230_TIM_MIN_P2E;
	d_l2_conn->diag_l2_p2emax = ISO_14230_TIM_MAX_P2E;
	d_l2_conn->diag_l2_p3min = ISO_14230_TIM_MIN_P3;
	d_l2_conn->diag_l2_p3max = ISO_14230_TIM_MAX_P3;
	d_l2_conn->diag_l2_p4min = ISO_14230_TIM_MIN_P4;
	d_l2_conn->diag_l2_p4max = ISO_14230_TIM_MAX_P4;

	d_l2_conn -> diag_l2_state = DIAG_L2_STATE_CLOSED;

	/* Now do protocol version of StartCommunications */

	flags = type&0xffff;

	rv = d_l2_conn->l2proto->diag_l2_proto_startcomms(d_l2_conn,
	flags, bitrate, target, source);

	if (rv < 0)
	{
		/* Something went wrong */
		if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,FLFMT "protocol startcomms returned %d\n", FL, rv);

		if (reusing == 0)
			free(d_l2_conn);

		/* XXX tidy structures... We possibly freed d_l2_conn but we set to NULL anyway ?? */

		d_l2_conn = NULL;
		return (struct diag_l2_conn *)diag_pseterr(rv);
	}

	/*
	 * note target for quick lookup of connection for received messages
	 * - unless we're re-using a established connection, then no need
	 * to re-note.
	 */
	if ( (reusing == 0) && d_l2_conn )
	{
		/* And attach connection info to our main list */
		d_l2_conn->next = diag_l2_connections ;
		diag_l2_connections = d_l2_conn ;

/* XXX insert in second linked list */

		diag_l2_conbyid[target] = d_l2_conn;

	}
	if (d_l2_conn)
		d_l2_conn -> diag_l2_state = DIAG_L2_STATE_OPEN;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_StartComms returns %p\n",
				FL, d_l2_conn);

	return d_l2_conn;
}

/*
 * Stop communications - stop talking to an ECU
 * - some L2 protocols have an ordered mechanism to do this, others are
 * just timeout based (i.e don't send anything for 5 seconds)
 */
int
diag_l2_StopCommunications(struct diag_l2_conn *d_l2_conn)
{
	d_l2_conn->diag_l2_state = DIAG_L2_STATE_CLOSING;

	/*
	 * Call protocol close routine, if it exists
	 */
	if (d_l2_conn->l2proto->diag_l2_proto_stopcomms)
		(void)d_l2_conn->l2proto->diag_l2_proto_stopcomms(d_l2_conn);

	d_l2_conn->diag_l2_state = DIAG_L2_STATE_CLOSED;
	return 0;
}

/*
 * Get the time of the last send time, and calculate expiration.
 */

void
diag_l2_sendstamp(struct diag_l2_conn *d_l2_conn) {

	/*
	 * Get the current time.
	 */
	(void) gettimeofday(&d_l2_conn->diag_l2_lastsend, NULL);

	/*
	 * Calculate the expiration time, we use 2/3 of P3max
	 * to calculate when to call the L2 protocol timeout() routine
	 */
	d_l2_conn->diag_l2_expiry = d_l2_conn->diag_l2_lastsend;

	d_l2_conn->diag_l2_expiry.tv_sec +=  (d_l2_conn->diag_l2_p3max*2/3) / 1000;
	d_l2_conn->diag_l2_expiry.tv_usec +=
		((d_l2_conn->diag_l2_p3max*2/3) % 1000) * 1000;

	if (d_l2_conn->diag_l2_expiry.tv_usec > 1000000)
	{
		d_l2_conn->diag_l2_expiry.tv_sec ++;
		d_l2_conn->diag_l2_expiry.tv_usec -= 1000000;
	}
}

/*
 * Send a message. This is synchronous.
 */
int
diag_l2_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_send %p msg %p msglen %d called\n",
				FL, d_l2_conn, msg, msg->len);

	diag_l2_sendstamp(d_l2_conn);	/* Save timestamps */

	/* Call protocol specific send routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_send(d_l2_conn, msg);

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "diag_l2_send returns %d\n",
				FL, rv);

	return rv;
}

/*
 * Send a message, and wait the appropriate time for a response and return
 * that message or an error indicator
 * This is synchronous and sleeps and is meant too.
 */
struct diag_msg *
diag_l2_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval)
{
	struct diag_msg *rv;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_msg %p %p called\n",
				FL, d_l2_conn, msg);

	/* Call protocol specific send routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_request(d_l2_conn, msg, errval);

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "diag_l2_request returns %p, err %d\n",
				FL, rv, *errval);

	return rv;
}


/*
 * Recv a message - will end up calling the callback routine with a message
 * or an error if an error has occurred
 *
 * At the moment this sleeps and the callback will happen before the recv()
 * returns - this is not the intention
 *
 * Timeout is in ms
 */
int
diag_l2_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle)
{
	int rv;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_recv %p timeout %d called\n",
				FL, d_l2_conn, timeout);

	/* Call protocol specific recv routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_recv(d_l2_conn, timeout, callback, handle);

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "diag_l2_recv returns %d\n", FL, rv);
	return rv;
}

/*
 * IOCTL, for setting/asking how various layers are working - similar to
 * Unix ioctl()
 */
int diag_l2_ioctl(struct diag_l2_conn *d_l2_conn, int cmd, void *data)
{
	struct diag_l0_device *dl0d;
	struct diag_serial_settings *ic;
	int rv = 0;
	struct diag_l2_data *d;

	if (diag_l2_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr,
			FLFMT "diag_l2_ioctl %p cmd %d\n",
				FL, d_l2_conn, cmd);


	dl0d = d_l2_conn->diag_link->diag_l2_dl0d ;

	switch (cmd)
	{
	case DIAG_IOCTL_GET_L1_TYPE:
		*(int *)data = diag_l1_gettype(dl0d);
		break;
	case DIAG_IOCTL_GET_L1_FLAGS:
		*(int *)data = diag_l1_getflags(dl0d);
		break;
	case DIAG_IOCTL_GET_L2_FLAGS:
		*(int *)data = d_l2_conn->l2proto->diag_l2_flags;
		break;
	case DIAG_IOCTL_GET_L2_DATA:
		d = (struct diag_l2_data *)data;
		d->physaddr = d_l2_conn->diag_l2_physaddr;
		d->kb1 = d_l2_conn->diag_l2_kb1;
		d->kb2 = d_l2_conn->diag_l2_kb2;
		break;
	case DIAG_IOCTL_SETSPEED:
		ic = (struct diag_serial_settings *)data;
		rv = diag_l1_setspeed(dl0d, ic);
		break;
	case DIAG_IOCTL_INITBUS:
		rv = diag_l1_initbus(dl0d, (struct diag_l1_initbus_args *)data);
		break;
	default:
		rv = 0;	/* Do nothing, quietly */
		break;
	}

	return rv;
}
