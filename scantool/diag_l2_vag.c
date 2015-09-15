/*
 * !!! INCOMPLETE !!!!
 *
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
 * Diag
 *
 * L2 driver for Volkswagen Audi Group protocol (Keyword 0x01 0x8a)
 *
 * !!! INCOMPLETE !!!!
 *
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l2_raw.h"
#include "diag_l2_iso9141.h"
#pragma message("Warning: diag_l2_iso9141.h is exposing a function so diag_l2_vag.c can use it ! FIX ME !")
#include "diag_vag.h"

#include "diag_l2_vag.h" /* prototypes for this file */


/*
 * ISO vag specific data
 */
struct diag_l2_vag
{
	uint8_t seq_nr;	/* Sequence number */
	uint8_t master;	/* Master flag, 1 = us, 0 = ECU */


	uint8_t rxbuf[MAXRBUF];	/* Receive buffer, for building message in */
	int rxoffset;		/* Offset to write into buffer */
};

#define STATE_CLOSED	  0	/* Established comms */
#define STATE_CONNECTING  1	/* Connecting */
#define STATE_ESTABLISHED 2	/* Established */


/*
 * Useful internal routines
 */

#if notyet
/*
 * Decode the message header XXX this seems copied from iso14230, and probably inadequate
 */
static int
diag_l2_proto_vag_decode(uint8_t *data, int len,
		 uint8_t *hdrlen, int *datalen, uint8_t *source, uint8_t *dest,
		int first_frame)
{
	int dl;

	if (diag_l2_debug & DIAG_DEBUG_PROTO)
	{
		int i;
		fprintf(stderr, FLFMT "decode len %d", FL, len);
		for (i = 0; i < len ; i++)
			fprintf(stderr, " 0x%X", data[i]&0xff);

		fprintf(stderr, "\n");
	}

	dl = data[0] & 0x3f;
	if (dl == 0)
	{
		/* Additional length field present */
		switch (data[0] & 0xC0)
		{
		case 0x80:
		case 0xC0:
			/* Addresses supplied, additional len byte */
			if (len < 4)
			{
				if (diag_l2_debug & DIAG_DEBUG_PROTO)
					fprintf(stderr, FLFMT "decode len short \n", FL);
				return diag_iseterr(DIAG_ERR_INCDATA);
			}
			*hdrlen = 4;
			*datalen = data[3];
			if (dest)
				*dest = data[1];
			if (source)
				*source = data[2];
			break;
		case 0x00:
			/* Addresses not supplied, additional len byte */
			if (first_frame)
				return diag_iseterr(DIAG_ERR_BADDATA);
			if (len < 2)
				return diag_iseterr(DIAG_ERR_INCDATA);
			*hdrlen = 2;
			*datalen = data[1];
			if (dest)
				*dest = 0;
			if (source)
				*source = 0;
			break;
		case 0X40:
			/* CARB MODE */
			return diag_iseterr(DIAG_ERR_BADDATA);
		}
	}
	else
	{
		/* Additional length field not present */
		switch (data[0] & 0xC0)
		{
		case 0x80:
		case 0xC0:
			/* Addresses supplied, NO additional len byte */
			if (len < 3)
				return diag_iseterr(DIAG_ERR_INCDATA);
			*hdrlen = 3;
			*datalen = dl;
			if (dest)
				*dest = data[1];
			if (source)
				*source = data[2];
			break;
		case 0x00:
			/* Addresses not supplied, No additional len byte */
			if (first_frame)
				return diag_iseterr(DIAG_ERR_BADDATA);
			*hdrlen = 1;
			*datalen = dl;
			if (dest)
				*dest = 0;
			if (source)
				*source = 0;
			break;
		case 0X40:
			/* CARB MODE */
			return diag_iseterr(DIAG_ERR_BADDATA);
		}
	}
	/*
	 * If len is silly [i.e 0] we've got this mid stream
	 */
	if (*datalen == 0)
		return diag_iseterr(DIAG_ERR_BADDATA);

	/*
	 * And confirm data is long enough, incl cksum
	 * If not, return saying data is incomplete so far
	 */

	if (len < (*hdrlen + *datalen + 1))
		return diag_iseterr(DIAG_ERR_INCDATA);

	return 0;
}
#endif

