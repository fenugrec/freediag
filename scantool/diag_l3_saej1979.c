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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * L3 code to do SAE J1979 messaging
 *
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_l3_saej1979.h"
#include "utlist.h"


/* internal data used by each connection */
struct l3_j1979_int {
	uint8_t src;	//source address ("tester ID")

	/* Received data buffer, and offset into it */
	uint8_t rxbuf[MAXRBUF];
	int	rxoffset;
};

/*
 * Return the expected J1979 packet length for a given mode byte
 * This includes *only* up to 7 data bytes (headers and checksum are stripped and
 * generated in L2)
 *
 * XXX DOESN'T COPE WITH in-frame-response - will break check routine as well
 * Also doesn't support 15765 (CAN) which has more modes.
 *
 * Get this wrong and all will fail, it's used to frame the incoming messages
 * properly
 */
static int diag_l3_j1979_getlen(uint8_t *data, int len)
{
	static const int rqst_lengths[] = { -1, 2, 3, 1, 1, 2, 2, 1, 7, 2 };
	int rv;
	uint8_t mode;

	if (len < 1)	/* Need at least 1 data byte*/
		return diag_iseterr(DIAG_ERR_INCDATA);

	mode = data[0];

	//J1979 specifies 9 modes (0x01 - 0x09) except with iso15765 (CAN) which has 0x0A modes.

	if (mode > 0x49)
		return diag_iseterr(DIAG_ERR_BADDATA);

	if (mode < 0x41) {
		if (mode <= 9)
			return rqst_lengths[mode];
		return diag_iseterr(DIAG_ERR_BADDATA);
	}

	rv = DIAG_ERR_BADDATA;

	//Here, all modes < 0x0A are taken care of.
	//Modes > 0x40 are responses and therefore need specific treatment
	//data[1] contains the PID / TID number.
	switch (mode) {
	case 0x41:
	case 0x42:		//almost identical modes except PIDS 1,2
		// Note : mode 2 responses will be +1 longer because of the frame_no byte.
		if ((data[1] & 0x1f) == 0) {
			/* return supported PIDs */
			rv=6;	//6.1.2.2
			break;
		}

		switch (data[1]) {
		case 1:	//Status. Only with service 01 (mode 0x41)
			if (mode==0x42)
				rv=DIAG_ERR_BADDATA;
			else
				rv=6;
			break;
		case 2:	//request freeze DTC. Only with Service 02 (mode=0x42)
			if (mode==0x41)
				rv = DIAG_ERR_BADDATA;
			else
				rv=4;
			break;
		case 3:
			rv = 4;
			break;
		case 0x04:
		case 0x05:
			rv=3;
			break;
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x55:
		case 0x56:
		case 0x57:
		case 0x58:
			//XXX For PIDs 0x06 thru 0x09 and 0x55-0x58, there may be an additional data byte based on PID 0x13 / 0x1D results
			// (presence of a bank3 O2 sensor. Not implemented...
			rv=3;
			break;
		case 0x0A:
		case 0x0B:	//IMAP
			rv=3;
			break;
		case 0x0C:	//RPM
			rv=4;
			break;
		case 0x0D:	//VSS
		case 0x0E:
		case 0x0F:	//IAT
			rv=3;
			break;
		case 0x10:	//MAF
			rv=4;
			break;
		case 0x11:	//TPS
		case 0x12:
		case 0x13:	//O2Sloc
			rv = 3;
			break;
		case 0x14:	//0x14-0x1B:O2 stuff
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
			rv = 4;
			break;
		case 0x1C:	//OBD
		case 0x1D:	//O2Sloc
		case 0x1E:
			rv = 2;
			break;
		case 0x1F:	//RUNTM
			rv = 4;
			break;
		//TODO : PIDS 0x21 etc
		default:
			/* Sometime add J2190 support (PID>0x1F) */
			rv = DIAG_ERR_BADDATA;
			break;
		}	//PIDs for modes 1 and 2
		//For mode 2 responses, actual length is 1 byte longer than Mode 1 resps because of Frame No
		if (mode == 0x42 && rv) rv += 1;
		break;
	case 0x43:
		rv=7;	//6.3.2.4
		break;
	case 0x44:
		rv = 1;	//6.4.2.2
		break;
	case 0x45:
		if ((data[1] & 0x1f) == 0)
			rv = 7;		// Read supported TIDs, 6.5.2.2
		else if (data[1] <= 4)
			rv=4;			//J1979 sec 6.5.2.4 : conditional TIDs.
		else
			rv = 6;		//Request TID result
		break;
	case 0x46:
		//6.6.2.2 supported PIDs : len=7
		//6.6.2.4 : 7 bytes; last 2 are "conditional" in meaning only, always present ??
	case 0x47:	//6.7.2.2
	case 0x48:	//6.8.2.1
		rv = 7;
		break;
	case 0x49:	//6.9.1
		if ((data[1] & 0x1f) ==0) {
			rv=7;	//supported INFOTYPES
		} else if (data[1] & 1) {
			//INFOTYPE is odd:
			rv=7;
		} else {
			//even
			rv=3;
		}
		break;
	default:
		break;
	}
	return rv;
}


