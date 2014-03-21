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
 * Diag
 *
 * L2 driver for ISO14230-2 layer 2
 *
 */

#ifdef WIN32
#else
#include <unistd.h>	//do we need this ?
#endif

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_err.h"
#include "diag_iso14230.h"

#include "diag_l2_iso14230.h" /* prototypes for this file */

CVSID("$Id$");


/*
 * ISO 14230 specific data
 */
struct diag_l2_14230
{
	uint8_t type;		/* FAST/SLOW/CARB */

	uint8_t srcaddr;	/* Src address used */
	uint8_t dstaddr;	/* Dest address used (for connect) */
	uint16_t modeflags;	/* Flags */

	uint8_t state;

	uint8_t first_frame;	/* First frame flag, used mainly for
					monitor mode when we need to find
					out whether we see a CARB or normal
					init */

	uint8_t rxbuf[MAXRBUF];	/* Receive buffer, for building message in */
	int rxoffset;		/* Offset to write into buffer */
};

#define STATE_CLOSED	  0	/* Established comms */
#define STATE_CONNECTING  1	/* Connecting */
#define STATE_ESTABLISHED 2	/* Established */


/*
 * Useful internal routines
 */

/*
 * Decode the message header, returning the length
 * of the message if a whole message has been received.
 * Note that this may be called with more than one message
 * but it only worries about the first message
 */
