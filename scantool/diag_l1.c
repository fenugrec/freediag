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
 * L1 diagnostic interface, generic routines
 *
 * These look much like the L0 interface, but handle things such
 * as de-duplexing etc
 *
 * This is written so that sometime this can dynamically support more
 * than one L0 interface - I don't have more than one (or more than one type)
 * so it's not completely that way :-(
 *
 * HOWEVER, if the L0 interface has multiple interfaces in it, which have
 * different flags, then this code needs some enhancements. One of the
 * interfaces we use does have this (multiplex engineering interface)
 */


#include <errno.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"


int diag_l0_debug;	//debug flags for l0
int diag_l1_debug;	//debug flags for l1


static int diag_l1_saferead(struct diag_l0_device *dl0d,
uint8_t *buf, size_t bufsiz, int timeout);

/*
 * l0dev_list : Linked list of supported L0 devices.
 * The devices should be added with "diag_l1_add_l0dev".
 */

struct diag_l0_node {
	const struct diag_l0 *l0dev;
	struct diag_l0_node *next;
} *l0dev_list;


//diag_l1_add_l0dev : this is called by each diag_l0_??_add function.
//It fills the l0dev_list linked list.
int
diag_l1_add_l0dev(const struct diag_l0 *l0dev) {
	int rv;

	struct diag_l0_node *last_node, *new_node;

	if (l0dev_list == NULL) {
		/*
		 * No devices yet, create the root.
		 */
		if ( (rv = diag_calloc(&l0dev_list, 1)) )
			return diag_iseterr(DIAG_ERR_NOMEM);

		l0dev_list->l0dev = l0dev;
		return 0;
	}

	//set last_node to the last element of l0dev_list
	for (last_node = l0dev_list; last_node != NULL; last_node = last_node->next)
		if (last_node->l0dev == l0dev)
			return diag_iseterr(DIAG_ERR_GENERAL);	/* Already in the list! */

	if ( (rv = diag_calloc(&new_node, 1)) )
		return diag_iseterr(DIAG_ERR_NOMEM);

	/* Find the last non-NULL node...*/
	for (last_node = l0dev_list; last_node->next != NULL; last_node = last_node->next)
		/* Search for the next-to-last node */;

	new_node->l0dev = l0dev;
	last_node->next = new_node;

	return 0;
}

/* Global init flag */
static int diag_l1_initdone=0;

//diag_l1_init : parse through the l0dev_list linked list
//and call diag_l0_init for each of them
int
diag_l1_init(void)
{
	struct diag_l0_node *node;

	if (diag_l1_initdone)
		return 0;

	if (diag_l1_debug & DIAG_DEBUG_INIT)
		fprintf(stderr,FLFMT "entered diag_l1_init\n", FL);


	/* Now call the init routines for the L0 devices */
	//NOTE : the diag_l0_init functions should NOT play any mem tricks (*alloc etc) or open handles.
	//That way we won't need to add a diag_l0_end function.
	//Unfortunately they do : l0dev_list is a linked-list calloc'ed by diag_l1_add_l0dev !

	for (node = l0dev_list; node; node = node->next) {
		if (node->l0dev->diag_l0_init)
			(node->l0dev->diag_l0_init)();
	}

	diag_l1_initdone = 1;
	return 0;
}

//diag_l1_end : opposite of diag_l1_init . Non-critical for now
int diag_l1_end(void) {
	diag_l1_initdone=0;
	return 0;
}

/*
 * Open the diagnostic device, return a new diag_l0_device .
 *
 * Finds the unique name in the l0 device linked-list (l0dev_list),
 * calls its diag_l0_open function.
 *
 * This is passed a L1 subinterface (ie, what type of physical interface
 * to run on)
 */
struct diag_l0_device *
diag_l1_open(const char *name, const char *subinterface, int l1protocol)
{
	struct diag_l0_node *node;
	const struct diag_l0 *l0dev;
	if (diag_l1_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr, FLFMT "diag_l1_open %s on %s with l1 proto %d\n", FL,
				name, subinterface, l1protocol);

	for (node = l0dev_list; node; node = node->next) {
		l0dev = node->l0dev;
		if (strcmp(name, l0dev->diag_l0_name) == 0)
		{
			/* Found it */

			/* Check h/w supports this l1 protocol */
			if ((l0dev->diag_l0_type & l1protocol) == 0)
				return diag_pseterr(DIAG_ERR_PROTO_NOTSUPP);

			/* Call the open routine */
			// Forward the requested L1 protocol
			return (l0dev->diag_l0_open)(subinterface, l1protocol);
		}
	}
	fprintf(stderr, FLFMT "diag_l1_open: did not recognize %s\n", FL, name);
	/* Not found */
	return diag_pseterr(DIAG_ERR_BADIFADAPTER);
}

//diag_l1_close : call the ->diag_l0_close member of the
//specified diag_l0_device.
int
diag_l1_close(struct diag_l0_device **ppdl0d)
{
	if (diag_l1_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, FLFMT "entering diag_l1_close: ppdl0d=%p\n", FL,
			(void *) ppdl0d);
	if (ppdl0d && *ppdl0d) {
		(*ppdl0d)->dl0->diag_l0_close(ppdl0d);
		*ppdl0d=NULL;
	}
	return 0;
}

/*
 * Do wakeup/init on the net.
 * Caller must have waited the appropriate time before calling this, since any
 * bus-idle requirements are specified at the L2 level.
 */
int
diag_l1_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	return dl0d->dl0->diag_l0_initbus(dl0d, in);
}