/*
 * Send a J1979 packet - we know the length (from looking at the data)
 * since we're L3, we assume msg->data[0] is the mode (service id), i.e. there
 * aren't any headers.
 */
static int
diag_l3_j1979_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	struct diag_l2_conn *d_conn;
	struct l3_j1979_int *l3i = d_l3_conn->l3_int;
//	uint8_t buf[32];

	/* Get l2 connection info */
	d_conn = d_l3_conn->d_l3l2_conn;

	if (diag_l3_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,FLFMT "send %d bytes, l2 flags 0x%X\n",
			FL, msg->len, d_l3_conn->d_l3l2_flags);

	//and make sure src address was set in msg:
	if (! msg->src)
		msg->src=0xF1;

	/* Note source address on 1st send */
	if (l3i->src == 0)
		l3i->src = msg->src;



	//we can't set destination address because it's L2-defined.
	// (iso14230 : dest = 0x33 phys;
	// (iso9141 + J1850 : dest = 0x6A )


	/* L2 does framing, adds addressing and CRC, so do nothing else*/
	rv = diag_l2_send(d_conn, msg);

	return rv;
}

/*
 * RX callback, called as data received from L2. If we get a full message,
 * call L3 callback routine
 */
static void
diag_l3_rcv_callback(void *handle, struct diag_msg *msg)
{
	/*
	 * Got some data from L2, build it into a L3 message, if
	 * message is complete call next layer callback routine
	 */
	struct diag_l3_conn *d_l3_conn = (struct diag_l3_conn *)handle;
	struct l3_j1979_int *l3i = d_l3_conn->l3_int;

	if (diag_l3_debug & DIAG_DEBUG_READ)
		fprintf(stderr,FLFMT "rcv_callback for %d bytes fmt 0x%X conn rxoffset %d\n",
			FL, msg->len, msg->fmt, l3i->rxoffset);

	if (msg->fmt & DIAG_FMT_FRAMED) {
		/* Send data upward if needed */
		if (d_l3_conn->callback)
			d_l3_conn->callback(d_l3_conn->handle, msg);
	} else {
		/* Add data to the receive buffer on the L3 connection */
		memcpy(&l3i->rxbuf[l3i->rxoffset],
			msg->data, msg->len);		//XXX possible buffer overflow !
		l3i->rxoffset += msg->len;
	}
}


/*
 * Process_data() - this is the routine that works out the framing
 * of the data , if recv() was always called at the correct time and
 * in the correct way then we would have one and one complete message
 * received by the L2 code normally - however, we cant be sure of that
 * so we employ this algorithm
 *
 * Look at the data and try and work out the length of the message based
 * on the J1979 protocol. Returns length (excluding headers & checksum !)
 * VVVVVVVV
 * Note: J1979 doesn't specify particular checksums beyond what 9141 and 14230 already provide,
 * i.e. a J1979 message is maximum 7 bytes long except on CANBUS.
 * headers, address and checksum are handled and stripped at the l2 level (9141, 14230, etc);
 *
 * Another validity check is comparing message length (expected vs real); not implemented XXX?
 * Upper levels can also verify that the responses correspond to the requests (i.e. Service ID 0x02 -> 0x42 )
 * XXX Broken for the moment because I'm moving the header stuff completely out of L3
 *
 */