static int
diag_l2_proto_14230_decode(uint8_t *data, int len,
		 uint8_t *hdrlen, int *datalen, uint8_t *source, uint8_t *dest,
		int first_frame)
{
	uint8_t dl;

	if (diag_l2_debug & DIAG_DEBUG_PROTO) {
		int i;
		fprintf(stderr, FLFMT "decode len %d", FL, len);
		for (i = 0; i < len ; i++) {
			fprintf(stderr, " 0x%x", data[i]&0xff);
		}
		fprintf(stderr, "\n");
	}

	dl = data[0] & 0x3f;
	if (dl == 0) {
		/* Additional length field present */
		switch (data[0] & 0xC0)
		{
		case 0x80:
		case 0xC0:
			/* Addresses supplied, additional len byte */
			if (len < 4) {
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
	} else {
		/* Additional length field not present */
		switch (data[0] & 0xC0) {
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

	if (diag_l2_debug & DIAG_DEBUG_PROTO)
	{
		fprintf(stderr, FLFMT "decode hdrlen = %d, datalen = %d, cksum = 1\n",
			FL, *hdrlen, *datalen);
	}
	return (*hdrlen + *datalen + 1);
}

/*
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
diag_l2_proto_14230_int_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	uint8_t *data, int *pDatalen)
{
	struct diag_l2_14230 *dp;
	int rv, l1_doesl2frame, l1flags;
	int tout;
	int state;
	struct diag_msg	*tmsg, *lastmsg;

#define ST_STATE1	1	/* Start */
#define ST_STATE2	2	/* Interbyte */
#define ST_STATE3	3	/* Inter message */

	dp = (struct diag_l2_14230 *)d_l2_conn->diag_l2_proto_data;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "diag_l2_14230_intrecv offset %x\n",
				FL, dp->rxoffset);

	state = ST_STATE1;
	tout = timeout;

	/* Clear out last received message if not done already */
	if (d_l2_conn->diag_msg) {
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	l1flags = d_l2_conn->diag_link->diag_l2_l1flags;
	if (l1flags & (DIAG_L1_DOESL2FRAME|DIAG_L1_DOESP4WAIT)) {
		if (timeout < 100)	/* Extend timeouts */
			timeout = 100;
	}
	if (l1flags & DIAG_L1_DOESL2FRAME)
		l1_doesl2frame = 1;
	else
		l1_doesl2frame = 0;

	while (1) {
		switch (state) {
		case ST_STATE1:
			tout = timeout;
			break;
		case ST_STATE2:
			tout = d_l2_conn->diag_l2_p2min - 2;
			if (tout < d_l2_conn->diag_l2_p1max)
				tout = d_l2_conn->diag_l2_p1max;
			break;
		case ST_STATE3:
			if (l1_doesl2frame)
				tout = 150;	/* Arbitrary, short, value ... */
			else
				tout = d_l2_conn->diag_l2_p2max;
		}

		/* Receive data into the buffer */

		if (diag_l2_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "before recv, state %d timeout %d, rxoffset %d\n",
				FL, state, tout, dp->rxoffset);


		/*
		 * In l1_doesl2frame mode, we get full frames, so we don't
		 * do the read in state2
		 */
		if ( (state == ST_STATE2) && l1_doesl2frame )
			rv = DIAG_ERR_TIMEOUT;
		else
			rv = diag_l1_recv(d_l2_conn->diag_link->diag_l2_dl0d, 0,
				&dp->rxbuf[dp->rxoffset],
				sizeof(dp->rxbuf) - dp->rxoffset,
				tout);

		if (diag_l2_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,
				FLFMT "after recv, rv %d rxoffset %d\n", FL, rv, dp->rxoffset);


		if (rv == DIAG_ERR_TIMEOUT) {
			/* Timeout, end of message, or end of responses */
			switch (state) {
			case ST_STATE1:
				/*
				 * 1st read, if we got 0 bytes, just return
				 * the timeout error
				 */
				if (dp->rxoffset == 0)
					break;
				/*
				 * Otherwise see if there are more bytes in
				 * this message,
				 */
				state = ST_STATE2;
				continue;
			case ST_STATE2:
				/*
				 * End of that message, maybe more to come
				 * Copy data into a message
				 */
				tmsg = diag_allocmsg((size_t)dp->rxoffset);
				tmsg->len = (uint8_t) dp->rxoffset;
				memcpy(tmsg->data, dp->rxbuf, (size_t)dp->rxoffset);
				(void)gettimeofday(&tmsg->rxtime, NULL);
				dp->rxoffset = 0;
				/*
				 * ADD message to list
				 */
				diag_l2_addmsg(d_l2_conn, tmsg);
				if (d_l2_conn->diag_msg == tmsg) {

					if ((diag_l2_debug & DIAG_DEBUG_DATA) && (diag_l2_debug & DIAG_DEBUG_PROTO)) {
						fprintf(stderr, FLFMT "Copying %u bytes to data\n",
							FL, tmsg->len);
						diag_data_dump(stderr, tmsg->data, tmsg->len);
						fprintf(stderr, "\n");
					}

					/* 1st one */
					if (data) {
						memcpy(data, tmsg->data,
							(size_t)tmsg->len);
						*pDatalen = tmsg->len;
					}
				}
				state = ST_STATE3;
				continue;
			case ST_STATE3:
				/*
				 * No more messages, but we did get one
				 */
				rv = d_l2_conn->diag_msg->len;
				break;
			}
			if (state == ST_STATE3)
				break;
		}

		if (rv<0)
			break;

		/* Data received OK */
		dp->rxoffset += rv;

		if (dp->rxoffset && (dp->rxbuf[0] == '\0')) {
			/*
			 * We get this when in
			 * monitor mode and there is
			 * a fastinit, pretend it didn't exist
			 */
			dp->rxoffset--;
			if (dp->rxoffset)
				memcpy(&dp->rxbuf[0], &dp->rxbuf[1],
					(size_t)dp->rxoffset);
			continue;
		}
		if ( (state == ST_STATE1) || (state == ST_STATE3) ) {
			/*
			 * Got some data in state1/3, now we're in a message
			 */
			state = ST_STATE2;
		}
	}

	/*
	 * Now check the messages that we have checksum etc, stripping
	 * off headers etc
	 */
	if (rv >= 0) {
		tmsg = d_l2_conn->diag_msg;
		lastmsg = NULL;

		while (tmsg) {
			int datalen;
			uint8_t hdrlen, source, dest;

			/*
			 * We have the message with the header etc, we
			 * need to strip the header and checksum
			 */
			dp = (struct diag_l2_14230 *)d_l2_conn->diag_l2_proto_data;
			rv = diag_l2_proto_14230_decode( tmsg->data,
				tmsg->len,
				&hdrlen, &datalen, &source, &dest,
				dp->first_frame);

			if (rv < 0 || rv>255)		/* decode failure */
				return diag_iseterr(rv);

			/*
			 * If L1 isnt doing L2 framing then it is possible
			 * we have misframed this message and it is infact
			 * more than one message, so see if we can decode it
			 */
			if ((l1_doesl2frame == 0) && (rv < tmsg->len)) {
				/*
				 * This message contains more than one
				 * data frame (because it arrived with
				 * odd timing), this means we have to
				 * do horrible copy about the data
				 * things ....
				 */
				struct diag_msg	*amsg;
				amsg = diag_dupsinglemsg(tmsg);
				amsg->len = (uint8_t) rv;
				tmsg->len -= (uint8_t) rv;
				tmsg->data += rv;

				/*  Insert new amsg before old msg */
				amsg->next = tmsg;
				if (lastmsg == NULL)
					d_l2_conn->diag_msg = amsg;
				else
					lastmsg->next = amsg;

				tmsg = amsg; /* Finish processing this one */
			}

			if (diag_l2_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr,
				FLFMT "msg %p decode/rejig done rv %d hdrlen %u datalen %d source %02x dest %02x\n",
					FL, (void *)tmsg, rv, hdrlen, datalen, source, dest);


			if ((tmsg->data[0] & 0xC0) == 0xC0) {
				tmsg->fmt = DIAG_FMT_ISO_FUNCADDR;
			} else {
				tmsg->fmt = 0;
			}
			tmsg->fmt |= DIAG_FMT_FRAMED | DIAG_FMT_DATAONLY ;
			tmsg->fmt |= DIAG_FMT_CKSUMMED;

			if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0) {
				/* XXX check checksum */
			}

			tmsg->src = source;
			tmsg->dest = dest;
			tmsg->data += hdrlen;	/* Skip past header */
			tmsg->len -= hdrlen; /* remove header */

			/* remove checksum byte if needed */
			if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0)
				tmsg->len--;

			dp->first_frame = 0;

			lastmsg = tmsg;
			tmsg = tmsg->next;
		}
	}
	return rv;
}


/* External interface */

static int
diag_l2_proto_14230_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg);
/*
 * The complex initialisation routine for ISO14230, which supports
 * 2 types of initialisation (5-BAUD, FAST) and functional
 * and physical addressing. The ISO14230 spec describes CARB initialisation
 * which is done in the ISO9141 code
 *
 * Remember, we have to wait longer on smart L1 interfaces.
 */
