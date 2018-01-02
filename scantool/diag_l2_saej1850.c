/*
 *
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
 * L2 driver for SAEJ1850
 *
 *
 * INCOMPLETE, will not work, but doesnt coredump. This has been checked in
 * because scantool.c was checked in for other reasons, and needs this so it
 * doesnt coredump ...
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_saej1850.h" /* prototypes for this file */

/*
 * SAEJ1850 specific data
 */
struct diag_l2_j1850 {
	uint8_t type; /* FAST/SLOW/CARB */

	uint8_t srcaddr; /* Src address used */
	uint8_t dstaddr; /* Dest address used */
	// uint16_t modeflags;	/* Flags XXXunused ! */

	uint8_t state;
	uint8_t rxbuf[MAXRBUF]; /* Receive buffer, for building message in */
	int rxoffset;           /* Offset to write into buffer */
};

#define STATE_CLOSED 0      /* Established comms */
#define STATE_CONNECTING 1  /* Connecting */
#define STATE_ESTABLISHED 2 /* Established */

/* Prototypes */
uint8_t dl2p_j1850_crc(uint8_t *msg_buf, int nbytes);

/* External interface */

/*
 * The complex initialisation routine for SAEJ1850
 */

static int
dl2p_j1850_startcomms(struct diag_l2_conn *d_l2_conn, UNUSED(flag_type flags),
		      UNUSED(unsigned int bitrate), target_type target,
		      source_type source) {
	struct diag_l2_j1850 *dp;
	int rv;

	if (diag_l2_debug_load() & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "diag_l2_j1850_startcomms dl2conn %p\n", FL,
			(void *)d_l2_conn);
	}

	rv = diag_calloc(&dp, 1);
	if (rv != 0) {
		return diag_iseterr(rv);
	}

	d_l2_conn->diag_l2_proto_data = (void *)dp;

	dp->srcaddr = source;
	dp->dstaddr = target;

	dp->state = STATE_CONNECTING;

	/* Empty our Receive buffer and wait for idle bus */
	/* XXX is the timeout value right ? It is 300 in other places. */

	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(50);

	/* Always OK */
	return 0;
}

/*
 */
static int
dl2p_j1850_stopcomms(struct diag_l2_conn *d_l2_conn) {
	struct diag_l2_j1850 *dp;

	dp = (struct diag_l2_j1850 *)d_l2_conn->diag_l2_proto_data;

	if (dp) {
		free(dp);
	}
	d_l2_conn->diag_l2_proto_data = NULL;

	/* Always OK for now */
	return 0;
}

/* Thanks to B. Roadman's web site for this CRC code */
uint8_t
dl2p_j1850_crc(uint8_t *msg_buf, int nbytes) {
	uint8_t crc_reg = 0xff, poly, i, j;
	uint8_t *byte_point;
	uint8_t bit_point;

	for (i = 0, byte_point = msg_buf; i < nbytes; ++i, ++byte_point) {
		for (j = 0, bit_point = 0x80; j < 8; ++j, bit_point >>= 1) {
			if (bit_point & *byte_point) { // case for new bit = 1
				if (crc_reg & 0x80) {
					poly = 1; // define the polynomial
				} else {
					poly = 0x1c;
				}
				crc_reg = ((crc_reg << 1) | 1) ^ poly;
			} else { // case for new bit = 0
				poly = 0;
				if (crc_reg & 0x80) {
					poly = 0x1d;
				}
				crc_reg = (crc_reg << 1) ^ poly;
			}
		}
	}
	return ~crc_reg; // Return CRC
}

/*
 * Just send the data
 *
 * We add the header and checksums here as appropriate
 * ret 0 if ok
 */