/*
 * Send a load of data
 *
 * P4 is the inter byte gap
 *
 * This does very un-clever half duplex removal, there better not be
 * any outstanding data on the bus (or in the l0 buffers) or this
 * will think it has a half-duplex failure, i.e a bus error
 *
 * Returns 0 on success
 */
int
diag_l1_send(struct diag_l0_device *dl0d, const char *subinterface, const void *data, size_t len, unsigned int p4)
{
	int rv = DIAG_ERR_GENERAL;
	uint32_t l0flags;
	const struct diag_l0 *dl0 = dl0d->dl0;
	uint8_t duplexbuf[MAXRBUF];

	if (len > MAXRBUF)
		return diag_iseterr(DIAG_ERR_BADLEN);

	/*
	 * If p4 is zero and not in half duplex mode, or if
	 * L1 is a "DOESL2" interface, or if L0 takes care of P4 waits:
	 * send the whole message to L0 as one write
	 */
	l0flags = diag_l1_getflags(dl0d);

	if (diag_l1_debug & DIAG_DEBUG_WRITE) {
			fprintf(stderr, FLFMT "diag_l1_send: len=%d P4=%u l0flags=%X\n", FL,
					(int) len, p4, l0flags);
	}


	if (   ((p4 == 0) && ((l0flags & DIAG_L1_HALFDUPLEX) == 0)) ||
		(l0flags & DIAG_L1_DOESL2FRAME) || (l0flags & DIAG_L1_DOESP4WAIT) ||
		((p4==0) && (l0flags & DIAG_L1_BLOCKDUPLEX)) ) {
		/*
		 * Send the lot
		 */
		rv = (dl0->diag_l0_send)(dl0d, subinterface, data, len);
		//optionally remove echos
		if ((l0flags & DIAG_L1_BLOCKDUPLEX) && (rv==0)) {
			//try to read the same number of sent bytes; timeout=300ms + 1ms/byte
			//This is plenty OK for typical 10.4kbps but should be changed
			//if ever slow speeds are used.
			if (diag_l1_saferead(dl0d, duplexbuf, len, 300+len) != (int) len) {
				rv=DIAG_ERR_GENERAL;
			}
			//compare to sent bytes
			if ( memcmp(duplexbuf, data, len) !=0) {
				fprintf(stderr,FLFMT "Bus Error: bad half duplex echo!\n", FL);
				rv=DIAG_ERR_BUSERROR;
			}
		}
	} else {
		const uint8_t *dp = (const uint8_t *)data;

		/* Send each byte */
		while (len--) {
			rv = (dl0->diag_l0_send)(dl0d, subinterface, dp, 1);
			if (rv != 0)
				break;

			/*
			 * If half duplex, read back the echo, if
			 * the echo is wrong then this is an error
			 * i.e something wrote on the diag bus whilst
			 * we were writing
			 */
			if (l0flags & DIAG_L1_HALFDUPLEX) {
				uint8_t c;

				c = *dp - 1; /* set it with wrong val. XXXX WHY 1000ms timeout !? */
				if (diag_l1_saferead(dl0d, &c, 1, 1000) < 0) {
					rv=DIAG_ERR_GENERAL;
					break;
				}

				if (c != *dp) {
					if (c == *dp - 1)
						fprintf(stderr,"Half duplex interface not echoing!\n");
					else
						fprintf(stderr,"Bus Error: got 0x%X expected 0x%X\n",
							c, *dp);
					rv = DIAG_ERR_BUSERROR;
					break;
				}
			}
			dp++;

			if (p4)	/* Inter byte gap */
				diag_os_millisleep(p4);
		}
	}

	return rv? diag_iseterr(rv):0;
}

/*
 * Get data (blocking, unless timeout is 0)
 * returns # of bytes read, or <0 if error.
 * XXX currently nothing handles the case of L0 returning 0 bytes read. Logically that could
 * only happen when requesting n bytes with a timeout of 0; otherwise DIAG_ERR_TIMEOUT will
 * generated.
 */
int
diag_l1_recv(struct diag_l0_device *dl0d,
	const char *subinterface, void *data, size_t len, int timeout)
{
	int rv;
	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (timeout==0)
		fprintf(stderr, FLFMT "Interesting : L1 read with timeout=0. Report this !\n", FL);

	rv=dl0d->dl0->diag_l0_recv(dl0d, subinterface, data, len, timeout);
	if (rv==0)
		fprintf(stderr, FLFMT "Interesting : L0 returns with 0 bytes... Report this !\n", FL);

	return rv;
}

/*
 * Set speed/parity etc; this should only be called through diag_l2_ioctl
 */
int
diag_l1_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset)
{
	return dl0d->dl0->diag_l0_setspeed(dl0d, pset);
}

//diag_l1_getflags: returns l0 flags
uint32_t diag_l1_getflags(struct diag_l0_device *dl0d)
{
	return dl0d->dl0->diag_l0_getflags(dl0d);
}

//diag_l1_gettype: returns diag_l0_type :supported L1 protos
//of the l0 driver
int diag_l1_gettype(struct diag_l0_device *dl0d)
{
	return dl0d->dl0->diag_l0_type;
}


//diag_l1_saferead : only used to remove half-duplex echos...
//return <0 on error, number of bytes on success
static int
diag_l1_saferead(struct diag_l0_device *dl0d, uint8_t *buf, size_t bufsiz, int timeout)
{
	int xferd;

	while ( (xferd = diag_tty_read(dl0d, buf, bufsiz, timeout)) < 0) {
		if (errno != EINTR)
			return diag_iseterr(DIAG_ERR_BUSERROR);
		xferd = 0; /* Interrupted read, nothing transferred. */
	}

	return xferd;
}