static int
diag_l2_proto_14230_startcomms( struct diag_l2_conn	*d_l2_conn, flag_type flags,
	unsigned int bitrate, target_type target, source_type source)
{
	struct diag_l2_14230 *dp;
	struct diag_msg	msg;
	uint8_t data[MAXRBUF];
	int rv, wait_time;
	int datalen;
	uint8_t datasrc, hdrlen;
	uint8_t cbuf[MAXRBUF];
	int len;
	int timeout;
	struct diag_serial_settings set;

	struct diag_l1_initbus_args in;

	if (diag_calloc(&dp, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	d_l2_conn->diag_l2_proto_data = (void *)dp;
	dp->type = flags & DIAG_L2_TYPE_INITMASK;

	dp->srcaddr = source;
	dp->dstaddr = target;
	dp->modeflags = flags;
	dp->first_frame = 1;

	memset(data, 0, sizeof(data));

	/*
	 * If 0 has been specified, use the correct speed
	 * for ISO14230 protocol
	 */
	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */

	if ((rv=diag_l1_setspeed(d_l2_conn->diag_link->diag_l2_dl0d, &set)))
	{
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
		return diag_iseterr(rv);
	}

	dp->state = STATE_CONNECTING ;

	/* Flush unread input, then wait for idle bus. */
	(void)diag_tty_iflush(d_l2_conn->diag_link->diag_l2_dl0d);
	diag_os_millisleep(300);

	switch (dp->type) {
	/* Fast initialisation */
	case DIAG_L2_TYPE_FASTINIT:

		/* Build an ISO14230 StartComms message */
		if (flags & DIAG_L2_TYPE_FUNCADDR)
		{
			msg.fmt = DIAG_FMT_ISO_FUNCADDR;
			d_l2_conn->diag_l2_physaddr = 0; /* Don't know it yet */
		}
		else
		{
			msg.fmt = 0;
			d_l2_conn->diag_l2_physaddr = target;
		}
		msg.src = source;
		msg.dest = target;
		msg.len = 1 ;
		data[0]= DIAG_KW2K_SI_SCR ;	/* startCommunication rqst*/
		msg.data = data;

		/* Do fast init stuff */
		in.type = DIAG_L1_INITBUS_FAST;
		rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);

		if (rv < 0)
			break;

		/* Send the prepared message */
		diag_l2_proto_14230_send(d_l2_conn, &msg);

		if (d_l2_conn->diag_link->diag_l2_l1flags & DIAG_L1_DOESL2FRAME)
			timeout = 200;
		else
			timeout = d_l2_conn->diag_l2_p2max + 20;

		/* And wait for a response, ISO14230 says will arrive in P2 */
		rv = diag_l2_proto_14230_int_recv(d_l2_conn,
				timeout, data, &len);
		if (rv < 0) {
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
			return diag_iseterr(rv);
		}

		rv = diag_l2_proto_14230_decode( data, len,
			&hdrlen, &datalen, &datasrc, NULL, dp->first_frame);
		if (rv < 0) {
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
			return diag_iseterr(rv);
		}

		switch (data[hdrlen]) {
		case DIAG_KW2K_RC_SCRPR:	/* StartComms +ve response */

			d_l2_conn->diag_l2_kb1 = data[hdrlen+1];
			d_l2_conn->diag_l2_kb2 = data[hdrlen+2];
			d_l2_conn->diag_l2_physaddr = datasrc;

			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr,
					FLFMT "diag_l2_14230_StartComms",
					FL);
				fprintf(stderr," Physaddr 0x%x",
					datasrc);
				fprintf(stderr," KB1 = %x, KB2 = %x\n",
					d_l2_conn->diag_l2_kb1,
					d_l2_conn->diag_l2_kb2);
			}
			dp->state = STATE_ESTABLISHED ;
			break;
		case DIAG_KW2K_RC_NR:
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr,
					FLFMT "diag_l2_14230_StartComms",
					FL);
				fprintf(stderr, " got -ve response\n");
			}
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
			return diag_iseterr(DIAG_ERR_ECUSAIDNO);
		default:
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr,
					FLFMT "diag_l2_14230_StartComms",
					FL);
				fprintf(stderr, " got unexpected response 0x%x\n",
					data[hdrlen]);
			}
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
			return diag_iseterr(DIAG_ERR_ECUSAIDNO);
		}
		break;
	/* 5 Baud init */
	case DIAG_L2_TYPE_SLOWINIT:
		in.type = DIAG_L1_INITBUS_5BAUD;
		in.addr = target;
		rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
		if (rv < 0)
			break;

		/* Mode bytes are in 7-Odd-1, read as 8N1 and ignore parity */
		//XXX why don't we call _recv with 2 bytes (we want 2 keybytes...) ?
		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			cbuf, 1, 100);
		if (rv < 0)
			break;
		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			&cbuf[1], 1, 100);
		if (rv < 0)
			break;

		/* ISO14230 uses KB2 of 0x8F */
		if (cbuf[1] != 0x8f) {
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
			return diag_iseterr(DIAG_ERR_WRONGKB);
		}

		/* Note down the mode bytes */
		d_l2_conn->diag_l2_kb1 = cbuf[0] & 0x7f;
		d_l2_conn->diag_l2_kb2 = cbuf[1] & 0x7f;

		if ( (d_l2_conn->diag_link->diag_l2_l1flags
			& DIAG_L1_DOESSLOWINIT) == 0) {

			/*
			 * Now transmit KB2 inverted
			 */
			cbuf[0] = ~ d_l2_conn->diag_l2_kb2;
			rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
				cbuf, 1, d_l2_conn->diag_l2_p4min);

			/*
			 * And wait for the address byte inverted
			 */
			rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
				cbuf, 1, 350);

			if (cbuf[0] != ((~target) & 0xFF) ) {
				free(dp);
				d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
				return diag_iseterr(DIAG_ERR_WRONGKB);
			}
		}

		dp->state = STATE_ESTABLISHED ;
		break;
	case DIAG_L2_TYPE_MONINIT:
		/* Monitor mode, don't send anything */
		dp->state = STATE_ESTABLISHED ;
		rv = 0;
		break;
	default:
		rv = DIAG_ERR_INIT_NOTSUPP;
		break;
	}

	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;	//delete pointer to dp
		return diag_iseterr(rv);
	}

	/*
	 * Now, we want to remove any rubbish left
	 * in inbound buffers, and wait for the bus to be
	 * quiet for a while before we will communicate
	 * (so that the next byte received is the first byte
	 * of an iso14230 frame, not a middle byte)
	 * We use 1/2 of P2max (inter response gap) or
	 * 5 * p4max (inter byte delay), whichever is larger
	 * a correct value to use
	 */
	wait_time = d_l2_conn->diag_l2_p2max / 2 ;
	if ((d_l2_conn->diag_l2_p4max * 5) > wait_time)
		wait_time = d_l2_conn->diag_l2_p4max * 5;

	while ( diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		  data, sizeof(data), wait_time) != DIAG_ERR_TIMEOUT) ;

	/* And we're done */
	dp->state = STATE_ESTABLISHED ;

	return 0;
}