/* XXX copied from diag_l2_iso14230
 * Internal receive function (does all the message building, but doesn't
 * do call back, returns the complete message, hasn't removed checksum
 * and header info
 *
 * Data from the first message is put into *data, and len into *datalen
 *
 * If the L1 interface is clever (DOESL2FRAME), then each read will give
 * us a complete message, and we will wait a little bit longer than the normal
 * timeout to detect "end of all responses"
 */

static int
diag_l2_proto_vag_int_recv(struct diag_l2_conn *d_l2_conn,
UNUSED(int timeout),
UNUSED(uint8_t *data),
UNUSED(int *datalen))
{
	struct diag_l2_vag *dp;
	int rv = 0;
/*	struct diag_msg	*tmsg;*/

	dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_vag_intrecv offset %X\n",
				FL, dp->rxoffset);

	/* Clear out last received message if not done already */
	if (d_l2_conn->diag_msg)
	{
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	/*
	 * And receive the new message
	 */

#if notdef

	/*
	 * Now check the messages that we have checksum etc, stripping
	 * off headers etc
	 */
	if (rv >= 0)
	{
		tmsg = d_l2_conn->diag_msg;

		while (tmsg)
		{
			struct diag_l2_vag *dp;
			int datalen;
			uint8_t hdrlen, source, dest;

			/*
			 * We have the message with the header etc, we
			 * need to strip the header and checksum
			 */
			dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
			rv = diag_l2_proto_vag_decode( tmsg->data,
				tmsg->len,
				&hdrlen, &datalen, &source, &dest,
				dp->first_frame);

			if (rv < 0)		/* decode failure */
			{
				return rv;
			}

			if (diag_l2_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr, "msg %X decode done rv %d hdrlen %d datalen %d source %02X dest %02X\n",
					tmsg, rv, hdrlen, datalen, source, dest);

			if (tmsg->data[0] & 0xC0 == 0xC0)
			{
				tmsg->fmt = DIAG_FMT_ISO_FUNCADDR;
			} else {
				tmsg->fmt = 0;
			}

			tmsg->fmt |= DIAG_FMT_FRAMED;
			tmsg->fmt |= DIAG_FMT_CKSUMMED;

			tmsg->src = source;
			tmsg->dest = dest;
			tmsg->data += hdrlen;	/* Skip past header */
			tmsg->len -= (hdrlen + 1); /* And remove hdr/cksum */

			dp->first_frame = 0;

			tmsg = tmsg->next;
		}
	}
#endif
	return rv;
}


#if notyet
/*
 * Send a byte, and ensure we get the inverted ack back
 */
static int
diag_l2_proto_vag_send_byte(struct diag_l2_conn *d_l2_conn, databyte_type databyte)
{
	uint8_t rx_data = 0;
	int rv;

	/* Send the data byte */
	uint8_t db = (uint8_t)databyte;
	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		&db, 1, d_l2_conn->diag_l2_p4min);
	if (rv < 0)
		return rv;

//XXX	diag_l2_sendstamp(d_l2_conn); /* update the last sent timer */
//since this function (_send_byte) should only be used internally;
//and nobody is supposed to use diag_l2_vag by itself (always go through
//the diag_l2 public interface; this shouldn't be required.

	/* Receive the ack */

	if (rx_data != ((~databyte) & 0xFF) )
		return 0;		/* Wrong data */

	return 0;
}
#endif

#if notyet
/*
 * Send a telegram to the ECU
 *
 * Passed connection details, command and optional data
 */
