/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * 2009-2015 fenugrec
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


#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"


int diag_l1_debug;	//debug flags for l1



/* Global init flag */
static int diag_l1_initdone=0;

//diag_l1_init : parse through l0dev_list and call each ->init()
int
diag_l1_init(void) {
	if (diag_l1_initdone) {
		return 0;
	}

	DIAG_DBGM(diag_l1_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "entered diag_l1_init\n", FL);

	/* Now call the init routines for the L0 devices */

	//NOTE : ->init functions should NOT play any mem tricks (*alloc etc) or open handles.
	//That way we won't need to add a diag_l0_end function.

	const struct diag_l0 *dl0;
	int i=0;
	while (l0dev_list[i]) {
		dl0=l0dev_list[i];
		if (dl0->init) {
			(void) dl0->init();	//TODO : forward errors up ?
		}
		i++;
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
 * Open the L0 device with L1 proto.
 *
 */
int
diag_l1_open(struct diag_l0_device *dl0d, int l1protocol) {
	DIAG_DBGM(diag_l1_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "diag_l1_open(0x%p, l1 proto=%d\n",
		FL, (void *)dl0d, l1protocol);

	/* Check h/w supports this l1 protocol */
	if ((dl0d->dl0->l1proto_mask & l1protocol) == 0) {
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

	/* Call the open routine with the requested L1 protocol */
	return diag_l0_open(dl0d, l1protocol);
}

//diag_l1_close : call the ->close member of the
//specified diag_l0_device.
void
diag_l1_close(struct diag_l0_device *dl0d) {
	DIAG_DBGM(diag_l1_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		FLFMT "entering diag_l1_close: dl0d=%p\n", FL, (void *)dl0d);

	if (dl0d) {
		diag_l0_close(dl0d);
	}
	return;
}

int diag_l1_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	/* At the moment, no ioctls are handled at the L1 level, so forward them to L0. */

	return diag_l0_ioctl(dl0d, cmd, data);
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
diag_l1_send(struct diag_l0_device *dl0d, const void *data, size_t len, unsigned int p4) {
	int rv = DIAG_ERR_GENERAL;
	uint32_t l0flags;
	uint8_t duplexbuf[MAXRBUF];

	if (len > MAXRBUF) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	l0flags = diag_l1_getflags(dl0d);

	DIAG_DBGMDATA(diag_l1_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V, data, len,
		FLFMT "_send: len=%d P4=%u l0flags=0x%X; ",
		FL, (int) len, p4, l0flags);

	/*
	 * If p4 is zero and not in half duplex mode, or if
	 * L1 is a "DOESL2" interface, or if L0 takes care of P4 waits,
	 * or if P4==0 and we do per-message duplex removal:
	 * send the whole message to L0 as one write
	 */

	if (   ((p4 == 0) && ((l0flags & DIAG_L1_HALFDUPLEX) == 0)) ||
		(l0flags & DIAG_L1_DOESL2FRAME) || (l0flags & DIAG_L1_DOESP4WAIT) ||
		((p4==0) && (l0flags & DIAG_L1_BLOCKDUPLEX)) ) {
		/*
		 * Send the lot
		 */
		rv = diag_l0_send(dl0d, data, len);

		//optionally remove echos
		if ((l0flags & DIAG_L1_BLOCKDUPLEX) && (rv==0)) {
			//try to read the same number of sent bytes; timeout=300ms + 1ms/byte
			//This is plenty OK for typical 10.4kbps but should be changed
			//if ever slow speeds are used.
			if (diag_l0_recv(dl0d, duplexbuf, len, 300+len) != (int) len) {
				rv=DIAG_ERR_GENERAL;
			}

			//compare to sent bytes
			if ( memcmp(duplexbuf, data, len) !=0) {
				fprintf(stderr,FLFMT "Bus Error: bad half duplex echo!\n", FL);
				rv=DIAG_ERR_BUSERROR;
			}
		}
	} else {
		/* else: send each byte */
		const uint8_t *dp = (const uint8_t *)data;

		while (len--) {
			rv = diag_l0_send(dl0d, dp, 1);
			if (rv != 0) {
				break;
			}

			/*
			 * If half duplex, read back the echo, if
			 * the echo is wrong then this is an error
			 * i.e something wrote on the diag bus whilst
			 * we were writing
			 */
			if (l0flags & DIAG_L1_HALFDUPLEX) {
				uint8_t c;

				c = *dp - 1; /* set it with wrong val. */
				if (diag_l0_recv(dl0d, &c, 1, 200) != 1) {
					rv=DIAG_ERR_GENERAL;
					break;
				}

				if (c != *dp) {
					if (c == *dp - 1) {
						fprintf(stderr,"Half duplex interface not echoing!\n");
					} else {
						fprintf(stderr,
							"Bus Error: got 0x%X "
							"expected 0x%X\n",
							c, *dp);
					}
					rv = DIAG_ERR_BUSERROR;
					break;
				}
			}
			dp++;

			if (p4) { /* Inter byte gap */
				diag_os_millisleep(p4);
			}
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
diag_l1_recv(struct diag_l0_device *dl0d, void *data, size_t len, unsigned int timeout) {
	int rv;
	if (!len) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	DIAG_DBGM(diag_l1_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "_recv request len=%d, timeout=%u;",
		FL, (int)len, timeout);

	if (timeout == 0) {
		fprintf(stderr,
			FLFMT
			"Interesting : L1 read with timeout=0. Report this !\n",
			FL);
	}

	rv=diag_l0_recv(dl0d, data, len, timeout);
	if (!rv) {
		fprintf(stderr, FLFMT "L0 returns with 0 bytes; returning TIMEOUT instead. Report this !\n", FL);
		return DIAG_ERR_TIMEOUT;
	}

	if (rv>0) {
		DIAG_DBGMDATA(diag_l1_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
			data, (size_t) rv, "got %d bytes; ",rv);
	}

	return rv;
}


//diag_l1_getflags: returns l0 flags
uint32_t diag_l1_getflags(struct diag_l0_device *dl0d) {
	return diag_l0_getflags(dl0d);
}

//diag_l1_gettype: returns l1proto_mask :supported L1 protos
//of the l0 driver
int diag_l1_gettype(struct diag_l0_device *dl0d) {
	return dl0d->dl0->l1proto_mask;
}