static void
diag_l3_j1979_process_data(struct diag_l3_conn *d_l3_conn)
{
	/* Process the received data into messages if complete */
	struct diag_msg *msg;
	int sae_msglen;
	struct l3_j1979_int *l3i = d_l3_conn->l3_int;

	while (l3i->rxoffset) {
		int badpacket=0;

		sae_msglen = diag_l3_j1979_getlen(l3i->rxbuf,
					l3i->rxoffset);	//set expected packet length based on SID + TID

		if (diag_l3_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr,FLFMT "process_data rxoffset is %d sae_msglen is %ld\n",
				FL, l3i->rxoffset, (long)sae_msglen);
			fprintf(stderr,FLFMT "process_data hex data is ",
				FL);
			diag_data_dump(stderr, l3i->rxbuf, l3i->rxoffset -1);
			fprintf(stderr,"\n");
		}

		if (sae_msglen < 0 || sae_msglen > 255) {
			if (sae_msglen == DIAG_ERR_INCDATA) {
				/* Not enough data in this frame, this isn't catastrophic */
				return;
			} else {
				/* Duff data received, bad news ! */
				badpacket = 1;
			}
		}

		if (badpacket || (sae_msglen <= l3i->rxoffset )) {

			/* Bad packet, or full packet, need to tell user */
			uint8_t *data = NULL;

			if (diag_calloc(&msg, 1)) {
				/* Stuffed, no memory, cant do anything */
				return;
			}

			if (!badpacket)
				if (diag_malloc(&data, (size_t)sae_msglen)) {
					free(msg);
					return;
				}

			if (badpacket || (data == NULL)) {
				/* Failure indicated by zero len msg */
				msg->data = NULL;
				msg->len = 0;
			} else {
				msg->fmt = DIAG_FMT_ISO_FUNCADDR;
				// XXX This won't do at all !
				//msg->type = l3i->rxbuf[0];	//nobody checks this
				msg->dest = l3i->rxbuf[1];
				msg->src = l3i->rxbuf[2];
				/* Copy in J1979 part of message */
				memcpy(data, &l3i->rxbuf[3], (size_t)(sae_msglen - 4));
				/* remove whole message from rx buf */
				memmove(l3i->rxbuf,
					&l3i->rxbuf[sae_msglen],
					(size_t)sae_msglen);

				l3i->rxoffset -= sae_msglen;

				msg->data = data;
				msg->len = (uint8_t) sae_msglen - 4;
			}

			msg->rxtime = diag_os_getms();

			/* Add it to the list */
			LL_CONCAT(d_l3_conn->msg, msg);
			free(data);
			free(msg);	//we don't use diag_freemsg() since we alloc'ed it manually
			if (badpacket) {
				/* No point in continuing */
				break;
			}
		} else {
			/* Need some more data */
			break;
		}
	}
}

/*
 * Receive a J1979 frame (building it as we get small amounts of data)
 *
 * - timeout expiry will cause return before complete packet
 *
 * Successful packet receive will call the callback routine with the message
 * XXX currently broken because I'm halfway through modifying _process_data
 */
