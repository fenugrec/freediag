/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * 2009-2016 fenugrec
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
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_err.h"

#include "diag_l2.h"
#include "utlist.h"


int diag_l2_debug;


/* struct to manage L2 stuff, used in here only */
static struct {
	diag_mtx connlist_mtx;            // mutex for accessing dl2conn_list
	struct diag_l2_conn *dl2conn_list; // linked-list of current diag_l2_conn-s
	struct diag_l2_link *dl2l_list;    // linked-list of current L2-L0 links
	bool init_done;
} l2internal = {
	.dl2conn_list = NULL,
	.dl2l_list = NULL,	// linked-list of current L2-L0 links
	.init_done = false
};


/** Find an existing L2 link using the specified L0 device.
 * @return NULL if not found
 */
static struct diag_l2_link *
diag_l2_findlink(const struct diag_l0_device *dl0d) {
	struct diag_l2_link *dl2l=NULL;

	LL_FOREACH(l2internal.dl2l_list, dl2l) {
		if (dl2l->l2_dl0d == dl0d) {
			break;
		}
	}

	return dl2l;
}

/*
 *
 * remove a L2 connection from our list
 * - up to the caller to have shut it down properly first
 * Always returns 0
 */

static int diag_l2_rmconn(struct diag_l2_conn *dl2c) {
	assert(dl2c !=NULL);

	diag_os_lock(&l2internal.connlist_mtx);
	LL_DELETE(l2internal.dl2conn_list, dl2c);
	diag_os_unlock(&l2internal.connlist_mtx);

	return 0;
}

/*
 * Called regularly to check timeouts etc (call at least once per
 * second)
 * Note: This is called from a signal handler.
 *
 * XXX Uses functions not async-signal-safe.
 * This parses through the l2internal.dl2conn_list linked-list and
 * calls the  ->diag_l2_proto_timeout function for every dl2conn
 * that has expired.
 *
 * In order for this to work well, all L2
 */
void
diag_l2_timer(void) {
	struct diag_l2_conn	*d_l2_conn;

	unsigned long now;

	now = diag_os_getms(); /* XXX probably Not async safe */

	if (periodic_done() || !diag_os_trylock(&l2internal.connlist_mtx)) {
		return;
	}

	LL_FOREACH(l2internal.dl2conn_list, d_l2_conn) {
		bool expired = 0;

		/*
		 * If in monitor mode, or the connection isn't open,
		 * or L1 does the keepalive, do nothing
		 */
		if (((d_l2_conn->diag_l2_type & DIAG_L2_TYPE_INITMASK) ==DIAG_L2_TYPE_MONINIT) ||
				(d_l2_conn->diag_l2_state != DIAG_L2_STATE_OPEN) ||
				(d_l2_conn->diag_link->l1flags & DIAG_L1_DOESKEEPALIVE)) {
			continue;
		}

		/* Check the send timers vs requested expiry time */

		//we're subtracting unsigned values but since the clock is
		//monotonic, the difference will always be >= 0
		expired = ((now - d_l2_conn->tlast) > d_l2_conn->tinterval)? 1:0 ;

		/* If expired, call the timeout routine */
		if (expired && d_l2_conn->l2proto->diag_l2_proto_timeout) {
			d_l2_conn->l2proto->diag_l2_proto_timeout(d_l2_conn);
		}
	}
	diag_os_unlock(&l2internal.connlist_mtx);
	return;
}

/*
 * Add a message to the message list on the L2 connection
 * (if msg was a chain of messages, they all get added so they don't get lost)
 */
void
diag_l2_addmsg(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	LL_CONCAT(d_l2_conn->diag_msg, msg);
	return;
}

/************************************************************************/
/*  PUBLIC Interface starts here					*/
/************************************************************************/

/*
 * Init called to initialise local structures
 */
int diag_l2_init() {

	if (l2internal.init_done) {
		return 0;
	}

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "entered diag_l2_init\n", FL);

	diag_os_initstaticmtx(&l2internal.connlist_mtx);

	l2internal.dl2l_list = NULL;
	l2internal.dl2conn_list = NULL;

	l2internal.init_done = 1;
	return 0;
}

