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

CVSID("$Id$");

int diag_l0_debug;
int diag_l1_debug;

const struct diag_l0 *diag_l0_device_dl0(struct diag_l0_device *dl0d) {
	return dl0d->dl0;
} //XXX this used to be in diag_tty.c ... why ??

static int diag_l1_saferead(struct diag_l0_device *dl0d,
char *buf, size_t bufsiz, int timeout);

/*
 * Linked list of supported L0 devices.
 * The devices should be added with "diag_l1_add_l0dev".
 */

struct diag_l0_node {
	const struct diag_l0 *l0dev;
	struct diag_l0_node *next;
} *l0dev_list;

int
diag_l1_add_l0dev(const struct diag_l0 *l0dev) {
	int rv;

	struct diag_l0_node *last_node, *new_node;

	if (l0dev_list == NULL) {
		/*
		 * No devices yet, create the root.
		 */
		if ( (rv = diag_calloc(&l0dev_list, 1)) ) 
			return rv;

		l0dev_list->l0dev = l0dev;
		return 0;
	}

	for (last_node = l0dev_list; last_node != NULL; last_node = last_node->next)
		if (last_node->l0dev == l0dev)
			return diag_iseterr(DIAG_ERR_GENERAL);	/* Already there. */

	if ( (rv = diag_calloc(&new_node, 1)) ) 
		return rv;

	for (last_node = l0dev_list; last_node->next != NULL; last_node = last_node->next)
		/* Search for the next-to-last node */;

	new_node->l0dev = l0dev;
	last_node->next = new_node;

	return 0;
}

/* Global init flag */
static int diag_l1_initdone;

int
diag_l1_init(void)
{
	struct diag_l0_node *node;

	if (diag_l1_initdone)
		return 0;
	diag_l1_initdone = 1;

	/* Now call the init routines for the L0 devices */
	//NOTE : the diag_l0_init functions should NOT play any mem tricks (*alloc etc) or open handles.
	//That way we won't need to add a diag_l0_end function.

	for (node = l0dev_list; node; node = node->next) {
		if (node->l0dev->diag_l0_init)
			(node->l0dev->diag_l0_init)();
	}

	return 0;
}

//diag_l1_end : opposite of diag_l1_init . Non-critical for now
int diag_l1_end(void) {
	diag_l1_initdone=0;
	return 0;
}

/*
 * Open the diagnostic device, return a dl0 device.
 *
 * Finds the unique name in the l0 device table,
 * calls the init routine, with the device parameter from the table
 *
 * This is passed a L1 subinterface (ie, what type of physical interface
 * to run on)
 */
struct diag_l0_device *
diag_l1_open(const char *name, const char *subinterface, int l1protocol)
{
	struct diag_l0_node *node;
	const struct diag_l0 *l0dev;

	for (node = l0dev_list; node; node = node->next) {
		l0dev = node->l0dev;
		if (strcmp(name, l0dev->diag_l0_name) == 0)
		{
			/* Found it */

			/* Check h/w supports this l1 protocol */
			if ((l0dev->diag_l0_type & l1protocol) == 0)
				return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_PROTO_NOTSUPP);

			/* Call the open routine */
			return (l0dev->diag_l0_open)(subinterface, l1protocol);
		}
	}

	/* Not found */
	return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
}

int
diag_l1_close(struct diag_l0_device **pdl0d)
{
	if (diag_l1_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, FLFMT "entering diag_l1_close\n", FL);
	return pdl0d ?
		(diag_l0_device_dl0(*pdl0d)->diag_l0_close)(pdl0d) : 0;
}

/*
 * Do wakeup/init on the net.
 */
int
diag_l1_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	return (diag_l0_device_dl0(dl0d)->diag_l0_initbus)(dl0d, in);
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
 * Returns 0 on success -1 on failure
 */
int
diag_l1_send(struct diag_l0_device *dl0d, const char *subinterface, const void *data, size_t len, int p4)
{
	int rv = -1;
	int l0flags;
	const struct diag_l0 *dl0 = diag_l0_device_dl0(dl0d);

	/*
	 * If p4 is zero and not in half duplex mode, or if
	 * L1 is a "DOESL2" interface send the whole message to L0
	 * as one write
	 */ 
	l0flags = diag_l1_getflags(dl0d);

	if (   ((p4 == 0) && ((l0flags & DIAG_L1_HALFDUPLEX) == 0)) ||
		(l0flags & DIAG_L1_DOESL2FRAME) || (l0flags & DIAG_L1_DOESP4WAIT) ) {
		/*
		 * Send the lot if we don't need to delay, or collect
		 * the echos
		 */
		rv = (dl0->diag_l0_send)(dl0d, subinterface, data, len);
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

				c = *dp - 1; /* set it with wrong val */
				if (diag_l1_saferead(dl0d, &c, 1, 1000) < 0)
					rv=DIAG_ERR_GENERAL;
					break;

				if (c != *dp) {
					if (c == *dp - 1)
						fprintf(stderr,"Half duplex interface not echoing!\n");
					else
						fprintf(stderr,"Bus Error: got 0x%x expected 0x%x\n",
							c&0xff, *dp & 0xff);
					rv = DIAG_ERR_BUSERROR;
					break;
				}
			}
			dp++;

			if (p4)	/* Inter byte gap */
				diag_os_millisleep(p4);
		}
	}

	return rv;
}

/*
 * Get data (blocking, unless timeout is 0)
 */
int
diag_l1_recv(struct diag_l0_device *dl0d,
	const char *subinterface, void *data, size_t len, int timeout)
{
	return diag_l0_device_dl0(dl0d)->diag_l0_recv(dl0d, subinterface, data, len, timeout);
}

/*
 * Set speed/parity etc
 */
int
diag_l1_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset)
{
	return (diag_l0_device_dl0(dl0d)->diag_l0_setspeed)(dl0d, pset);
}


int diag_l1_getflags(struct diag_l0_device *dl0d)
{
	return (diag_l0_device_dl0(dl0d)->diag_l0_getflags)(dl0d);
}

int diag_l1_gettype(struct diag_l0_device *dl0d)
{
	return diag_l0_device_dl0(dl0d)->diag_l0_type;
}


//return <0 on error, number of bytes on success
static int
diag_l1_saferead(struct diag_l0_device *dl0d, char *buf, size_t bufsiz, int timeout)
{
	int xferd;

	/* And read back the single byte echo, which shows TX completes */
	while ( (xferd = diag_tty_read(dl0d, buf, bufsiz, timeout)) < 0) {
		if (errno != EINTR)
			return diag_iseterr(DIAG_ERR_BUSERROR);
		xferd = 0; /* Interrupted read, nothing transferred. */
	}

	return xferd;
}