static int
diag_l3_j1979_recv(struct diag_l3_conn *d_l3_conn, unsigned int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle)
{
	int rv;
	struct diag_msg *msg;
	unsigned int tout;
	int state;
//State machine:
#define ST_STATE1 1	// timeout=0 to get data already in buffer
#define ST_STATE2 2
#define ST_STATE3 3
#define ST_STATE4 4

	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_FRAMED) {
		/*
		 * L2 does framing stuff , which means we get one message
		 * with nicely formed frames
		 */
		state = ST_STATE4;
	} else {
		state = ST_STATE1;
	}

	/* Store the callback routine for use if needed */
	d_l3_conn->callback = rcv_call_back;
	d_l3_conn->handle = handle;

	/*
	 * This works by doing a read with a timeout of 0, to collect
	 * any data that was present on the link, if no messages complete
	 * then read with the normal timeout, then read with a timeout
	 * of p4max ms until no more data is left (or timeout), then call
	 * the callback routine (if there is a complete message)
	 */
	while (1) {
		/* State machine for setting timeout values */
		switch (state) {
			case ST_STATE1:
				tout = 0;
				break;
			case ST_STATE2:
				tout = timeout;
				break;
			case ST_STATE3:
				tout = 5; /* XXX should be p4max */
				break;
			case ST_STATE4:
				tout = timeout;
				break;
			default:
				break;
		}

		if (diag_l3_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,FLFMT "recv state %d tout %u\n",
				FL, state, tout);

		/*
		 * Call L2 receive, L2 will build up the datapacket and
		 * call the callback routine if needed, we use a timeout
		 * of zero to see if data exists *now*,
		 */
		rv = diag_l2_recv(d_l3_conn->d_l3l2_conn, tout,
			diag_l3_rcv_callback, (void *)d_l3_conn);

		if (diag_l3_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,FLFMT "recv returns %d\n",
				FL, rv);

		if ((rv < 0) && (rv != DIAG_ERR_TIMEOUT))
			break;		/* Some nasty failure */

		if (rv == DIAG_ERR_TIMEOUT) {
			if ( (state == ST_STATE3) || (state == ST_STATE4) ) {
				/* Finished */
				break;
			}
			if ( (state == ST_STATE1) && (d_l3_conn->msg == NULL) ) {
				/*
				 * Try again, with real timeout
				 * (and thus sleep)
				 */
				state = ST_STATE2;
				tout = timeout;
				continue;
			}
		}

		if (state != ST_STATE4) {
			/* Process the data into messages */
			diag_l3_j1979_process_data(d_l3_conn);

			if (diag_l3_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr,FLFMT "recv process_data called, msg %p\n",
					FL, (void *)d_l3_conn->msg);

			/*
			 * If there is a full message, remove it, call back
			 * the user call back routine with it, and free it
			 */
			msg = d_l3_conn->msg;
			if (msg) {
				d_l3_conn->msg = msg->next;

				rcv_call_back(handle, msg);
				diag_freemsg(msg);
				rv = 0;
				/* And quit while we are ahead */
				break;
			}
		}
		/* We do not have a complete message (yet) */
		if (state == ST_STATE2) {
			/* Part message, see if we get some more */
			state = ST_STATE3;
		}
		if (state == ST_STATE1) {
			/* Ok, try again with proper timeout */
			state = ST_STATE2;
		}
		if ((state == ST_STATE3) || (state == ST_STATE4)) {
			/* Finished, we only do read once in this state */
			break;
		}
	}

	return rv;
}



/*
 * This is called without the ADDR_ADDR_1 on it, ie it contains
 * just the SAEJ1979 data
 * Returns a string with the description + data associated with a J1979 message.
 * Doesn't do any data scaling / conversion.
 */

void
diag_l3_j1979_decode(UNUSED(struct diag_l3_conn *d_l3_conn),
struct diag_msg *msg, char *buf, size_t bufsize)
{
	unsigned i, j;

	char buf2[80];

	char area;	//for DTCs

	if (msg->data[0] & 0x40)
		snprintf(buf, bufsize, "J1979 response ");
	else
		snprintf(buf, bufsize, "J1979 request ");

