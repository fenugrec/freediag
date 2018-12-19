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
 * L2 driver for Mercedes Benz protocol used on things like the
 * EGS (auto gearbox controller) on 1999/2000/2001 cars
 * I have called this Mercedes Benz protocol 1, since all other control
 * units I have played with use ISO14230
 * TODO :
 */


#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_mb1.h"  /* prototypes for this file */


static int
dl2p_mb1_int_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	uint8_t *data, int len);


static int
dl2p_mb1_startcomms( struct diag_l2_conn *d_l2_conn,
UNUSED(flag_type flags),
unsigned int bitrate,
target_type target,
UNUSED(source_type source)) {
	struct diag_l1_initbus_args in;
	uint8_t cbuf[2];
	int rv;
	unsigned int baud;
	uint8_t rxbuf[MAXRBUF];
	struct diag_serial_settings set;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "startcomms conn %p\n", FL, (void *)d_l2_conn);

	/*
	 * If 0 has been specified, use a suitable default
	 */
	if (bitrate == 0) {
		baud = 9600;
	} else {
		baud = bitrate;
	}
	d_l2_conn->diag_l2_speed = baud;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, (void *) &set);
	if (rv < 0) {
		return diag_ifwderr(rv);
	}

	/* Flush unread input, then wait for idle bus. */
	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(300);

	/*
	 * Do 5Baud init
	 */
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = target;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0) {
		return diag_ifwderr(rv);
	}

	/*
	 * L0 code should have set correct baud rate now etc
	 * Now read keybytes, ignoring parity
	 */
	rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0,
		cbuf, 1, 100);
	if (rv < 0) {
		return diag_ifwderr(rv);
	}
	rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0,
		&cbuf[1], 1, 100);
	if (rv < 0) {
		return diag_ifwderr(rv);
	}

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "startcomms conn %p got kb 0x%X 0x%X\n",
		FL, (void *)d_l2_conn, cbuf[0], cbuf[1]);

	/*
	 * Check the received keybytes
	 */
	d_l2_conn->diag_l2_kb1 = cbuf[0];
	d_l2_conn->diag_l2_kb2 = cbuf[1];

	if (cbuf[0] != 0xC2) {
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}
	if (cbuf[1] != 0xCD) {
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}

	/*
	 * Set the P3max (idle timer) to 1 second
	 */
	d_l2_conn->diag_l2_p3max = 1000;

	/*
	 * Now we probably get a message back that we don't want
	 * particularly that tells us the ecu part num, h/w and s/w versions
	 */
	(void) dl2p_mb1_int_recv(d_l2_conn, 1000, rxbuf, sizeof(rxbuf));

	return diag_iseterr(rv);
}

static int
dl2p_mb1_stopcomms(UNUSED(struct diag_l2_conn *dl2c)) {
	return 0;
}

/*
 * Decode the message header, and check the message is complete
 * and that the checksum is correct.
 *
 * Once we know the actual msglen, *msglen is filled in, else it is set to 0
 *
 * Data/len is received data/len
 */
static int
dl2p_mb1_decode(uint8_t *data, int len, int *msglen) {
	uint16_t cksum;
	int i;

	DIAG_DBGMDATA(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V, data, len,
		FLFMT "decode len %d; ", FL, len);

	*msglen = 0;

	if (len < 3) {
		return diag_iseterr(DIAG_ERR_INCDATA);
	}

	if (data[2] > len) {
		return diag_iseterr(DIAG_ERR_INCDATA);
	}

	*msglen = data[3];

	for (i = 0, cksum = 0; i < len - 2; i++) {
		cksum += data[i];
	}
	if (data[len-2] != (cksum &0xff)) {
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
			FLFMT "recv cksum 0x%02X 0x%02X, wanted 0x%X\n",
			FL, data[len - 1] & 0xff, data[len - 2] & 0xff,
			cksum & 0xffff);
		return diag_iseterr(DIAG_ERR_BADCSUM);
	}
	if (data[len-1] != ((cksum>>8) & 0xff)) {
		DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
			FLFMT "recv cksum 0x%02X 0x%02X, wanted 0x%X\n",
			FL, data[len - 1] & 0xff, data[len - 2] & 0xff,
			cksum & 0xffff);
		return diag_iseterr(DIAG_ERR_BADCSUM);
	}
	return 0;
}

/*
 * Internal receive, reads a whole message from the ECU -
 * returns the data length of the packet received
 */
static int
dl2p_mb1_int_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	uint8_t *data, int len) {
	int rxoffset, rv;
	unsigned int tout;
	int msglen;
	size_t readlen;

	rxoffset = 0;
	tout = timeout;
	msglen = 0;
	readlen = 3;
	while ( (rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0,
			&data[rxoffset], readlen, tout)) > 0) {
		rxoffset += rv;
		tout = 100;

		/* Got some data */

		rv = dl2p_mb1_decode(data, rxoffset, &msglen);

		if (rv >= 0) {
			/* Full packet ! */
			break;
		}
		if (rv != DIAG_ERR_INCDATA) {
			/* Bad things happened */
			rxoffset = rv;
			break;
		}

		/* Not full, read some more */

		if (msglen) {
			readlen = msglen - rxoffset;
		} else if (rxoffset < 3) {
			readlen = 3;
		} else {
			readlen = len - rxoffset;
		}
	}
	return rxoffset;
}


/*
 * Read data, attempts to get complete set of responses
 *
 */