//stopcomms : incomplete. For the moment it only undoes
// what _startcomms did (alloc)
static int
diag_l2_proto_14230_stopcomms(struct diag_l2_conn* pX)
{
	/*
	 * Send a stopcomms message, and wait for the +ve response, for upto
	 * p3max - the layer 2 code that called this already turned off the
	 * idle timer
	 */
/* TODO : implement StopComm request! */
	fprintf(stderr, FLFMT "14230_stopcomms: warning, incomplete code !\n", FL);
	//at least we'll free() what startcomms alloc'ed.
	if (pX->diag_l2_proto_data) {
		free(pX->diag_l2_proto_data);
		pX->diag_l2_proto_data=NULL;
	}

	return 0;
}

/*
 * Just send the data
 *
 * We add the header and checksums here as appropriate, based on the keybytes
 *
 * We take the source and dest from the internal data NOT from the msg fields
 *
 * We also wait p3 ms
 */
static int
diag_l2_proto_14230_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg)
{
	int rv;
	uint8_t csum;
	unsigned int i;
	size_t len;
	uint8_t buf[MAXRBUF];
	int offset;
	struct diag_l2_14230 *dp;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
			FLFMT "diag_l2_14230_send %p msg %p len %d called\n",
				FL, (void *)d_l2_conn, (void *)msg, msg->len);

	dp = (struct diag_l2_14230 *)d_l2_conn->diag_l2_proto_data;

	/* Build the new message */

	if (dp->modeflags & DIAG_L2_TYPE_FUNCADDR)
		buf[0] = 0xC0;
	else
		buf[0] = 0x80;

	/* If user supplied addresses, use them, else use the originals */
	if (msg->dest)
		buf[1] = msg->dest;
	else
		buf[1] = dp->dstaddr;
	if (msg->src)
		buf[2] = msg->src;
	else
		buf[2] = dp->srcaddr;