//diag_l2_end : opposite of diag_l2_init !
int diag_l2_end() {
	diag_os_delmtx(&l2internal.connlist_mtx);
	l2internal.init_done = 0;
	return 0;
}

/** Remove a dl2l from the linked list,
 * close its dl0d link with diag_l1_close and free
 * the dl2l.
 * This should only be called if we're sure the dl2l
 * is not used anymore...
 */
static int
diag_l2_closelink(struct diag_l2_link *dl2l) {
	assert(dl2l != NULL);

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		FLFMT "l2_closelink %p called\n", FL, (void *)dl2l);

	/* Remove from linked-list */
	LL_DELETE(l2internal.dl2l_list, dl2l);

	if (dl2l->l2_dl0d == NULL) {
		fprintf(stderr, FLFMT "**** Corrupt DL2L !! Report this !!!\n", FL);
	} else {
		diag_l1_close(dl2l->l2_dl0d);
	}

	free(dl2l);

	return 0;
}

/*
 * Open an L2 link over specified diag_l0_device.
 * Aborts if the L1 protocol doesn't match.
 * Ret 0 if ok
 *
 * We need to specify the L1 protocol, because some L1
 * interfaces are smart and can support multiple protocols, also L2 needs
 * to know later (and asks L1)
 */
int
diag_l2_open(struct diag_l0_device *dl0d, int L1protocol) {
	int rv;
	struct diag_l2_link *dl2l;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "l2_open %s on %p, L1proto=%d\n",
		FL, dl0d->dl0->longname, (void *)dl0d, L1protocol);

	/* try to find in linked list */
	dl2l = diag_l2_findlink(dl0d);

	if (dl2l) {
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			"\texisting L2 link \"%s\" found\n",
			dl2l->l2_dl0d->dl0->shortname);

		if (dl2l->l1proto != L1protocol) {
			fprintf(stderr, "Problem : L0 open with wrong L1 proto...\n");
			return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
		}

		/* Device was already open, with correct protocol  */
		return 0;
	}

	rv = diag_l1_open(dl0d, L1protocol);
	if (rv) {
		return diag_ifwderr(rv);	//forward error to next level
	}

	/* Create the L2 link */
	if ((rv=diag_calloc(&dl2l, 1))) {
		return diag_ifwderr(rv);
	}

	dl2l->l2_dl0d = dl0d;
	dl2l->l1flags = diag_l1_getflags(dl0d);
	dl2l->l1type = diag_l1_gettype(dl0d);
	dl2l->l1proto = L1protocol;

	/* Put ourselves at the head of the list. */
	LL_PREPEND(l2internal.dl2l_list, dl2l);

	return 0;
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

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		FLFMT "Entered diag_l2_close for dl0d=%p;\n",
		FL, (void *)dl0d);

	assert(dl0d !=NULL);	//crash if it's null. We need to fix these problems.

	diag_os_lock(&l2internal.connlist_mtx);

	// Check if dl0d is still used by someone in l2internal.dl2conn_list
	LL_FOREACH(l2internal.dl2conn_list, d_l2_conn) {
		if (d_l2_conn->diag_link->l2_dl0d == dl0d) {
			fprintf(stderr, FLFMT "Not closing dl0d: used by dl2conn %p!\n", FL,
				(void *) d_l2_conn);
			diag_os_unlock(&l2internal.connlist_mtx);
			return diag_iseterr(DIAG_ERR_GENERAL);	//there's still a dl2conn using it !
		}
	}

	while ((dl2l = diag_l2_findlink(dl0d)) != NULL) {
		/* can't just "LL_FOREACH" since we're removing stuff from the list as we go */
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
			"\tclosing dl2link %p.\n", (void *) dl2l);
		diag_l2_closelink(dl2l);	//closelink calls diag_l1_close() as required
	}

	diag_os_unlock(&l2internal.connlist_mtx);
	return 0;
}