static int
dl2p_j1850_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int l1flags, rv, l1protocol;
	struct diag_l2_j1850 *dp;

	uint8_t buf[MAXRBUF];
	int offset = 0;

	if (diag_l2_debug_load() & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "diag_l2_j1850_send %p msg %p len %d called\n", FL,
			(void *)d_l2_conn, (void *)msg, msg->len);
	}

	if ((msg->len + 4) >= MAXRBUF) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	dp = (struct diag_l2_j1850 *)d_l2_conn->diag_l2_proto_data;
	l1flags = d_l2_conn->diag_link->l1flags;
	l1protocol = d_l2_conn->diag_link->l1proto;

	if (l1flags & DIAG_L1_DATAONLY) {
		// no headers to add, just the message
		// nop
	} else {
		// Add the J1850 header to the data

		if (l1protocol == DIAG_L1_J1850_PWM) {
			buf[0] = 0x61;
		} else {
			buf[0] = 0x68;
		}
		buf[1] = dp->dstaddr;
		buf[2] = dp->srcaddr;
		offset += 3;
	}

	// Now copy in data
	memcpy(&buf[offset], msg->data, msg->len);
	offset += msg->len;

	if (((l1flags & DIAG_L1_DOESL2CKSUM) == 0) &&
	    ((l1flags & DIAG_L1_DATAONLY) == 0)) {
		// Add in J1850 CRC
		int curoff = offset;
		buf[offset++] = dl2p_j1850_crc(buf, curoff);
	}

	if (diag_l2_debug_load() & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "diag_l2_j1850_send sending %d bytes to L1\n", FL,
			offset);
	}

	// And send data to Layer 1
	rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, 0, buf, (size_t)offset, 0);

	return rv ? diag_iseterr(rv) : 0;
}

/*
 * Protocol receive routine
 *
 * Receive all messages until timeout has elapsed, split + save on d_l2_conn->diag_msg
 * This is implemented differently from the ISO L2s (9141 and 14230), in that
 * timeout is measured starting at this function's entry.
 *
 * Ret 0 if ok, whether or not there were any messages.
 */
static int
dl2p_j1850_int_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout) {
	int rv;
	struct diag_l2_j1850 *dp;
	unsigned long long t_done; // time elapsed
	unsigned long long t_us;   // total timeout, in us
	unsigned long long t0;     // start time

	int l1flags = d_l2_conn->diag_link->l1flags;

	t0 = diag_os_gethrt();

	dp = (struct diag_l2_j1850 *)d_l2_conn->diag_l2_proto_data;
	diag_freemsg(d_l2_conn->diag_msg);

	if (diag_l2_debug_load() & DIAG_DEBUG_READ) {
		fprintf(stderr,
			FLFMT
			"diag_l2_j1850_int_recv offset 0x%X, "
			"timeout=%u\n",
			FL, dp->rxoffset, timeout);
	}

	// No support for non framing L2 interfaces yet ...
	if (!(l1flags & DIAG_L1_DOESL2FRAME)) {
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

	/* Extend timeouts since L0/L1 does framing */
	timeout += SMART_TIMEOUT;
	t_us = timeout * 1000ULL;
	t_done = 0;

	dp->rxoffset = 0;

	/* note : some of this is not necessary since we assume every L0/L1 does J1850
	 * framing properly. */
	while (t_done < t_us) {
		// loop while there's time left
		unsigned long tout;
		struct diag_msg *tmsg;
		unsigned datalen;

		tout = timeout - (t_done / 1000);

		// Unofficially, smart L0s (like ME,SIM) return max 1 response per call to
		// l1_recv()
		rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, NULL,
				  &dp->rxbuf[dp->rxoffset],
				  sizeof(dp->rxbuf) - dp->rxoffset, tout);

		if (rv == DIAG_ERR_TIMEOUT) {
			break;
		}

		if (rv < 0) {
			// Other errors are more serious.
			diag_freemsg(d_l2_conn->diag_msg);
			return rv;
		}
		dp->rxoffset += rv;

		// update elapsed time
		t_done = diag_os_hrtus(diag_os_gethrt() - t0);
		if (rv == 0) {
			continue; // no data ?
		}

		datalen = dp->rxoffset;

		// get data payload length
		if (!(l1flags & DIAG_L1_NOHDRS)) {
			// headers present
			if (datalen <= 3) {
				continue;
			}
			datalen -= 3;
		}
		if (!(l1flags & DIAG_L1_STRIPSL2CKSUM)) {
			// CRC present
			if (datalen <= 1) {
				continue;
			}
			datalen -= 1;
		}

		// alloc msg and analyze
		tmsg = diag_allocmsg(datalen);
		if (tmsg == NULL) {
			diag_freemsg(d_l2_conn->diag_msg);
			return diag_iseterr(DIAG_ERR_NOMEM);
		}

		if (!(l1flags & DIAG_L1_NOHDRS)) {
			// get header content & trim

			tmsg->dest = dp->rxbuf[1];
			tmsg->src = dp->rxbuf[2];
			// and copy, skipping header bytes.
			memcpy(tmsg->data, &dp->rxbuf[3], datalen);
		} else {
			memcpy(tmsg->data, dp->rxbuf, datalen);
		}

		if (!(l1flags & DIAG_L1_STRIPSL2CKSUM)) {
			// test & trim checksum
			uint8_t tcrc = dl2p_j1850_crc(dp->rxbuf, dp->rxoffset - 1);
			if (dp->rxbuf[dp->rxoffset - 1] != tcrc) {
				fprintf(stderr,
					"Bad checksum detected: needed %02X got %02X\n",
					tcrc, dp->rxbuf[dp->rxoffset - 1]);
				tmsg->fmt |= DIAG_FMT_BADCS;
			}
		}

		tmsg->fmt |= DIAG_FMT_CKSUMMED; // either L1 did it or we just did
		tmsg->fmt |= DIAG_FMT_FRAMED;

		tmsg->rxtime = diag_os_getms();
		dp->rxoffset = 0;

		diag_l2_addmsg(d_l2_conn, tmsg);

	} // while !timed out

	dp->state = STATE_ESTABLISHED;
	return 0;
}