/* TODO:, check mode flag that specifies always use 4 byte hdr ,
	or mode flag showing never use extended header, and
		received keybytes */

	if (msg->len < 64) {
		if (msg->len < 1)
			return diag_iseterr(DIAG_ERR_BADLEN);
		buf[0] |= msg->len;
		offset = 3;
	} else {
		buf[3] = msg->len;
		offset = 4;
	}
	memcpy(&buf[offset], msg->data, msg->len);

	len = msg->len + offset;	/* data + hdr */

	if ((d_l2_conn->diag_link->diag_l2_l1flags & DIAG_L1_DOESL2CKSUM) == 0) {
		/* We must add checksum, which is sum of bytes */
		for (i = 0, csum = 0; i < len; i++)
			csum += buf[i];
		buf[len] = csum;
		len++;				/* + checksum */
	}

	/* Wait p3min milliseconds, but not if doing fast/slow init */
	if (dp->state == STATE_ESTABLISHED)
		diag_os_millisleep(d_l2_conn->diag_l2_p3min);

	rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
		buf, len, d_l2_conn->diag_l2_p4min);

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "send about to return %d\n",
				FL, rv);

	return rv;
}

/*
 * Protocol receive routine
 *
 * Will sleep until a complete set of responses has been received, or fail
 * with a timeout error
 *
 * The interbyte type in data from an ECU is between P1Min and P1Max
 * The intermessage time for part of one response is P2Min and P2Max
 *
 * If we are running with an intelligent L1 interface, then we will be
 * getting one message per frame, and we will wait a bit longer
 * for extra messages
 */
static int
diag_l2_proto_14230_recv(struct diag_l2_conn *d_l2_conn, int timeout,
	void (*callback)(void *handle, struct diag_msg *msg),
	void *handle)
{
	uint8_t data[256];
	int rv;
	int datalen;

	/* Call internal routine */
	rv = diag_l2_proto_14230_int_recv(d_l2_conn, timeout, data, &datalen);

	if (rv < 0)	/* Failure */
		return rv;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "l2_protO_14230_int_recv : handle=%p\n", FL,
			(void *)handle);	//%pcallback! we won't try to printf the callback pointer.

	/*
	 * Call user callback routine
	 */
	if (callback)
		callback(handle, d_l2_conn->diag_msg);

	/* No longer needed */
	diag_freemsg(d_l2_conn->diag_msg);
	d_l2_conn->diag_msg = NULL;

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "rcv callback completed\n", FL);

	return 0;
}