/*
 * startCommunication routine, establishes a connection to
 * an ECU by sending fast/slow start (or whatever the protocol is),
 * and sets all the timer parameters etc etc
 * this creates & alloc's a new diag_l2_conn (freed in diag_l2_stopcomm)
 * we just pass along the flags arg straight to the proto_startcomm()
 * L2protocol : matched with ->diag_l2_protocol constants
 */
struct diag_l2_conn *
diag_l2_StartCommunications(struct diag_l0_device *dl0d, int L2protocol, flag_type flags,
	unsigned int bitrate, target_type target, source_type source) {
	struct diag_l2_conn	*d_l2_conn;
	const struct diag_l2_proto *dl2p;

	struct diag_l2_link *dl2l;
	int i,rv;

	assert(dl0d!=NULL);

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "_startCommunications dl0d=%p L2proto %d flags=0x%X "
		"%ubps target=0x%X src=0x%X\n",
		FL, (void *)dl0d, L2protocol, flags, bitrate,
		target & 0xff, source & 0xff);

	/* there must be a dl2l with the desired dl0d. */
	dl2l = diag_l2_findlink(dl0d);
	if (!dl2l) {
		fprintf(stderr, "No dl2l with requested dl0 !?\n");
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/*
	 * Check connection doesn't exist already, if it does, do not reuse !
	 * with the current L1/L2 structure, hoping to share one L1 between more than one l2
	 * is a bad idea.
	 */

	diag_os_lock(&l2internal.connlist_mtx);

	LL_FOREACH(l2internal.dl2conn_list, d_l2_conn) {
		if (d_l2_conn->diag_link == dl2l) {
			fprintf(stderr, "Already an L2 connection with specified dl0-dl2l, cannot reuse !\n");
			diag_os_unlock(&l2internal.connlist_mtx);
			return diag_pseterr(DIAG_ERR_GENERAL);
		}
	}

	/* Create new L2 connection */
	rv = diag_calloc(&d_l2_conn, 1);
	if (rv != 0) {
		diag_os_unlock(&l2internal.connlist_mtx);
		return diag_pfwderr(rv);
	}
	d_l2_conn->diag_link = dl2l;

	/* Look up the protocol we want to use */

	d_l2_conn->l2proto = NULL;

	for (i=0; l2proto_list[i] ; i++) {
		dl2p = l2proto_list[i];
		if (dl2p->diag_l2_protocol == L2protocol) {
			d_l2_conn->l2proto = dl2p;
			break;
		}
	}

	if (d_l2_conn->l2proto == NULL) {
		fprintf(stderr,
			FLFMT "Protocol %d not installed.\n", FL, L2protocol);
		free(d_l2_conn);
		diag_os_unlock(&l2internal.connlist_mtx);
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

	if (rv < 0) {
		/* Something went wrong */
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			FLFMT "protocol startcomms returned %d\n", FL, rv);

		free(d_l2_conn);
		diag_os_unlock(&l2internal.connlist_mtx);
		return diag_pfwderr(rv);
	}

	/*
	 * note target for quick lookup of connection for received messages
	 * - unless we're re-using a established connection, then no need
	 * to re-note.
	 */

	/* And attach connection info to our main list */
	LL_PREPEND(l2internal.dl2conn_list, d_l2_conn);

	d_l2_conn->tlast=diag_os_getms();
	d_l2_conn->diag_l2_state = DIAG_L2_STATE_OPEN;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "diag_l2_StartComms returns %p\n",
		FL, (void *)d_l2_conn);

	diag_os_unlock(&l2internal.connlist_mtx);
	return d_l2_conn;
}

/*
 * Stop communications - stop talking to an ECU
 * - some L2 protocols have an ordered mechanism to do this, others are
 * just timeout based (i.e don't send anything for 5 seconds)
 * this also free()s d_l2_conn (alloced in startcomm)
 * and removes it from l2internal.dl2conn_list
 */
