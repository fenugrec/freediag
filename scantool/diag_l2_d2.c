/*
 *	freediag - Vehicle Diagnostic Utility
 *
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
 * L2 driver for Volvo D2 protocol over K-line (keyword D3 B0)
 *
 * This protocol is used by the engine and chassis ECUs for extended
 * diagnostics on the 1996-1998 Volvo 850, S40, C70, S70, V70, XC70, V90 and
 * possibly other models.
 *
 * The message headers are similar, but not identical, to KWP2000.
 * In KWP2000, the length value in the header represents the number of
 * data bytes only in the message; here, it also includes the trailing
 * checksum byte -- that is, the length value is 1 greater than it would be
 * in KWP2000.
 *
 * See diag_l7_d2 for the corresponding application protocol.
 *
 * This driver currently works only with ELM327 interfaces.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "diag_tty.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l2_d2.h"

/* replace a byte's msb with a parity bit */
static uint8_t with_parity(uint8_t c, enum diag_parity eo) {
	uint8_t p;
	int i;

	p = 0;
	if (eo == diag_par_o) {
		p = 1;
	}

	for (i = 0; i < 7; i++) {
		p ^= c; p <<= 1;
	}

	return((c&0x7f)|(p&0x80));
}

static int dl2p_d2_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv;
	uint8_t buf[3 + 62 + 1];
	struct diag_l2_d2 *dp;

	dp = (struct diag_l2_d2 *)d_l2_conn->diag_l2_proto_data;

	if (msg->len < 1 || msg->len > 62) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	buf[0] = 0x80 + msg->len + 1;
	buf[1] = msg->dest ? msg->dest : dp->dstaddr;
	buf[2] = msg->src ? msg->src : dp->srcaddr;

	memcpy(&buf[3], msg->data, msg->len);

	diag_os_millisleep(d_l2_conn->diag_l2_p3min);

	rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, buf,
	                  msg->len + 3, d_l2_conn->diag_l2_p4min);

	return rv?diag_ifwderr(rv):0;
}

