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
#include <assert.h>

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

//diag_l2_add_protocol : fill in the l2proto_list linked list
int
diag_l2_add_protocol(const struct diag_l2_proto *l2proto) {
	int rv;

	struct diag_l2_node *last_node, *new_node;

	if (l2proto_list == NULL) {
		/*
		 * No devices yet, create the root.
		 */
		if ( (rv = diag_calloc(&l2proto_list, 1)) )
			return diag_iseterr(DIAG_ERR_NOMEM);

		l2proto_list->l2proto = l2proto;
		return 0;
	}

	//set last_node to the last element of l2proto_list
	for (last_node = l2proto_list; last_node != NULL ; last_node = last_node->next)
		if (last_node->l2proto == l2proto)
			return diag_iseterr(DIAG_ERR_GENERAL);	/* Already there. */

	if ( (rv = diag_calloc(&new_node, 1)) )
		return diag_iseterr(DIAG_ERR_NOMEM);

	/* Find the last non-NULL node...*/

	for (last_node = l2proto_list; last_node->next != NULL ;
				last_node = last_node->next)
	/* Do Nothing */ ;

	new_node->l2proto = l2proto;
	last_node->next = new_node;

	return 0;
}


/*
 * The list of connections, not searched often so no need to hash
 * (maybe when we actually finish supporting talking to more than
 * one ECU on one interface at once)
 */
#define CONBYIDSIZE 256

static struct diag_l2_conn	*diag_l2_connections=NULL;	//linked-list of current diag_l2_conn's
static struct diag_l2_conn	*diag_l2_conbyid[CONBYIDSIZE];	/* Look up by ECU address */

static int diag_l2_init_done=0;	/* Init done */

/*
 * The list of L1 devices we have open, not used often so no need to hash
 * (diag_l2_links is a linked list)
 */
static struct diag_l2_link *diag_l2_links;

/*
 * Find our link to the L1 device, by name
 * (try to match dev_name with  ->diag_l2_name of one of the diag_l2_link
 * elements of the diag_l2_links linked-list.
 */
static struct diag_l2_link *
diag_l2_findlink(const char *dev_name)
{
	struct diag_l2_link *dl2l = diag_l2_links;

	while (dl2l)
	{
		if ( strcmp(dl2l->diag_l2_name , dev_name) == 0)
			return dl2l;
		dl2l = dl2l->next;
	}
	return NULL;
}

/*
 * Remove a link from the diag_l2_links linked list.
 * caller should free the dl2l afterwards.
 * This is only called from diag_l2_closelink();
 *
 */
void diag_l2_rmlink(struct diag_l2_link *dl2l) {
	struct diag_l2_link *dltemp, *d_l2_last;

	if (diag_l2_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, FLFMT "l2_rmlink: removing %p from diag_l2_links\n",
				FL, (void *) dl2l);

	assert(dl2l != NULL);

	dltemp = diag_l2_links;
	d_l2_last = NULL;

	while (dltemp)
	{
		if (dltemp == dl2l)
		{
			if (d_l2_last)
				d_l2_last = dl2l->next;
			else
				diag_l2_links = dl2l->next;
			break;
		}
		d_l2_last = dltemp;
		dltemp = dltemp->next;
	}
	return;
}

/*
 *
 * remove a L2 connection from our list
 * - up to the caller to have shut it down properly first
 * Always returns 0
 */

