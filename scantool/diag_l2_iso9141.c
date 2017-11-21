/*
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
 * L2 driver for ISO 9141 protocol.
 *
 * NOTE: ISO9141-2 says that if the target address is 0x33, then the SAE-J1979
 * Scantool Application Protocol is used.
 *
 * Other addresses are manufacturer-specific, and MAY EXCEED THIS IMPLEMENTATION.
 * (But we still let you TRY to use them... :) Just keep in mind that ISO9141 messages
 * have a maximum payload of 7 bytes.
 */

#include <string.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_iso9141.h" /* prototypes for this file */



/*
 * This implements the handshaking process between Tester and ECU.
 * It is used to wake up an ECU and get its KeyBytes.
 *
 * The process as defined in ISO9141 is:
 * 1 - Tester sends target address (0x33) at 5 baud;
 * 2 - ECU wakes up, sends synch pattern 0x55 at approx. 10400 baud;
 * 3 - Tester clocks synch pattern and defines baud rate (10400 baud);
 * 4 - ECU sends first KeyByte;
 * 5 - ECU sends second KeyByte;
 * 6 - Tester regulates p2 time according to KeyBytes;
 * 6 - Tester sends second KeyByte inverted;
 * 7 - ECU sends received address inverted (0xCC);
 * This concludes a successfull handshaking in ISO9141.
 */
int
dl2p_iso9141_wakeupECU(struct diag_l2_conn *d_l2_conn) {
	struct diag_l1_initbus_args in;
	uint8_t kb1, kb2, address, inv_address, inv_kb2;
	int rv = 0;
	struct diag_l2_iso9141 *dp;

	kb1 = kb2 = address = inv_address = inv_kb2 = 0;
	dp = d_l2_conn->diag_l2_proto_data;

	// Flush unread input:
	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);

	// Wait for idle bus:
	diag_os_millisleep(W5min);

	// Do 5Baud init (write Address, read Synch Pattern):
	address = dp->target;
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = address;
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0)
		return diag_iseterr(rv);

	if (d_l2_conn->diag_link->l1flags & DIAG_L1_DOESFULLINIT) {
		d_l2_conn->diag_l2_kb1=0x08;
		d_l2_conn->diag_l2_kb2=0x08;	//possibly not true, but who cares.
		d_l2_conn->diag_l2_p2min = 25;
		return 0;
	}

	// The L1 device has read the 0x55, and reset the previous speed.

	// Receive the first KeyByte:
	rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0, &kb1, 1, W2max+RXTOFFSET);
	if (rv < 0)
		return diag_iseterr(DIAG_ERR_WRONGKB);

	// Receive the second KeyByte:
	rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0, &kb2, 1, W3max+RXTOFFSET);
	if (rv < 0)
		return diag_iseterr(DIAG_ERR_WRONGKB);

	// Check keybytes, these can be 0x08 0x08 or 0x94 0x94:
	if ( (kb1 != kb2) || ( (kb1 != 0x08) && (kb1 != 0x94) ) ) {
		fprintf(stderr, FLFMT "Wrong Keybytes: got %02X %02X\n", FL, kb1, kb2);
		return diag_iseterr(DIAG_ERR_WRONGKB);
	}

	// Copy KeyBytes to protocol session data:
	d_l2_conn->diag_l2_kb1 = kb1;
	d_l2_conn->diag_l2_kb2 = kb2;

	// set p2min according to KeyBytes:
	// P2min is 0 for kb 0x94, 25ms for kb 0x08;
	if (kb1 == 0x94)
		d_l2_conn->diag_l2_p2min = 0;
	else
		d_l2_conn->diag_l2_p2min = 25;

	// Now send inverted KeyByte2, and receive inverted address
	// (unless L1 deals with this):
	if ( (d_l2_conn->diag_link->l1flags
	  & DIAG_L1_DOESSLOWINIT) == 0) {
		//Wait W4min:
		diag_os_millisleep(W4min);

		//Send inverted kb2:
		inv_kb2 = (uint8_t) ~kb2;
		rv = diag_l1_send (d_l2_conn->diag_link->l2_dl0d, 0,
					&inv_kb2, 1, 0);
		if (rv < 0)
			return diag_iseterr(rv);

		// Wait for the address byte inverted:
		// XXX I added RXTOFFSET as a band-aid fix for systems, like
		//mine, that don't receive ~addr with only W4max. See #define
		//NOTE : l2_iso14230 uses a huge 350ms timeout for this!!
		rv = diag_l1_recv (d_l2_conn->diag_link->l2_dl0d, 0,
					&inv_address, 1, W4max + RXTOFFSET);
		if (rv < 0) {
			if (diag_l2_debug & DIAG_DEBUG_OPEN)
				fprintf(stderr,
					FLFMT "wakeupECU(dl2conn %p) did not get inv. address; rx error %d\n",
					FL, (void *)d_l2_conn, rv);
			return diag_iseterr(rv);
		}

		// Check the received inverted address:
		if ( inv_address != ((~address) & 0xff)) {
			fprintf(stderr,
					FLFMT "wakeupECU(dl2conn %p) addr mismatch: 0x%02X != 0x%02X\n",
					FL, (void *)d_l2_conn, inv_address, ~address);
			return diag_iseterr(DIAG_ERR_BADDATA);
		}
	}

	//Success! Handshaking done.

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "_wakeupECU dl2con=%p kb1=0x%02X kb2=0x%02X\n",
			FL, (void *)d_l2_conn, kb1, kb2);

	return 0;
}