static int dl2p_d2_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
                        void (*callback)(void *handle, struct diag_msg *msg),
                        void *handle) {
	int rv;
	int i;

	uint8_t tmpdlyproto[7] = {0x84, 0x13, 0x99, 0x7E, 0x99, 0x23, 0x99};

	uint8_t buf[14 + 3 + 62 + 1 + 5];	// 2017-11-23  jonesrh	Longest D2 w/ 3-byte header and CS *is* 66,
						//			  but longest D2 w/ 4-byte header is **71** (ie, ECU 29 F9F2 response to B9F2).
						//			Suggest changing to "uint8_t buf[3 + 62 + 1 + 5 /* extra for longest 4-byte header */];"
						//			  or, "uint8_t buf[3 + 62 + 1 + 5 /* extra to account for longest seen D2 msg */];"
						//			  or, more properly, "uint8_t buf[4 + 66 + 1];".
						//			The pre-2017-11 coding -- ie, without the " + 5" -- was correct for 
						//			  all presently known '96-'98 Volvo 850/SVC70 D2 ECUs, except for ECU 29 (ECC).
						//			This 2017-11-23 change will hopefully avoid overwrite problems if
						//			  "readnv 0xF2" (or "read 0xE9") is issued for '98 S70/V70 ECC (sometime in the future).
						// 2017-12-02  jonesrh  Increase by prefixing of up to two seven (7) byte "7E xx 23" responses,
						//			  as is sometimes seen for B9F0 responses.
	struct diag_msg *msg;

	do {
		buf[0] = 0x00;
		rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, buf, sizeof(buf), timeout + 100);
		if (rv < 0)
			return rv;

		if (rv < 5)
			return diag_iseterr(DIAG_ERR_INCDATA);

		// If the received buffer contains (at the beginning) 1 or more "7E xx 23" responses that are simply "temporary delay" responses,
		// then "eat them", do not report them here (though they may have been already logged if debug flags were enabled).
		// - The reason this "eating of '7E xx 23' responses" part of the "try to use valid D2 concatenated messages suffixed with <DATA ERROR" hack"
		//   is done in this dl2p_d2_recv() function instead of the dl2p_d2_request() function is:
		//   * dl2p_d2_recv will chop off the initial 3 byte header and trailing checksum when it reports the buffer to its caller (dl2p_d2_request), and
		//   * that action corrupts the checks for valid "7E xx 23" responses and 
		//     it corrupts the checks for the valid response to the original request.
		// - So we'll have to settle for this approach.
		//   * But it limits the validity checking that we can perform.
		// - Hopefully, these 2017-12-02 changes to dl2p_d2_recv will
		//   compensate for all the common "7E xx 23" responses that
		//   the '97-'98 S70/V70/C70/XC70 experience, and it might even
		//   compensate for the multiple "7E xx 23" that are seen
		//   during clearing of some ECU's DTCs -- for both the
		//   '96-'97 850 and the '97-'98 S70/V70/C70/XC70.
		tmpdlyproto[2] = buf[2];  // Ideally this needs to be loaded from the dest of the request.
		tmpdlyproto[4] = buf[4];  // Ideally this needs to be loaded from the function of the request (ie, the 1st byte after the header).
		tmpdlyproto[6] = diag_cks1(tmpdlyproto,6);
		for (i=0; rv-i >= 7; i += 7) {
			if (memcmp( tmpdlyproto, buf+i, 7) != 0)
				break;
		}
		// Done detecting an initial "7E xx 23" response (and any consecutive duplicates of that same response).
	} while (rv-i == 0);	// When rv-i = 0, only 1 (or more) "7E xx 23" responses detected in buffer, so just retry the diag_l1_recv.

	if (rv-i < 5)
		return diag_iseterr(DIAG_ERR_INCDATA);

	if (diag_cks1( &buf[i], (size_t)(rv-i-1)) != buf[rv-1])
		return diag_iseterr(DIAG_ERR_BADCSUM);	// On 2017-12-02, added this explicit checksum validation, 
							// since the lines containing "<DATA ERROR" responses no longer trigger
							// L0 to discard such a D2 line.
							// - Most "<DATA ERROR" responses from ELM327 are *NOT* true data errors,
							//   but instead are concatenations of request and response (if viewing an ATMA recording), or
							//   concatenation of multiple responses.
							// - It is exceedingly rare to see a real correctly calculated checksum that indicates a true data error.
							// - And it is fairly rare to see true data errors.
							// - The most common type of data error is deleting or repetition of characters
							//   when communicating with ELM327 Bluetooth devices, and those types of errors
							//   are most easily caught by seeing hex digit pairs in responses that are not separated by a single space.
							// On 2017-12-10, jonesrh added this additional comment:
							// - Probably something needs to be done about DIAG_FMT_CKSUMMED and DIAG_FMT_BADCS,
							//   but I have not analyzed them yet, and have not yet been forced to do something with them.
							//   I'll let Adam Goldman deal with the proper handling of those 2 flags
							//   to mimic how diag_l0_elm.c deals with the D2 "<DATA ERROR" and how
							//   this diag_l2_d2.c deals with performing the checksum and declaring the bad checksum error.

	msg = diag_allocmsg((size_t)(rv - i - 4));	// Questions from jonesrh: Is response header not checked for validity vs. the request header?
							//			   Should they at least be checked to see that the request's target address
							//			     and the response's sender address match exactly?
							//			   Since dl2p_d2_recv and dl2p_d2_request are static,
							//			     can their interface be changed to allow better checking of
							//			     request vs. response values as it relates to the "7E xx 23" 
							//			     and other concatenated responses?  If yes, then it would probably mean
							//			     moving these 2017-12-02 changes over to dl2p_d2_request
							//			     and beefing up the validity checking.
	if (msg == NULL) {
		return diag_iseterr(DIAG_ERR_NOMEM);
	}
	memcpy(msg->data, &buf[3], (size_t)(rv - 4));
	msg->rxtime = diag_os_getms();
	msg->src = buf[2];
	msg->dest = buf[1];
	msg->fmt = DIAG_FMT_FRAMED;

	if (callback) {
		callback(handle, msg);
	}

	diag_freemsg(msg);

	return 0;
}

static void dl2p_d2_request_callback(void *handle, struct diag_msg *in) {
	struct diag_msg **out = (struct diag_msg **)handle;
	*out = diag_dupsinglemsg(in);
}

static struct diag_msg *dl2p_d2_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
                                        int *errval) {
	int rv;
	struct diag_msg *rmsg = NULL;

	*errval = 0;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return NULL;
	}

	do {
		if (rmsg != NULL) {
			diag_freemsg(rmsg);
		}
		rv = dl2p_d2_recv(d_l2_conn, 1000, dl2p_d2_request_callback, &rmsg);
		if (rv < 0) {
			*errval = rv;
			return NULL;
		}
		if (rmsg == NULL) {
			*errval = DIAG_ERR_NOMEM;
			return NULL;
		}
		/* If we got routineNotCompleteOrServiceInProgress, loop until
		   the final response. */
	} while (rmsg->len==3 && rmsg->data[0]==0x7e && rmsg->data[1]==msg->data[0] && rmsg->data[2]==0x23);

	return rmsg;
}