static int
dl2p_j1850_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
		void (*callback)(void *handle, struct diag_msg *msg), void *handle) {
	int rv;
	struct diag_msg *tmsg;

	rv = dl2p_j1850_int_recv(d_l2_conn, timeout);

	if (rv < 0) {
		/* Failed, or timed out */
		return rv;
	}

	if (d_l2_conn->diag_msg == NULL) {
		return DIAG_ERR_TIMEOUT;
	}

	/*
	 * We now have data stored on the L2 descriptor
	 */
	if (diag_l2_debug_load() & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "calling rcv msg=%p callback, handle=%p\n", FL,
			(void *)d_l2_conn->diag_msg,
			handle); //%pcallback! we won't try to printf the
				 // callback pointer.
	}

	tmsg = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;

	/* Call used callback */
	if (callback) {
		callback(handle, tmsg);
	}

	/* message no longer needed */
	diag_freemsg(tmsg);

	if (diag_l2_debug_load() & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "rcv callback completed\n", FL);
	}

	return 0;
}

/*
 * Send a request and wait for a response
 */
static struct diag_msg *
dl2p_j1850_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval) {
	int rv;
	struct diag_msg *rmsg = NULL;

	/* First send the message */
	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/* And now wait for a response */
	/* XXX, whats the correct timeout for this ??? */
	rv = dl2p_j1850_int_recv(d_l2_conn, 250);
	if (rv < 0) {
		*errval = rv;
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/* any responses ? */
	if (!d_l2_conn->diag_msg) {
		*errval = DIAG_ERR_TIMEOUT;
		return NULL;
	}

	/* Return the message to user, who is responsible for freeing it */
	if (!d_l2_conn->diag_msg) {
		// no response, but no error either
		*errval = DIAG_ERR_TIMEOUT;
	}
	rmsg = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;
	return rmsg;
}

const struct diag_l2_proto diag_l2_proto_saej1850 = {DIAG_L2_PROT_SAEJ1850,
						     "SAEJ1850",
						     DIAG_L2_FLAG_FRAMED |
							     DIAG_L2_FLAG_CONNECTS_ALWAYS,
						     dl2p_j1850_startcomms,
						     dl2p_j1850_stopcomms,
						     dl2p_j1850_send,
						     dl2p_j1850_recv,
						     dl2p_j1850_request,
						     NULL};
