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
#include <string.h>
#include "diag.h"
#include "diag_tty.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l2_d2.h"

/* replace a byte's msb with a parity bit */
static uint8_t
with_parity(uint8_t c, enum diag_parity eo) {
	uint8_t p;
	int i;

	p = 0;
	if (eo == diag_par_o) p = 1;

	for (i = 0; i < 7; i++) {
		p ^= c; p <<= 1;
	}

	return((c&0x7f)|(p&0x80));
}

static int
dl2p_d2_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv;
	uint8_t buf[3 + 62 + 1];
	struct diag_l2_d2 *dp;

	dp = (struct diag_l2_d2 *)d_l2_conn->diag_l2_proto_data;

	if (msg->len < 1 || msg->len > 62)
		return diag_iseterr(DIAG_ERR_BADLEN);

	buf[0] = 0x80 + msg->len + 1;
	buf[1] = msg->dest ? msg->dest : dp->dstaddr;
	buf[2] = msg->src ? msg->src : dp->srcaddr;

	memcpy(&buf[3], msg->data, msg->len);

	diag_os_millisleep(d_l2_conn->diag_l2_p3min);

	rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, NULL, buf,
		msg->len + 3, d_l2_conn->diag_l2_p4min);

	return rv?diag_iseterr(rv):0;
}

static int
dl2p_d2_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	void (*callback)(void *handle, struct diag_msg *msg),
	void *handle) {
	int rv;
	uint8_t buf[3 + 62 + 1];
	struct diag_msg *msg;

	rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, NULL, buf,
		sizeof(buf), timeout + 100);
	if (rv < 0)
		return rv;

	if (rv < 5)
		return diag_iseterr(DIAG_ERR_INCDATA);

	msg = diag_allocmsg((size_t)(rv - 4));
	if (msg == NULL)
		return diag_iseterr(DIAG_ERR_NOMEM);
	memcpy(msg->data, &buf[3], (size_t)(rv - 4));
	msg->rxtime = diag_os_getms();
	msg->src = buf[2];
	msg->dest = buf[1];
	msg->fmt = DIAG_FMT_FRAMED;

	if (callback)
		callback(handle, msg);

	diag_freemsg(msg);

	return 0;
}

static void
dl2p_d2_request_callback(void *handle, struct diag_msg *in) {
	struct diag_msg **out = (struct diag_msg **)handle;
	*out = diag_dupsinglemsg(in);
}

static struct diag_msg *
dl2p_d2_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
                int *errval) {
	int rv;
	struct diag_msg *rmsg;

	*errval = 0;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return NULL;
	}

	rv = dl2p_d2_recv(d_l2_conn, 1000, dl2p_d2_request_callback, &rmsg);
	if (rv < 0) {
		*errval = rv;
		return NULL;
	}
	if (rmsg == NULL)
		*errval = DIAG_ERR_NOMEM;

	return rmsg;
}

static int
dl2p_d2_startcomms(struct diag_l2_conn *d_l2_conn, flag_type flags,
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

	if ((flags & DIAG_L2_TYPE_INITMASK) != DIAG_L2_TYPE_SLOWINIT)
		return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);

	if (diag_calloc(&dp, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	d_l2_conn->diag_l2_proto_data = (void *)dp;

	if (source != 0x13)
		fprintf(stderr, "Warning : Using tester address %02X. Some ECUs require tester address to be 13.\n", source);

	dp->srcaddr = source;
	dp->dstaddr = target;

	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if ((rv=diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, (void *) &set)))
		goto err;

	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(300);

	wm_data[1] = dp->dstaddr;
	wm_data[2] = dp->srcaddr;
	wm.data = wm_data;
	wm.len = sizeof(wm_data);
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETWM, &wm);
	if (rv < 0)
		goto err;

	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = with_parity(target, diag_par_o);
	in.testerid = dp->srcaddr;
	in.kb1 = 0; in.kb2 = 0;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0)
		goto err;

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

static int
dl2p_d2_stopcomms(struct diag_l2_conn *pX) {
	struct diag_msg msg = {0};
	uint8_t data[] = { 0xa0 };
	int errval = 0;
	static struct diag_msg *rxmsg;

	msg.len = 1;
	msg.dest = 0; msg.src = 0;	/* use default addresses */
	msg.data = data;

	rxmsg = dl2p_d2_request(pX, &msg, &errval);

	if (rxmsg == NULL || errval) {
		fprintf(stderr, "StopDiagnosticSession request failed, waiting for session to time out.\n");
		diag_os_millisleep(5000);
	}

	if (rxmsg != NULL)
		diag_freemsg(rxmsg);

	if (pX->diag_l2_proto_data) {
		free(pX->diag_l2_proto_data);
		pX->diag_l2_proto_data=NULL;
	}

	return 0;
}

static void
dl2p_d2_timeout(struct diag_l2_conn *d_l2_conn) {
	struct diag_msg msg = {0};
	uint8_t data[] = { 0xa1 };
	int errval = 0;
	static struct diag_msg *rxmsg;

	msg.len = 1;
	msg.dest = 0; msg.src = 0;	/* use default addresses */
	msg.data = data;

	rxmsg = dl2p_d2_request(d_l2_conn, &msg, &errval);

	if (rxmsg != NULL)
		diag_freemsg(rxmsg);
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