static int dl2p_d2_startcomms(struct diag_l2_conn *d_l2_conn, flag_type flags,
                              unsigned int bitrate, target_type target, source_type source) {
	struct diag_serial_settings set;
	struct diag_l2_d2 *dp;
	int rv;
	struct diag_msg wm = {0};
	uint8_t wm_data[] = {0x82, 0, 0, 0xa1};
	struct diag_l1_initbus_args in;

	if (!(d_l2_conn->diag_link->l1flags & DIAG_L1_DOESFULLINIT) || !(d_l2_conn->diag_link->l1flags & DIAG_L1_DOESL2CKSUM)) {
		fprintf(stderr, "Can't do D2 over K-line on this L0 interface yet, sorry.\n");
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

	if ((flags & DIAG_L2_TYPE_INITMASK) != DIAG_L2_TYPE_SLOWINIT) {
		return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);
	}

	rv = diag_calloc(&dp, 1);
	if (rv != 0) {
		return diag_ifwderr(rv);
	}

	d_l2_conn->diag_l2_proto_data = (void *)dp;

	if (source != 0x13) {
		fprintf(stderr,
		        "Warning : Using tester address %02X. Some ECUs "
		        "require tester address to be 13.\n",
		        source);
	}

	dp->srcaddr = source;
	dp->dstaddr = target;

	if (bitrate == 0) {
		bitrate = 10400;
	}
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if ((rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED,
	                        (void *)&set))) {
		goto err;
	}

	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(300);

	wm_data[1] = dp->dstaddr;
	wm_data[2] = dp->srcaddr;
	wm.data = wm_data;
	wm.len = sizeof(wm_data);
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETWM, &wm);
	if (rv < 0) {
		goto err;
	}

	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = with_parity(target, diag_par_o);
	in.testerid = dp->srcaddr;
	in.kb1 = 0; in.kb2 = 0;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0) {
		goto err;
	}

	if (in.kb1 == 0 && in.kb2 == 0) {
		d_l2_conn->diag_l2_kb1 = 0xd3;
		d_l2_conn->diag_l2_kb2 = 0xb0;
		fprintf(stderr, FLFMT "_startcomms : L0 didn't return keybytes, continuing anyway\n", FL);
	} else {
		d_l2_conn->diag_l2_kb1 = in.kb1;
		d_l2_conn->diag_l2_kb2 = in.kb2;
	}

	if ((d_l2_conn->diag_l2_kb1 != 0xd3) || (d_l2_conn->diag_l2_kb2 != 0xb0)) {
		fprintf(stderr, FLFMT "_startcomms : wrong keybytes %02X%02X, expecting D3B0\n",
		        FL, d_l2_conn->diag_l2_kb1, d_l2_conn->diag_l2_kb2);
		rv = DIAG_ERR_WRONGKB;
		goto err;
	}

	return 0;

err:
	free(dp);
	d_l2_conn->diag_l2_proto_data = NULL;
	return diag_iseterr(rv);
}

static int dl2p_d2_stopcomms(struct diag_l2_conn *pX) {
	struct diag_msg msg = {0};
	uint8_t data[] = { 0xa0 };
	int errval = 0;
	static struct diag_msg *rxmsg;

	msg.len = 1;
	msg.dest = 0; msg.src = 0;      /* use default addresses */
	msg.data = data;

	rxmsg = dl2p_d2_request(pX, &msg, &errval);

	if (rxmsg == NULL || errval) {
		fprintf(stderr, "StopDiagnosticSession request failed, waiting for session to time out.\n");
		diag_os_millisleep(5000);
	} else {
		if (diag_l2_debug & DIAG_DEBUG_CLOSE) {
			fprintf(stderr, "waiting 3.8 sec...\n");
		}
		diag_os_millisleep(3800);	// On 2017-11-23, jonesrh added 3800 ms delay as per experience gained in large amount of testing
						// during volvo850diag development.
						// - This is very, very likely the change that eliminated undesired errors *after* successfully receiving the E0 response.
	}


	if (rxmsg != NULL) {
		diag_freemsg(rxmsg);
	}

	if (pX->diag_l2_proto_data) {
		free(pX->diag_l2_proto_data);
		pX->diag_l2_proto_data=NULL;
	}

	return 0;
}

static void dl2p_d2_timeout(struct diag_l2_conn *d_l2_conn) {
	struct diag_msg msg = {0};
	uint8_t data[] = { 0xa1 };
	int errval = 0;
	static struct diag_msg *rxmsg;

	msg.len = 1;
	msg.dest = 0; msg.src = 0;      /* use default addresses */
	msg.data = data;

	rxmsg = dl2p_d2_request(d_l2_conn, &msg, &errval);

	if (rxmsg != NULL) {
		diag_freemsg(rxmsg);
	}
}

const struct diag_l2_proto diag_l2_proto_d2 = {
	DIAG_L2_PROT_D2,
	"D2",
	DIAG_L2_FLAG_FRAMED | DIAG_L2_FLAG_KEEPALIVE,
	dl2p_d2_startcomms,
	dl2p_d2_stopcomms,
	dl2p_d2_send,
	dl2p_d2_recv,
	dl2p_d2_request,
	dl2p_d2_timeout
};