static int
diag_l2_proto_vag_send_block(
struct diag_l2_conn *d_l2_conn,
	command_type cmd, uint8_t *data, uint8_t len)
{
	struct diag_l2_vag *dp;
	int i, blocklen;
	int rv;
	uint8_t databyte;

	dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;



	if (dp->master == 0)
	{
		/*
		 * We're not the master, need to receive a block
		 * to become the master
		 */
	}

	/* Send the length field, which is len + seq + command + data */
	blocklen = len + 3;
	databyte = blocklen;

	if ( (rv = diag_l2_proto_vag_send_byte(d_l2_conn, databyte)) < 0)
		return rv;


	/* Now send the sequence nr */
	if ( (rv = diag_l2_proto_vag_send_byte(d_l2_conn, dp->seq_nr)) < 0)
		return rv;

	/* Now send the command */
	if ( (rv = diag_l2_proto_vag_send_byte(d_l2_conn, cmd)) < 0)
		return rv;

	/* Now send the data */

	for (i=0; i < len; i++)
	{
		if ( (rv = diag_l2_proto_vag_send_byte(d_l2_conn, data[i])) < 0)
			return rv;
	}

	/* And send 0x03 as end of frame */



	/* And switch to show ECU is master */
	dp->master = 0;

	return 0;
}
#endif


/* External interface */

/*
 * The complex initialisation routine for ISOvag, which supports
 * 2 types of initialisation (5-BAUD, FAST) and functional
 * and physical addressing. The ISOvag spec describes CARB initialisation
 * which is done in the ISO9141 code
 */

static int
diag_l2_proto_vag_startcomms( struct diag_l2_conn *d_l2_conn,
UNUSED(flag_type flags),
unsigned int bitrate, target_type target, UNUSED(source_type source))
{
	struct diag_serial_settings set;
	struct diag_l2_vag *dp;
	uint8_t data[MAXRBUF];
	int rv;
/*	int wait_time;*/
/*	int hdrlen;*/
/*	int datalen;*/
/*	int datasrc;*/
	uint8_t cbuf[MAXRBUF];
/*	int len;*/

	struct diag_l1_initbus_args in;

	if (diag_calloc(&dp, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	d_l2_conn->diag_l2_proto_data = (void *)dp;


	memset(data, 0, sizeof(data));

	/*
	 * If 0 has been specified, use a useful default of 9600
	 */
	if (bitrate == 0)
		bitrate = 9600;
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, &set);
	if (rv < 0)
	{
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	/* Flush unread input, then wait for idle bus. */
	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(300);


	/* Now do 5 baud init of supplied address */
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = target;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return rv;
	}


	/* Mode bytes are in 7-Odd-1, read as 8N1 and ignore parity */
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		cbuf, 1, 100);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}
	rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		&cbuf[1], 1, 100);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	/* Keybytes are 0x1 0x8a for VAG protocol */
	if ((cbuf[0] != 0x01) || (cbuf[1] != 0x8a)) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}

	/* Note down the mode bytes */
	d_l2_conn->diag_l2_kb1 = cbuf[0] & 0x7f;
	d_l2_conn->diag_l2_kb2 = cbuf[1] & 0x7f;

	if ( (d_l2_conn->diag_link->diag_l2_l1flags
		& DIAG_L1_DOESSLOWINIT) == 0)
	{

		/*
		 * Now transmit KB2 inverted
		 */
		cbuf[0] = ~ d_l2_conn->diag_l2_kb2;
		rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			cbuf, 1, d_l2_conn->diag_l2_p4min);

	}


	/*
	 * Now receive the first 3 messages
	 * which show ECU versions etc
	 */


	return 0;
}

//free what _startcomms alloc'ed
static int diag_l2_proto_vag_stopcomms(struct diag_l2_conn *d_l2_conn) {

	if (d_l2_conn->diag_l2_proto_data)
		free(d_l2_conn->diag_l2_proto_data);
	d_l2_conn->diag_l2_proto_data=NULL;

	return 0;
}

/*
 * Send the data
 *
 * - with VAG protocol this will sleep as the message is sent as each byte
 * is ack'ed by the far end
 *
 * - after each byte reset the send timer or the timeout routine may try
 * and send something
 *
 * 1st byte of message is command, followed by data
 */