static int diag_l2_rmconn(struct diag_l2_conn *dl2c)
{
	struct diag_l2_conn	*d_l2_conn = diag_l2_connections;
	struct diag_l2_conn	*d_l2_last_conn = NULL;

	assert(dl2c !=NULL);

	while (d_l2_conn)
	{
		if (d_l2_conn == dl2c)
		{
			/* Remove it from list */
			if (d_l2_last_conn)
				d_l2_last_conn->next = dl2c->next ;
			else
				diag_l2_connections = dl2c->next;

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
 * This parses through the diag_l2_connections linked-list and
 * calls the  ->diag_l2_proto_timeout function for every dl2conn
 * that has expired.
 *
 * In order for this to work well, all L2
 */
void
diag_l2_timer(void)
{
	struct diag_l2_conn	*d_l2_conn;

	unsigned long now;
	now=diag_os_getms();	/* XXX probably Not async safe */

	for (d_l2_conn = diag_l2_connections;
		d_l2_conn; d_l2_conn = d_l2_conn->next)
	{
		int expired = 0;

		/*
		 * If in monitor mode, or the connection isn't open,
		 * or L1 does the keepalive, do nothing
		 */
		if (((d_l2_conn->diag_l2_type & DIAG_L2_TYPE_INITMASK) ==DIAG_L2_TYPE_MONINIT) ||
				!(d_l2_conn->diag_l2_state & DIAG_L2_STATE_OPEN) ||
				(d_l2_conn->diag_link->diag_l2_l1flags & DIAG_L1_DOESKEEPALIVE)) {
			continue;
		}

		/* Check the send timers vs requested expiry time */

		//we're subtracting unsigned values but since the clock is
		//monotonic, the difference will always be >= 0
		expired = ((now - d_l2_conn->tlast) > d_l2_conn->tinterval)? 1:0 ;

		/* If expired, call the timeout routine */
		if (expired && d_l2_conn->l2proto->diag_l2_proto_timeout)
			d_l2_conn->l2proto->diag_l2_proto_timeout(d_l2_conn);
	}
}

/*
 * Add a message to the message list on the L2 connection
 * (if msg was a chain of messages, they all get added so they don't get lost)
 */
void
diag_l2_addmsg(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	struct diag_msg *tmsg = d_l2_conn->diag_msg;

	if (d_l2_conn->diag_msg == NULL)
	{
		d_l2_conn->diag_msg = msg;
//		d_l2_conn->diag_msg->mcnt = 1;
		return;
	}
	/* Add to end of list */
	while (tmsg)
	{
		if (tmsg->next == NULL)
		{
			tmsg->next = msg;
//			d_l2_conn->diag_msg->mcnt ++;
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
	if ( diag_l2_init_done )
		return 0;

	if (diag_l2_debug & DIAG_DEBUG_INIT)
		fprintf(stderr,FLFMT "entered diag_l2_init\n", FL);


	memset(diag_l2_conbyid, 0, CONBYIDSIZE);

	diag_l2_init_done = 1;

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
 * diag_l2_closelink :
 * remove it from the linked list,
 * close its dl0d link with diag_l1_close and free
 * the dl2l.
 * This should only be called if we're sure the dl2l
 * is not used anymore...
 */
static int
diag_l2_closelink(struct diag_l2_link *dl2l)
{
	assert(dl2l != NULL);


	if (diag_l2_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr,FLFMT "l2_closelink %p called\n",
			FL, (void *)dl2l);

	/* Clear out this link */
	diag_l2_rmlink(dl2l);	/* Take off list */
	if (dl2l->diag_l2_dl0d == NULL)
		fprintf(stderr, FLFMT "**** Corrupt DL2L !! Report this !!!\n", FL);
	else
		diag_l1_close(&dl2l->diag_l2_dl0d);

	free(dl2l);

	return 0;
}

/*
 * Open a link to a Layer 1 device, returns dl0d. If device is already
 * open but its l1 protocol is different, close and then re-open it;
 * if it matches L1proto return the existing dl0d.
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
			FLFMT "l2_open %s on %s, L1proto=%d\n",
			FL, dev_name, subinterface, L1protocol);

	dl2l = diag_l2_findlink(dev_name);

	if (dl2l)
	{
		if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr, "\texisting L2 link \"%s\" found\n", dl2l->diag_l2_name);
		/* device open */
		if (dl2l->diag_l2_l1protocol != L1protocol) {
			/* Wrong L1 protocol, close link */
			diag_l2_closelink(dl2l);
			dl2l=NULL;
		} else 	{
			/* Device was already open, with correct protocol  */
			return dl2l->diag_l2_dl0d;
		}
	}


	/* Else, create the link */
	if ((rv=diag_calloc(&dl2l, 1))) {
		return diag_pseterr(rv);
	}

	dl0d = diag_l1_open(dev_name, subinterface, L1protocol);
	if (dl0d == NULL)
	{
		rv=diag_geterr();
		free(dl2l);
		return diag_pseterr(rv);	//forward error to next level
	}

//	diag_l0_set_dl2_link(dl0d, dl2l);	/* Associate ourselves with this */
	dl0d->dl2_link=dl2l;

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
 * must have closed all the L3 connections relating to this or they will
 * just get left hanging using resources.
 * XXX what do we want to accomplish here ?
 * We can't have multiple L2 links with the same diag_l0_device, because
 * of how diag_l2_open and diag_l2_findlink work.
 * This func will probably need to be modified if we want to do fancy
 * multi-protocol / multi-L2 stuff...
 *
 * Currently this checks if there's a dl2_link using that dl0d.
 * If there isn't, we close it.
 * If there's a dl2 link, we check if it's still used by parsing
 * through all the diag_l2_conns.
 */
int
diag_l2_close(struct diag_l0_device *dl0d) {
	struct diag_l2_conn *d_l2_conn;
	struct diag_l2_link *dl2l;

	if (diag_l2_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr,FLFMT "Entered diag_l2_close for dl0d=%p;\n",
			FL, (void *)dl0d);

	assert(dl0d !=NULL);	//crash if it's null. We need to fix these problems.

	if (dl0d->dl2_link != NULL) {
		// Check if dl2_link is still referenced by someone in diag_l2_connections
		for (d_l2_conn = diag_l2_connections;
			d_l2_conn; d_l2_conn = d_l2_conn->next) {
			if (d_l2_conn->diag_link == dl0d->dl2_link) {
				fprintf(stderr, FLFMT "Not closing dl0d: used by dl2conn %p!\n", FL,
					(void *) d_l2_conn);
				return 0;	//there's still a dl2conn using it !
			}
		}
		//So we found nobody in diag_l2_connections that uses this dl0d + dl2link.
		if (diag_l2_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, "\tclosing unused dl2link %p.\n",
				(void *)dl0d->dl2_link);
		diag_l2_closelink(dl0d->dl2_link);	//closelink calls diag_l1_close() as required
		//
		//dl0d->dl2_link = NULL;
		return 0;
	}
	// this dl0d had no ->dl2_link; check in the linked-list anyway in case
	// it was orphaned (i.e. dl0d->dl2_link was not set properly...)
	for ( dl2l = diag_l2_links; dl2l; dl2l=dl2l->next) {
			if (dl2l->diag_l2_dl0d == dl0d) {
				fprintf(stderr, FLFMT "Not closing dl0d: used by dl2link %p!\n", FL,
					(void *) dl2l);
				return 0;	//there's still a dl2link using it !
			}
	}
	//So we parsed all d2 links and found no parents; let's close dl0d.

	if (diag_l2_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, "\tclosing unused dl0d.\n");
	diag_l1_close(&dl0d);

	return 0;
}

/*
 * startCommunication routine, establishes a connection to
 * an ECU by sending fast/slow start (or whatever the protocol is),
 * and sets all the timer parameters etc etc
 * this creates & alloc's a new diag_l2_conn (freed in diag_l2_stopcomm)
 * we just pass along the flags arg straight to the proto_startcomm()
 */
struct diag_l2_conn *
diag_l2_StartCommunications(struct diag_l0_device *dl0d, int L2protocol, flag_type flags,
	unsigned int bitrate, target_type target, source_type source)
{
	struct diag_l2_conn	*d_l2_conn;
	struct diag_l2_node *node;

	struct diag_l2_link *dl2l;
	int rv;
	int reusing = 0;

	assert(dl0d!=NULL);

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "_startCommunications dl0d=%p L2proto %d flags %X %ubps target=0x%X src=0x%X\n",
			FL, (void *)dl0d, L2protocol, flags ,
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
		if (d_l2_conn->diag_link != NULL)
			if (d_l2_conn->diag_link->diag_l2_dl0d == dl0d) {
			reusing = 1;
			break;
			}
		d_l2_conn = d_l2_conn->next;
	}

	if (d_l2_conn == NULL)
	{
		/* New connection */
		if (diag_calloc(&d_l2_conn, 1))
			return diag_pseterr(DIAG_ERR_NOMEM);
		reusing = 0;
	}

	dl2l = dl0d->dl2_link;
	//dl2l = diag_l0_dl2_link(dl0d);
	if (dl2l == NULL) {
		free(d_l2_conn);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

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
		free(d_l2_conn);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}


	d_l2_conn->diag_l2_type = flags ;
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

	d_l2_conn->tinterval = (ISO_14230_TIM_MAX_P3 * 2/3);	//default keepalive interval.

	d_l2_conn->diag_l2_state = DIAG_L2_STATE_CLOSED;

	/* Now do protocol version of StartCommunications */

	rv = d_l2_conn->l2proto->diag_l2_proto_startcomms(d_l2_conn,
	flags, bitrate, target, source);

	if (rv < 0)
	{
		/* Something went wrong */
		if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,FLFMT "protocol startcomms returned %d\n", FL, rv);

		if (reusing == 0) {
			free(d_l2_conn);
		}

		return diag_pseterr(rv);
	}

	/*
	 * note target for quick lookup of connection for received messages
	 * - unless we're re-using a established connection, then no need
	 * to re-note.
	 */
	if ( reusing == 0 )
	{
		/* And attach connection info to our main list */
		d_l2_conn->next = diag_l2_connections ;
		diag_l2_connections = d_l2_conn ;

		diag_l2_conbyid[target] = d_l2_conn;

	}
	d_l2_conn->tlast=diag_os_getms();
	d_l2_conn->diag_l2_state = DIAG_L2_STATE_OPEN;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_StartComms returns %p\n",
				FL, (void *)d_l2_conn);

	return d_l2_conn;
}

/*
 * Stop communications - stop talking to an ECU
 * - some L2 protocols have an ordered mechanism to do this, others are
 * just timeout based (i.e don't send anything for 5 seconds)
 * this also free()s d_l2_conn (alloced in startcomm)
 * and removes it from diag_l2_connections and diag_l2_conbyid[]
 */
int
diag_l2_StopCommunications(struct diag_l2_conn *d_l2_conn)
{
	struct diag_msg * tempmsg;

	assert(d_l2_conn != NULL);

	d_l2_conn->diag_l2_state = DIAG_L2_STATE_CLOSING;

	/*
	 * Call protocol close routine, if it exists
	 */
	if (d_l2_conn->l2proto->diag_l2_proto_stopcomms)
		(void)d_l2_conn->l2proto->diag_l2_proto_stopcomms(d_l2_conn);

	//remove from the main linked list
	diag_l2_rmconn(d_l2_conn);

	//and remove from diag_l2_conbyid[]
	int i;
	for (i=0; i < CONBYIDSIZE; i++) {
		if (diag_l2_conbyid[i] == d_l2_conn) {
			diag_l2_conbyid[i]=NULL;
			if (diag_l2_debug & DIAG_DEBUG_CLOSE)
				fprintf(stderr, FLFMT "l2_stopcomm: removing dl2 for ID=%X\n", FL, i);
		}
	}
	//We assume the protocol-specific _stopcomms() cleared out anything it
	//may have alloc'ed. inside the l2 connection struct.
	// But we might still have some attached messages that
	//were never freed, so we need to purge those:
	for (tempmsg=d_l2_conn->diag_msg; tempmsg!=NULL; tempmsg = tempmsg->next) {
		diag_freemsg(tempmsg);
	}
	//and free() the connection.
	free(d_l2_conn);

	return 0;
}


/*
 * Send a message. This is synchronous.
 * calls the appropriate l2_proto_send()
 * and updates the timestamps
 */
int
diag_l2_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_send %p msg %p msglen %d called\n",
				FL, (void *)d_l2_conn, (void *)msg, msg->len);

	/* Call protocol specific send routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_send(d_l2_conn, msg);

	if (rv==0) {
		//update timestamp
		d_l2_conn->tlast = diag_os_getms();
	}


	return rv? diag_iseterr(rv):0 ;
}

/*
 * Send a message, and wait the appropriate time for a response and return
 * that message or an error indicator
 * This is synchronous and sleeps and is meant too.
 */
struct diag_msg *
diag_l2_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval)
{
	struct diag_msg *rxmsg;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "_request dl2c=%p msg=%p called\n",
				FL, (void *)d_l2_conn, (void *)msg);

	/* Call protocol specific send routine */
	rxmsg = d_l2_conn->l2proto->diag_l2_proto_request(d_l2_conn, msg, errval);

	if (diag_l2_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "_request returns %p, err %d\n",
				FL, (void *)rxmsg, *errval);
	}

	if (rxmsg==NULL) {
		return diag_pseterr(*errval);
	}
		//update timers
	d_l2_conn->tlast = diag_os_getms();

	return rxmsg;
}