/*
 * This implements the start of a new protocol session.
 * It wakes up an ECU if not in monitor mode.
 */
static int
dl2p_iso9141_startcomms(struct diag_l2_conn *d_l2_conn,
				 flag_type flags, unsigned int bitrate,
				 target_type target, source_type source) {
	int rv;
	struct diag_serial_settings set;
	struct diag_l2_iso9141 *dp;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "_startcomms conn %p %ubps tgt=0x%X src=0x%X\n",
			FL, (void *)d_l2_conn, bitrate, target, source);

	if (diag_calloc(&dp, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	dp->srcaddr = source;
	dp->target = target;
	dp->state = STATE_CONNECTING;
	d_l2_conn->diag_l2_kb1 = 0;
	d_l2_conn->diag_l2_kb2 = 0;
	d_l2_conn->diag_l2_proto_data = (void *)dp;

	// Prepare the port for this protocol:
	// Data bytes are in {7bits, OddParity, 1stopbit}, but we
	// read and write as {8bits, NoParity, 1stopbit}.
	// That must be taken into account by the application / layer 3 !!!
	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;
	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if ((rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, &set))) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	// Initialize ECU (unless if in monitor mode):
	switch (flags & DIAG_L2_TYPE_INITMASK) {
		case DIAG_L2_TYPE_MONINIT:
			rv = 0;
			break;
		case DIAG_L2_TYPE_SLOWINIT:
			rv = dl2p_iso9141_wakeupECU(d_l2_conn);
			break;
		default:
			//CARB and FASTINIT are not in iso9141.
			rv = DIAG_ERR_INIT_NOTSUPP;
			break;
	}


	if (rv) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	dp->state = STATE_ESTABLISHED;

	return 0;
}


/*
 * Free session-specific allocated data.
 * ISO9141 doesn't have a StopCommunication mechanism like iso14230,
 * so we just "undo" what iso9141_startcomms did.
 */
static int
dl2p_iso9141_stopcomms(struct diag_l2_conn* d_l2_conn) {
	struct diag_l2_iso9141 *dp;

	dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;
	if (dp)
		free(dp);
	d_l2_conn->diag_l2_proto_data=NULL;

	//Always OK for now.
	return 0;
}