	switch (msg->data[0]) {
		case 0x01:
			snprintf(buf2, sizeof(buf2), "Mode 1 PID 0x%02X", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x41:
			snprintf(buf2, sizeof(buf2),"Mode 1 Data: PID 0x%02X ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x02:
			snprintf(buf2, sizeof(buf2), "Mode 2 PID 0x%02X Frame 0x%02X", msg->data[1],
				msg->data[2]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x42:
			snprintf(buf2, sizeof(buf2),"Mode 2 FreezeFrame Data: PID 0x%02X Frame 0x%02X ",
				msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf2);
			for (i=3; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x03:
			snprintf(buf2, sizeof(buf2),"Mode 3 (Powertrain DTCs)");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x07:
			snprintf(buf2, sizeof(buf2),
				"Request Non-Continuous Monitor System Test Results");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x47:
			snprintf(buf2, sizeof(buf2), "Non-Continuous Monitor System ");
			smartcat(buf, bufsize, buf2);
			/* Fallthru */
		case 0x43:
			snprintf(buf2, sizeof(buf2),"DTCs: ");
			smartcat(buf, bufsize, buf2);
			for (i=0, j=1; i<3; i++, j+=2) {
				if ((msg->data[j]==0) && (msg->data[j+1]==0))
					continue;

				switch ((msg->data[j] >> 6) & 0x03) {
					case 0:
						area = 'P';
						break;
					case 1:
						area = 'C';
						break;
					case 2:
						area = 'B';
						break;
					case 3:
						area = 'U';
						break;
					default:
						fprintf(stderr, "Illegal msg->data[%d] value\n", j);
						area = 'X';
						break;
				}
				snprintf(buf2, sizeof(buf2), "%c%02X%02X  ", area, msg->data[j] & 0x3f,
					msg->data[j+1]&0xff);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x04:
			snprintf(buf2, sizeof(buf2), "Clear DTCs");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x44:
			snprintf(buf2, sizeof(buf2), "DTCs cleared");
			smartcat(buf, bufsize, buf2);
			break;
		case 0x05:
			snprintf(buf2, sizeof(buf2), "Oxygen Sensor Test ID 0x%02X Sensor 0x%02X",
					msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x45:
			snprintf(buf2, sizeof(buf2), "Oxygen Sensor TID 0x%02X Sensor 0x%02X ",
				msg->data[1], msg->data[2]);
			smartcat(buf, bufsize, buf);
			for (i=3; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x06:
			snprintf(buf2, sizeof(buf2), "Onboard monitoring test request TID 0x%02X", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x46:
			snprintf(buf2, sizeof(buf2),"Onboard monitoring test result TID 0x%02X ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x08:
			snprintf(buf2, sizeof(buf2), "Request control of onboard system TID 0x%02X", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x48:
			snprintf(buf2, sizeof(buf2), "Control of onboard system response TID 0x%02X ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		case 0x09:
			snprintf(buf2, sizeof(buf2), "Request vehicle information infotype 0x%02X", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			break;
		case 0x49:
			snprintf(buf2, sizeof(buf2), "Vehicle information infotype 0x%02X ", msg->data[1]);
			smartcat(buf, bufsize, buf2);
			for (i=2; i < msg->len; i++) {
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
			break;
		default:
			snprintf(buf2, sizeof(buf2),"UnknownType 0x%02X: Data Dump: ", msg->data[0]);
			smartcat(buf, bufsize, buf2);
			for (i=0; i < msg->len; i++)
			{
				snprintf(buf2, sizeof(buf2), "0x%02X ", msg->data[i]);
				smartcat(buf, bufsize, buf2);
			}
	}
	return;
}


//Send a service 1, pid 0 request and check for a valid reply.
//ret 0 if ok
static int diag_l3_j1979_keepalive(struct diag_l3_conn *d_l3_conn) {
	struct diag_msg msg={0};
	struct diag_msg *rxmsg;
	uint8_t data[6];
	int errval;
	struct l3_j1979_int *l3i = d_l3_conn->l3_int;

	/*
	 * Service 1 Pid 0 request is the SAEJ1979 idle message
	 * XXX Need to get the address bytes correct
	 */

	msg.data = data;
	msg.len = 2;
	data[0] = 1 ;		/* Mode 1 */
	data[1] = 0; 		/* Pid 0 */

	/*
	 * And set the source address, if no sends have happened, then
	 * the src address will be 0, so use the default used in J1979
	 */
	if (l3i->src)
		msg.src = l3i->src;
	else
		msg.src = 0xF1;		/* Default as used in SAE J1979 */

	/* Send it: using l3_request() makes sure the connection's timestamp is updated */
	rxmsg=diag_l3_request(d_l3_conn, &msg, &errval);

	if (rxmsg==NULL)
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	if (diag_l3_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr, FLFMT "keepalive : got %u bytes, %02X ...\n",
			FL, rxmsg->len, rxmsg->data[0]);

	/* Check if its a valid SID 1 PID 0 response */
	if ((rxmsg->len < 1) || (rxmsg->data[0] != 0x41)) {
		diag_freemsg(rxmsg);
		return diag_iseterr(DIAG_ERR_ECUSAIDNO);
	}

	diag_freemsg(rxmsg);
	return 0;

}

//_start : send service 1 pid 0 request (J1979 keepalive); according to SAE J1979 (p.7) :
// " IMPORTANT — All emissions-related OBD ECUs which at least support one of the services defined in this
//	document shall support service $01 and PID $00. Service $01 with PID $00 is defined as the universal
//	“initialisation/keep alive/ping” message for all emissions-related OBD ECUs. "
//That sounds like a sure-fire way to make sure we have a succesful connection to a J1979-compliant ECU.
int diag_l3_j1979_start(struct diag_l3_conn *d_l3_conn) {
	int rv;
	struct l3_j1979_int *l3i;

	assert(d_l3_conn != NULL);

	if (diag_calloc(&l3i, 1))
		return diag_iseterr(DIAG_ERR_NOMEM);

	d_l3_conn->l3_int = l3i;

	rv=diag_l3_j1979_keepalive(d_l3_conn);

	if (rv<0) {
		fprintf(stderr, FLFMT "J1979 Keepalive failed ! Try to disconnect and reconnect.\n", FL);
		free(l3i);
		return diag_iseterr(rv);
	}


	return 0;
}

/* Stop communications : nothing defined, other than letting the link timeout (L2 defined). */
int dl3_j1979_stop(struct diag_l3_conn *d_l3_conn) {
	assert(d_l3_conn != NULL);
	free(d_l3_conn->l3_int);
	return 0;
}

/*
 * Timer routine, called with time (in ms) since the "timer" value in
 * the L3 structure
 * return 0 if ok
 */
static int
diag_l3_j1979_timer(struct diag_l3_conn *d_l3_conn, unsigned long ms)
{
	int rv;
	int debug_l2_orig=diag_l2_debug;	//save debug flags; disable them for this procedure
	int debug_l1_orig=diag_l1_debug;

	/* J1979 needs keepalive at least every 5 seconds (P3), we use 3.5s */

	assert(d_l3_conn != NULL);
	if (ms < J1979_KEEPALIVE)
		return 0;

	/* Does L2 do keepalive for us ? */
	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_KEEPALIVE)
		return 0;

	/* OK, do keep alive on this connection */

	if (diag_l3_debug & DIAG_DEBUG_TIMER) {
		/* XXX Not async-signal-safe */
		fprintf(stderr, FLFMT "\nP3 timeout impending for %p %lu ms\n",
				FL, (void *)d_l3_conn, ms);
	}
	diag_l2_debug=0;	//disable
	diag_l1_debug=0;

	rv=diag_l3_j1979_keepalive(d_l3_conn);

	if (rv<0) {
		fprintf(stderr, FLFMT "J1979 Keepalive failed ! Try to disconnect and reconnect.\n", FL);
	}

	diag_l2_debug=debug_l2_orig;	//restore debug flags
	diag_l1_debug=debug_l1_orig;

	return rv;
}

const struct diag_l3_proto diag_l3_j1979 = {
	"SAEJ1979",
	diag_l3_j1979_start,
	dl3_j1979_stop,
	diag_l3_j1979_send,
	diag_l3_j1979_recv,
	NULL,	//ioctl
	diag_l3_base_request,
	diag_l3_j1979_decode,
	diag_l3_j1979_timer
};