static int
dl2p_mb1_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle) {
	uint8_t	rxbuf[MAXRBUF];
	int rv;
	struct diag_msg *msg;

	rv = dl2p_mb1_int_recv(d_l2_conn, timeout, rxbuf,
		sizeof(rxbuf));

	if (rv < 0 || rv > (255 + 4)) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "recv conn %p got %d byte message\n",
		FL, (void *)d_l2_conn, rv);

	if (rv < 5) {
		/* Bad, minimum message is 5 bytes */
		return diag_iseterr(DIAG_ERR_BADDATA);
	}

	/*
	 * Ok, alloc a message
	 */
	msg = diag_allocmsg((size_t)(rv - 4));
	if (msg == NULL) {
		return diag_iseterr(DIAG_ERR_NOMEM);
	}
	msg->data[0] = rxbuf[1];		/* Command */
	memcpy(&msg->data[1], &rxbuf[3], (size_t)(rv - 3));	/* Data */
	msg->rxtime = diag_os_getms();
	msg->fmt = DIAG_FMT_FRAMED ;

	/*
	 * Call user callback routine
	 */
	if (callback) {
		callback(handle, msg);
	}

	/* No longer needed */
	diag_freemsg(msg);

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "recv() callback completed\n", FL);

	return 0;
}

/*
 * Send the data, using MB1 protocol
 * ret 0 if ok
 */
static int
dl2p_mb1_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv;
	unsigned int sleeptime;
	uint8_t txbuf[MAXRBUF];
	uint16_t cksum;
	unsigned i;

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		FLFMT "diag_l2_send %p, msg %p called\n",
		FL, (void *)d_l2_conn, (void *)msg);

	/*
	 * Make sure enough time between last receive and this send
	 * In fact, because of the timeout on recv(), this is pretty small, but
	 * we take the safe road and wait the whole of p3min plus whatever
	 * delay happened before
	 */
	sleeptime = d_l2_conn->diag_l2_p3min;
	if (sleeptime > 0) {
		diag_os_millisleep(sleeptime);
	}

	txbuf[0] = d_l2_conn->diag_l2_destaddr;
	txbuf[1] = msg->data[0]; 	/* Command */
	txbuf[2] = msg->len + 4;	/* Data + Hdr + 2 byte checksum */
	memcpy(&txbuf[3], &msg->data[1], (size_t)(msg->len-1));

	/* Checksum is 16 bit addition, in LSB order on packet */
	for (i = 0, cksum = 0; i < (msg->len + 2); i++) {
		cksum += txbuf[i];
	}

	txbuf[msg->len+2] = (uint8_t) (cksum & 0xff);
	txbuf[msg->len+3] = (uint8_t) ((cksum>>8) & 0xff);

	DIAG_DBGMDATA(diag_l2_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		txbuf, txbuf[2],
		FLFMT "send %d bytes; ", FL, txbuf[2]);

	rv = diag_l1_send (d_l2_conn->diag_link->l2_dl0d, 0,
		txbuf, txbuf[2], d_l2_conn->diag_l2_p4min);


	return rv? diag_ifwderr(rv):0 ;
}

static struct diag_msg *
dl2p_mb1_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval) {
	int rv;
	struct diag_msg *rmsg = NULL;
	uint8_t	rxbuf[MAXRBUF];

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return diag_pseterr(DIAG_ERR_GENERAL);
	}
	/* And wait for response for 1 second */

	rv = dl2p_mb1_int_recv(d_l2_conn, 1000, rxbuf,
		sizeof(rxbuf));

	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "msg receive conn %p got %d byte message\n", FL,
		(void *)d_l2_conn, rv);

	if (rv < 5 || rv > (255+4)) {
		/* Bad, minimum message is 5 bytes, or error happened */
		if (rv < 0) {
			*errval = rv;
		} else {
			*errval = DIAG_ERR_BADDATA;
		}
	} else {
		/*
		 * Ok, alloc a message
		 */
		rmsg = diag_allocmsg((size_t)(rv - 4));
		if (rmsg == NULL) {
			return diag_pseterr(DIAG_ERR_NOMEM);
		}
		rmsg->data[0] = rxbuf[1];		/* Command */
		memcpy(&rmsg->data[1], &rxbuf[3], (size_t)(rv - 3));	/* Data */
		rmsg->rxtime = diag_os_getms();
		rmsg->fmt = DIAG_FMT_FRAMED;
	}
	return rmsg;
}


/*
 * Timeout, called to send idle packet to keep link to ECU alive
 */
static void
dl2p_mb1_timeout(struct diag_l2_conn *d_l2_conn) {
	struct diag_msg msg = {0};
	uint8_t txbuf[8];
	uint8_t rxbuf[1000];
	int rv;

	/* XXX Not async-signal-safe */
	DIAG_DBGM(diag_l2_debug, DIAG_DEBUG_TIMER, DIAG_DBGLEVEL_V,
		FLFMT "timeout conn %p\n", FL, (void *)d_l2_conn);

	txbuf[0] = 0x50;	/* Idle command */
	txbuf[1] = 0x01;
	msg.data = txbuf;
	msg.len = 2;

	/* Use l2_send as it updates timeout timers */
	rv =  diag_l2_send(d_l2_conn, &msg);

	/* And receive/ignore the response */
	if (rv >= 0) {
		(void)dl2p_mb1_int_recv(d_l2_conn, 1000, rxbuf, sizeof(rxbuf));
	}
	return;
}

const struct diag_l2_proto diag_l2_proto_mb1 = {
	DIAG_L2_PROT_MB1,
	"MB1",
	DIAG_L2_FLAG_FRAMED | DIAG_L2_FLAG_KEEPALIVE,
	dl2p_mb1_startcomms,
	dl2p_mb1_stopcomms,
	dl2p_mb1_send,
	dl2p_mb1_recv,
	dl2p_mb1_request,
	dl2p_mb1_timeout
};