int
diag_l2_StopCommunications(struct diag_l2_conn *d_l2_conn) {
	assert(d_l2_conn != NULL);

	d_l2_conn->diag_l2_state = DIAG_L2_STATE_CLOSING;

	/*
	 * Call protocol close routine, if it exists
	 */
	if (d_l2_conn->l2proto->diag_l2_proto_stopcomms) {
		(void)d_l2_conn->l2proto->diag_l2_proto_stopcomms(d_l2_conn);
	}

	//remove from the main linked list
	diag_l2_rmconn(d_l2_conn);

	//We assume the protocol-specific _stopcomms() cleared out anything it
	//may have alloc'ed. inside the l2 connection struct.
	// But we might still have some attached messages that
	//were never freed, so we need to purge those:
	if (d_l2_conn->diag_msg != NULL) {
		diag_freemsg(d_l2_conn->diag_msg);
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
diag_l2_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		FLFMT "diag_l2_send %p msg %p msglen %u called\n",
		FL, (void *)d_l2_conn, (void *)msg, msg->len);

	/* Call protocol specific send routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_send(d_l2_conn, msg);

	if (rv==0) {
		//update timestamp
		d_l2_conn->tlast = diag_os_getms();
	}


	return rv? diag_ifwderr(rv):0 ;
}

/*
 * Send a message, and wait the appropriate time for a response and return
 * that message or an error indicator
 * This is synchronous and sleeps and is meant too.
 */
struct diag_msg *
diag_l2_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval) {
	struct diag_msg *rxmsg;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		FLFMT "_request dl2c=%p msg=%p called\n",
		FL, (void *)d_l2_conn, (void *)msg);

	/* Call protocol specific send routine */
	rxmsg = d_l2_conn->l2proto->diag_l2_proto_request(d_l2_conn, msg, errval);

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		FLFMT "_request returns %p, err %d\n",
		FL, (void *)rxmsg, *errval);

	if (rxmsg==NULL) {
		return diag_pfwderr(*errval);
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
diag_l2_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle) {
	int rv;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "diag_l2_recv %p timeout %u called\n",
		FL, (void *)d_l2_conn, timeout);

	/* Call protocol specific recv routine */
	rv = d_l2_conn->l2proto->diag_l2_proto_recv(d_l2_conn, timeout, callback, handle);

	if (rv==0) {
		//update timers if success
		d_l2_conn->tlast = diag_os_getms();
	} else {
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
			FLFMT "diag_l2_recv returns %d\n", FL, rv);
	}

	return rv;
}

/*
 * IOCTL, for setting/asking how various layers are working - similar to
 * Unix ioctl()
 * ret 0 if ok
 */
int diag_l2_ioctl(struct diag_l2_conn *d_l2_conn, unsigned int cmd, void *data) {
	struct diag_l0_device *dl0d;
	int rv = 0;
	struct diag_l2_data *d;
	struct diag_l2_link *dl2l;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "diag_l2_ioctl %p cmd 0x%X\n",
		FL, (void *)d_l2_conn, cmd);


	dl2l = d_l2_conn->diag_link;
	dl0d = dl2l->l2_dl0d ;

	switch (cmd) {
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
		if (dl2l->l1flags & (DIAG_L1_AUTOSPEED | DIAG_L1_NOTTY)) {
			break;
		}
		rv = diag_l1_ioctl(dl0d, cmd, data);
		break;
	case DIAG_IOCTL_IFLUSH:
		if (dl2l->l1flags & DIAG_L1_NOTTY) {
			break;
		}
		rv = diag_l1_ioctl(dl0d, cmd, data);
		break;
	case DIAG_IOCTL_SETWM:
		if (!(dl2l->l1flags & DIAG_L1_DOESKEEPALIVE)) {
			break;
		}
		rv = diag_l1_ioctl(dl0d, cmd, data);
		break;
	case DIAG_IOCTL_INITBUS:
		//fall-through to L1
	default:
		/* Not implemented by L2 : forward to L1 */
		rv = diag_l1_ioctl(dl0d, cmd, data);
		break;
	}

	return rv? diag_ifwderr(rv):0 ;
}