static struct diag_msg *
diag_l2_proto_14230_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
		int *errval)
{
	int rv;
	struct diag_msg *rmsg = NULL;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return (struct diag_msg *)diag_pseterr(rv);
	}

	while (1) {
		rv = diag_l2_proto_14230_int_recv(d_l2_conn,
			d_l2_conn->diag_l2_p2max + 10, NULL, NULL);

		if (rv < 0) {
			*errval = DIAG_ERR_TIMEOUT;
			return (struct diag_msg *)diag_pseterr(rv);
		}

		/*
		 * The connection now has the received message data
		 * stored, remove it and deal with it
		 */
		rmsg = d_l2_conn->diag_msg;
		d_l2_conn->diag_msg = NULL;

		/* Got a Error message */
		if (rmsg->data[0] == DIAG_KW2K_RC_NR) {
			if (rmsg->data[2] == DIAG_KW2K_RC_B_RR) {
				/*
				 * Msg is busyRepeatRequest
				 * So do a send again
				 */
				rv = diag_l2_send(d_l2_conn, msg);
				if (rv < 0) {
					*errval = rv;
					return (struct diag_msg *)diag_pseterr(rv);
				}
				diag_freemsg(rmsg);
				continue;
			}

			if (rmsg->data[2] == DIAG_KW2K_RC_RCR_RP) {
				/*
				 * Msg is a requestCorrectlyRcvd-RspPending
				 * so do read again
				 */
				diag_freemsg(rmsg);
				continue;
			}
			/* Some other type of error */
		} else {
			/* Success */
			break;
		}
	}
	/* Return the message to user, who is responsible for freeing it */
	return rmsg;
}

/*
 * Timeout, - if we don't send something to the ECU it will timeout
 * soon, so send it a keepalive message now.
 */
static void
diag_l2_proto_14230_timeout(struct diag_l2_conn *d_l2_conn)
{
	struct diag_l2_14230 *dp;
	struct diag_msg	msg;
	uint8_t data[256];
	int timeout;

	dp = (struct diag_l2_14230 *)d_l2_conn->diag_l2_proto_data;

	/* XXX fprintf not async-signal-safe */
	if (diag_l2_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "timeout impending for %p type %d\n",
				FL, (void *)d_l2_conn, dp->type);
	}

	msg.data = data;

	/* Prepare the "keepalive" message */
	if (dp->modeflags & DIAG_L2_IDLE_J1978) {
		/* Idle using J1978 spec */
		msg.len = 2;
		data[0] = 1;
		data[1] = 0;
	} else {
		/* Idle using ISO "Tester Present" message */
		msg.len = 1;
		msg.dest = 0;	/* Use default */
		msg.src = 0;	/* Use default */
		data[0] = DIAG_KW2K_SI_TP;
	}

	/*
	 * There is no point in checking for errors, or checking
	 * the received response as we can't pass an error back
	 * from here
	 */

	/* Send it, important to use l2_send as it updates the timers */
	(void)diag_l2_send(d_l2_conn, &msg);

	/*
	 * Get the response in p2max, we allow longer, and even
	 * longer on "smart" L2 interfaces
	 */
	timeout = d_l2_conn->diag_l2_p3min;
	if (d_l2_conn->diag_link->diag_l2_l1flags &
			(DIAG_L1_DOESL2FRAME|DIAG_L1_DOESP4WAIT)) {
		if (timeout < 100)
			timeout = 100;
	}
	(void)diag_l2_recv(d_l2_conn, timeout, NULL, NULL);
}
static const struct diag_l2_proto diag_l2_proto_14230 = {
	DIAG_L2_PROT_ISO14230, DIAG_L2_FLAG_FRAMED | DIAG_L2_FLAG_DATA_ONLY
	| DIAG_L2_FLAG_KEEPALIVE | DIAG_L2_FLAG_DOESCKSUM,
	diag_l2_proto_14230_startcomms,
	diag_l2_proto_14230_stopcomms,
	diag_l2_proto_14230_send,
	diag_l2_proto_14230_recv,
	diag_l2_proto_14230_request,
	diag_l2_proto_14230_timeout
};

int diag_l2_14230_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_14230);
}