/*
 * This implements the interpretation of a response message.
 * With ISO9141, the data length will depend on the
 * content of the message, and cannot be guessed at the L1
 * level; therefore, only L3 / application will be able to
 * check it correctly. We just assume the data length is always =
 * (received_len - (header + chksm))
 * So this only verifies minimal length and valid header bytes.
 * Should only really be used by the _int_recv function.
 */
static int
dl2p_iso9141_decode(uint8_t *data, int len,
				 uint8_t *hdrlen, int *datalen, uint8_t *source, uint8_t *dest) {

	if (diag_l2_debug & DIAG_DEBUG_PROTO) {
		fprintf(stderr, FLFMT "decode len %d: ", FL, len);
		diag_data_dump(stderr,data, len);
		fprintf(stderr, "\n");
	}

	//Check header bytes:
	if(data[0] != 0x48 || data[1] != 0x6B )
		return diag_iseterr(DIAG_ERR_BADDATA);

	//verify minimal length
	if(len - OHLEN_ISO9141 > 0) {
		*datalen = len - OHLEN_ISO9141;
	} else {
		if (diag_l2_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "decode len short \n", FL);

		return diag_iseterr(DIAG_ERR_INCDATA);
	}

	//Set header length (always the same):
	*hdrlen = OHLEN_ISO9141 - 1;

	// Set Source and Destination addresses:
	if (dest)
		*dest = 0xF1; //Always the Tester.
	if (source)
		*source = data[2]; // Originating ECU;

	if (diag_l2_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr, FLFMT "decode total len = %d, datalen = %d\n",
			FL, len, *datalen);

	return OHLEN_ISO9141 + *datalen;
}

/*
 * This implements the reading of all the ECU responses to a Tester request.
 * One ECU may send multiple responses to one request.
 * Multiple ECUS may send responses to one request.
 * The end of all responses is marked by p2max timeout. (p3min > p2max so
 * that if we (the tester) send a new request after p3min, we are guaranteed
 * not to clobber an ECU response)
 * Ret 0 if OK.
 *
 * "timeout" has to be long enough to receive at least 1 byte;
 *  in theory it could be P2min + (8 / baudrate) but there is no
 * harm in using P2max.
 *
 * XXX Dilemma. To properly split messages, do we trust our timing VS iso9141 P2min/max requirements?
 * Do we try to find valid headers + checksum and filter out bad frames ?
 * Do we let L3_saej1979 try and DJ the framing through L2 ?
 */