static int
diag_l2_proto_vag_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv = 0;
/*	int i;*/
/*	int csum;*/
/*	int len;*/
/*	uint8_t buf[MAXRBUF];*/
/*	int offset;*/
	//struct diag_l2_vag *dp;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_vag_send %p msg %p len %d called\n",
				FL, (void *)d_l2_conn, (void *)msg, msg->len);

	//dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;	//not used ?

#if xx
	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		buf, len, d_l2_conn->diag_l2_p4min);
#endif

	return rv? diag_iseterr(rv):0 ;
}

/*
 * Protocol receive routine
 *
 * Will sleep until a complete set of responses has been received, or fail
 * with a timeout error
 *
 * The interbyte type in data from an ecu is between P1Min and P1Max
 * The intermessage time for part of one response is P2Min and P2Max
 *
 * If we are running with an intelligent L1 interface, then we will be
 * getting one message per frame, and we will wait a bit longer
 * for extra messages
 */
static int
diag_l2_proto_vag_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg),
	void *handle)
{
	uint8_t data[256];
	int rv;
	int datalen;

	/* Call internal routine */
	rv = diag_l2_proto_vag_int_recv(d_l2_conn, timeout, data, &datalen);

	if (rv <= 0)	/* Failure */
		return rv;

	if (diag_l2_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr, FLFMT "calling rcv callback, handle=%p\n", FL,
			(void *)handle);	//%pcallback! we won't try to printf the callback pointer.
	}

	/*
	 * Call user callback routine
	 */
	if (callback)
		callback(handle, d_l2_conn->diag_msg);

	/* No longer needed */
	diag_freemsg(d_l2_conn->diag_msg);
	d_l2_conn->diag_msg = NULL;

	if (diag_l2_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr, FLFMT "rcv callback completed\n", FL);
	}

	return 0;
}

static struct diag_msg *
diag_l2_proto_vag_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0)
	{
		*errval = rv;
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/* And wait for response */
	//XXX this l2_iso9141 function should be re-written for l2_vag; they may be incompatible...
	rv = diag_l2_proto_iso9141_int_recv(d_l2_conn, 1000);
	if ((rv >= 0) && d_l2_conn->diag_msg)
	{
		/* OK */
		rmsg = d_l2_conn->diag_msg;
		d_l2_conn->diag_msg = NULL;
	}
	else
	{
		/* Error */
		*errval = DIAG_ERR_TIMEOUT;
		rmsg = NULL;
	}
	return rmsg;
}


/*
 * Timeout, - if we don't send something to the ECU it will timeout
 * soon, so send it a keepalive message now.
 */
static void
diag_l2_proto_vag_timeout(struct diag_l2_conn *d_l2_conn)
{
	//struct diag_l2_vag *dp;
	struct diag_msg msg={0};
	uint8_t data[256];
/*	int rv;*/

	//dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;	//not used?

	if (diag_l2_debug & DIAG_DEBUG_TIMER)
	{
		fprintf(stderr, FLFMT "timeout impending for %p\n",
				FL, (void *)d_l2_conn);
	}

	msg.data = data;

	/*
	 * There is no point in checking for errors, or checking
	 * the received response as we cant pass an error back
	 * from here
	 */

	/* Send it, important to use l2_send as it updates the timers */
	(void)diag_l2_send(d_l2_conn, &msg);

	/* Get the response in p2max, we use p3min to be more flexible */
	(void)diag_l2_recv(d_l2_conn, d_l2_conn->diag_l2_p3min, NULL, NULL);
}

const struct diag_l2_proto diag_l2_proto_vag = {
	DIAG_L2_PROT_VAG,
	"VAG",
	DIAG_L2_FLAG_FRAMED,
	diag_l2_proto_vag_startcomms,
	diag_l2_proto_vag_stopcomms,
	diag_l2_proto_vag_send,
	diag_l2_proto_vag_recv,
	diag_l2_proto_vag_request,
	diag_l2_proto_vag_timeout
};