/*
 * Recv a message - will end up calling the callback routine with a message
 * or an error if an error has occurred
 *
 * At the moment this sleeps and the callback will happen before the recv()
 * returns - this is not the intention XXX we need to clarify this
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
				FL, (void *)d_l2_conn, timeout);

	/* Call protocol specific recv routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_recv(d_l2_conn, timeout, callback, handle);

	if (rv==0) {
		//update timers if success
		d_l2_conn->tlast = diag_os_getms();
	} else if (diag_l2_debug & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "diag_l2_recv returns %d\n", FL, rv);
	}

	return rv;
}

/*
 * IOCTL, for setting/asking how various layers are working - similar to
 * Unix ioctl()
 * ret 0 if ok
 */
int diag_l2_ioctl(struct diag_l2_conn *d_l2_conn, unsigned int cmd, void *data)
{
	struct diag_l0_device *dl0d;
	struct diag_serial_settings *ic;
	int rv = 0;
	struct diag_l2_data *d;

	if (diag_l2_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr,
			FLFMT "diag_l2_ioctl %p cmd %X\n",
				FL, (void *)d_l2_conn, cmd);


	dl0d = d_l2_conn->diag_link->diag_l2_dl0d ;

	switch (cmd)
	{
	case DIAG_IOCTL_GET_L1_TYPE:
		*(int *)data = diag_l1_gettype(dl0d);
		break;
	case DIAG_IOCTL_GET_L1_FLAGS:
		*(uint32_t *)data = diag_l1_getflags(dl0d);
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
		if (dl0d->dl2_link->diag_l2_l1flags & (DIAG_L1_AUTOSPEED | DIAG_L1_NOTTY))
			break;
		ic = (struct diag_serial_settings *)data;
		rv = diag_l1_setspeed(dl0d, ic);
		break;
	case DIAG_IOCTL_INITBUS:
		rv = diag_l1_initbus(dl0d, (struct diag_l1_initbus_args *)data);
		break;
	case DIAG_IOCTL_IFLUSH:
		if (dl0d->dl2_link->diag_l2_l1flags & DIAG_L1_NOTTY)
			break;
		rv = diag_tty_iflush(dl0d);
		break;
	default:
		rv = 0;	/* Do nothing, quietly */
		break;
	}

	return rv? diag_iseterr(rv):0 ;
}