int
dl2p_iso9141_int_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout) {
	int rv, l1_doesl2frame, l1flags;
	unsigned int tout = 0;
	int state;
	struct diag_l2_iso9141 *dp;
	struct diag_msg *tmsg, *lastmsg;

#define ST_STATE1 1 // Start - wait for a frame.
#define ST_STATE2 2 // In frame - wait for more bytes.
#define ST_STATE3 3 // End of frame - wait for more frames.

	if (diag_l2_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "_int_recv offset 0x%X\n",
			FL, d_l2_conn->rxoffset);

	dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;

	// Clear out last received message if not done already.
	if (d_l2_conn->diag_msg) {
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	// Check if L1 device does L2 framing:
	l1flags = d_l2_conn->diag_link->l1flags;
	l1_doesl2frame = (l1flags & DIAG_L1_DOESL2FRAME);
	if (l1_doesl2frame) {
		// Extend timeouts for the "smart" interfaces:
		if (timeout < SMART_TIMEOUT)
			timeout += SMART_TIMEOUT;
	}

	// Message read cycle: byte-per-byte for passive interfaces,
	// frame-per-frame for smart interfaces (DOESL2FRAME).
	// ISO-9141-2 says:
	// 	Inter-byte gap in a frame < p1max
	//	Inter-frame gap < p2max
	// We are a bit more flexible than that, see below.
	// Frames get acumulated in the protocol structure list.
	state = ST_STATE1;
	while (1) {
		switch(state) {
			case ST_STATE1:
				// Ready for first byte, use timeout
				// specified by user.
				tout = timeout;
				break;

			case ST_STATE2:
				// Inter-byte timeout within a frame.
				// ISO says p1max is the maximum, but in fact
				// we give ourselves up to p2min minus a little bit.
				tout = d_l2_conn->diag_l2_p2min - 2;
				if (tout < d_l2_conn->diag_l2_p1max)
					tout = d_l2_conn->diag_l2_p1max;
				break;

			case ST_STATE3:
				// This is the timeout waiting for any more
				// responses from the ECU. ISO says min is p2max
				// but we'll use p3min.
				// Aditionaly, for "smart" interfaces, we expand
				// the timeout to let them process the data.
				//TODO: maybe set tout=p3min + margin ?
				tout = d_l2_conn->diag_l2_p3min;
				if (l1_doesl2frame)
					tout += SMART_TIMEOUT;
				break;
		}

		// If L0/L1 does L2 framing, we get full frames, so we don't
		// need to do the read byte-per-byte (skip state2):
		if ( (state == ST_STATE2) && l1_doesl2frame )
			rv = DIAG_ERR_TIMEOUT;
		else if (dp->rxoffset == MAXLEN_ISO9141)
			rv = DIAG_ERR_TIMEOUT;	//we got a full frame already !
		else
			// Receive data into the buffer:
			rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0,
					&dp->rxbuf[dp->rxoffset],
					MAXLEN_ISO9141 - dp->rxoffset,
					tout);

		// Timeout = end of message or end of responses.
		if (rv == DIAG_ERR_TIMEOUT) {
			switch (state) {
				case ST_STATE1:
					// If we got 0 bytes on the 1st read,
					// just return the timeout error.
					if (dp->rxoffset == 0)
						break;

					// Otherwise try to read more bytes into
					// this message.
					state = ST_STATE2;
					continue;
					break;

				case ST_STATE2:
					// End of that message, maybe more to come;
					// Copy data into a message.
					tmsg = diag_allocmsg((size_t)dp->rxoffset);
					if (tmsg == NULL)
						return diag_iseterr(DIAG_ERR_NOMEM);
					memcpy(tmsg->data, dp->rxbuf,
						(size_t)dp->rxoffset);
					tmsg->rxtime = diag_os_getms();

					if (diag_l2_debug & DIAG_DEBUG_READ) {
						fprintf(stderr, "l2_iso9141_recv: ");
						diag_data_dump(stderr, dp->rxbuf, (size_t)dp->rxoffset);
						fprintf(stderr, "\n");
					}

					dp->rxoffset = 0;

					// Add received message to response list:
					diag_l2_addmsg(d_l2_conn, tmsg);

					// Finished this one, get more:
					state = ST_STATE3;
					continue;
					break;

				case ST_STATE3:
					/*
					 * No more messages, but we did get one
					 */
					rv = d_l2_conn->diag_msg->len;
					break;
				default:
					break;
			}	//switch (state)

			// end of all response messages:
			if (state == ST_STATE3)
				break;
		}	//if diag_err_timeout

		// Other reception errors.
		if (rv <= 0 || rv > 255)
			break;

		// Data received OK.
		// Add length to offset.
		dp->rxoffset += (uint8_t) rv;

		// This is where some tweaking might be needed if
		// we are in monitor mode... but not yet.

		// Got some data in state1/3, now we're in a message!
		if ( (state == ST_STATE1) || (state == ST_STATE3) )
			state = ST_STATE2;

	}//end while (read cycle).

	// Now walk through the response message list,
	// and strip off their headers and checksums
	// after verifying them.
	if (rv < 0) return diag_iseterr(rv);

	tmsg = d_l2_conn->diag_msg;
	lastmsg = NULL;

	while (tmsg) {
		int datalen;
		uint8_t hdrlen=0, source=0, dest=0;

		dp = (struct diag_l2_iso9141 *)d_l2_conn->diag_l2_proto_data;

		if ((l1flags & DIAG_L1_NOHDRS)==0) {
			// Parse message structure, if headers are present
			rv = dl2p_iso9141_decode( tmsg->data,
							tmsg->len, &hdrlen, &datalen, &source, &dest);

			if (rv < 0 || rv > 255) {
				// decode failure!
				return diag_iseterr(DIAG_ERR_BADDATA);
			}
		} else if (!l1_doesl2frame) {
			// Absent headers, and no framing -> illegal !
			fprintf(stderr, "Warning : insane L1flags (l2frame && nohdrs ?)\n");
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

		// Process L2 framing, if L1 doesn't do it.
		if (l1_doesl2frame == 0) {

			//we'll have to assume we have only a single message, since at the L1 level
			//it's impossible to guess the message length. But, if we got more than MAXLEN_ISO9141 bytes,
			//(not allowed by standard), we try splitting the message (assuming the first message had the maximum
			//length). It's the best that can be done at this level.

			if (rv > MAXLEN_ISO9141) {
				struct diag_msg	*amsg;
				amsg = diag_dupsinglemsg(tmsg);
				if (amsg == NULL) {
					return diag_iseterr(DIAG_ERR_NOMEM);
				}
				amsg->len = (uint8_t) MAXLEN_ISO9141;
				tmsg->len -= (uint8_t) MAXLEN_ISO9141;
				tmsg->data += MAXLEN_ISO9141;

				/*  Insert new amsg before old msg */
				amsg->next = tmsg;
				if (lastmsg == NULL)
					d_l2_conn->diag_msg = amsg;
				else
					lastmsg->next = amsg;

				tmsg = amsg; /* Finish processing this one */
			}

		}

		// If L1 doesn't strip the checksum byte, verify it:
		if ((l1flags & DIAG_L1_STRIPSL2CKSUM) == 0) {
			uint8_t rx_cs = tmsg->data[tmsg->len - 1];
			if(rx_cs != diag_cks1(tmsg->data, tmsg->len - 1)) {
				fprintf(stderr, FLFMT "Checksum error in received message!\n", FL);
				tmsg->fmt |= DIAG_FMT_BADCS;
			} else {
				tmsg->fmt &= ~DIAG_FMT_BADCS;
				tmsg->fmt |= DIAG_FMT_FRAMED ;	//if the checksum fits, it means we framed things properly.
			}
			// "Remove" the checksum byte:
			tmsg->len--;
		} else {
			tmsg->fmt |= DIAG_FMT_FRAMED ;	//if L1 stripped the checksum, it was probably valid ?
		}

		//if the headers aren't stripped by L1 already:
		if ((l1flags & DIAG_L1_NOHDRS)==0) {
			// Set source address:
			tmsg->src = source;
			// Set destination address:
			tmsg->dest = dest;

			// and "remove" headers:
			tmsg->data += (OHLEN_ISO9141 - 1);
			tmsg->len -= (OHLEN_ISO9141 - 1);
		}


		// Message done. Flag it up:
		tmsg->fmt |= DIAG_FMT_CKSUMMED;

		// Prepare to decode next message:
		lastmsg = tmsg;
		tmsg = tmsg->next;
	}	//while tmsg

	return 0;
}



//_recv : timeout is fed directly to _int_recv and should be long enough
//to guarantee we receive at least one byte. (P2Max should do the trick)
//ret 0 if ok
static int
dl2p_iso9141_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
			void (*callback)(void *handle, struct diag_msg *msg), void *handle) {
	int rv;

	rv = dl2p_iso9141_int_recv(d_l2_conn, timeout);
	if ((rv >= 0) && (d_l2_conn->diag_msg !=NULL)) {
		if (diag_l2_debug & DIAG_DEBUG_READ)
			fprintf(stderr, FLFMT "_recv : handle=%p\n", FL,
				(void *)handle);	//%pcallback! we won't try to printf the callback pointer.
		/*
		 * Call user callback routine
		 */
		if (callback)
			callback(handle, d_l2_conn->diag_msg);

		/* No longer needed */
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	return (rv<0)? diag_iseterr(rv):0 ;
}

/*
 * Package the data into a message frame with header and checksum.
 * Addresses were supplied by the protocol session initialization.
 * Checksum is calculated on-the-fly.
 * Apply inter-frame delay (p2).
 * ret 0 if ok
 */
static int
dl2p_iso9141_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv;
	unsigned int sleeptime;
	uint8_t buf[MAXLEN_ISO9141];
	int offset;
	struct diag_l2_iso9141 *dp;

	dp = d_l2_conn->diag_l2_proto_data;

	if (diag_l2_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,
		FLFMT "_send dl2c=%p msg=%p\n",
		FL, (void *)d_l2_conn, (void *)msg);

	// Check if the payload plus the overhead (and checksum) exceed protocol packet size:
	if(msg->len + OHLEN_ISO9141 > MAXLEN_ISO9141) {
		fprintf(stderr, FLFMT "send: Message payload exceeds maximum allowed by protocol!\n", FL);
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	/*
	 * Make sure enough time between last receive and this send
	 * In fact, because of the timeout on recv(), this is pretty small, but
	 * we take the safe road and wait the whole of p3min plus whatever
	 * delay happened before
	 */
	sleeptime = d_l2_conn->diag_l2_p3min;
	if (sleeptime > 0)
		diag_os_millisleep(sleeptime);

	offset = 0;

	//if L1 requires headerless data, send directly :
	if (d_l2_conn->diag_link->l1flags & DIAG_L1_DATAONLY) {
		rv = diag_l1_send (d_l2_conn->diag_link->l2_dl0d, NULL,
				msg->data, msg->len, d_l2_conn->diag_l2_p4min);
		return rv? diag_iseterr(rv):0;
	}

	/* add ISO9141-2 header */
	buf[offset++] = 0x68; //defined by spec;
	buf[offset++] = 0x6A; //defined by spec;
	buf[offset++] = dp->srcaddr;

	// Now copy in data
	memcpy(&buf[offset], msg->data, msg->len);
	offset += msg->len;

	// If the interface doesn't do ISO9141-2 checksum, add it in:
	if ((d_l2_conn->diag_link->l1flags & DIAG_L1_DOESL2CKSUM) == 0) {
		uint8_t curoff = (uint8_t) offset;
		buf[offset++] = diag_cks1(buf, curoff);
	}

	if (diag_l2_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, "l2_iso9141_send: ");
		diag_data_dump(stderr, buf, (size_t)offset);
		fprintf(stderr, "\n");
	}

	// Send it over the L1 link:
	rv = diag_l1_send (d_l2_conn->diag_link->l2_dl0d, NULL,
			buf, (size_t)offset, d_l2_conn->diag_l2_p4min);


	return rv? diag_iseterr(rv):0;
}


//_request: ret a new message if ok; NULL if failed
static struct diag_msg *
dl2p_iso9141_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
				  int *errval) {
	int rv;
	struct diag_msg *rmsg = NULL;

	rv = diag_l2_send(d_l2_conn, msg);
	if (rv < 0) {
		*errval = rv;
		return NULL;
	}

	/* And wait for response */
	rv = dl2p_iso9141_int_recv(d_l2_conn, d_l2_conn->diag_l2_p2max + RXTOFFSET);
	if ((rv >= 0) && d_l2_conn->diag_msg) {
		/* OK */
		rmsg = d_l2_conn->diag_msg;
		d_l2_conn->diag_msg = NULL;
	}
	else {
		/* Error */
		*errval = DIAG_ERR_TIMEOUT;
		rmsg = NULL;
	}

	return rmsg;
}

const struct diag_l2_proto diag_l2_proto_iso9141 = {
	DIAG_L2_PROT_ISO9141,
	"ISO9141",
	DIAG_L2_FLAG_FRAMED,
	dl2p_iso9141_startcomms,
	dl2p_iso9141_stopcomms,
	dl2p_iso9141_send,
	dl2p_iso9141_recv,
	dl2p_iso9141_request,
	NULL
};
