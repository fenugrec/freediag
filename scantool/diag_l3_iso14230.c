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
 * L7, ISO14230-3 KeyWord 2000 protocol
 * routines
 */

#include <string.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_iso14230.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_l3_iso14230.h"

CVSID("$Id$");


static const char *l3_iso14230_sidlookup(const int id);
static const char *l3_iso14230_neglookup(const int id);


/*
 * We'll use a big switch statement here, and rely on the compiler
 * to make it efficient
 * it assumes the packet in *buf has no more headers ! i.e. buf[0]
 * is the service ID byte.
 */
char *
diag_l3_iso14230_decode_response(struct diag_msg *msg,
char *buf, const size_t bufsize)
{
	//TODO : change the chars for uint8_t, probably safer:
	// we do a few >< comparisons on chars... which are signed
	char buf2[80];

	switch (*msg->data)
	{
	default:
		// XXX aren't we comparing *signed* chars here ??
		// (the responses to StartComm, StopComm and AccessTimingP are >0x80 )
		if ((msg->data[0] >= 0x50) && (msg->data[0] <= 0x7e))
		{
			snprintf(buf, bufsize, "Positive response, %s ",
					l3_iso14230_sidlookup(msg->data[0] & ~0x40));
			if ((msg->data[0] & ~0x40) == DIAG_KW2K_SI_REID)
			{
				snprintf(buf2, sizeof(buf2), "identOption 0x%02x", msg->data[1]);
				smartcat(buf, bufsize, buf2);
			}
			else if ((msg->data[0] & ~0x40) == DIAG_KW2K_SI_RDDBLI)
			{
				snprintf(buf2, sizeof(buf2), "RLI 0x%02x", msg->data[1]);
				smartcat(buf, bufsize, buf2);
			}
		}
		else
		{
			snprintf(buf, bufsize, "Unknown_response_code 0x%x",
					msg->data[0]);
		}
		break;
	case DIAG_KW2K_RC_SCRPR:
		snprintf(buf, bufsize, "StartCommunications_OK");
		break;
	case DIAG_KW2K_RC_SPRPR:
		snprintf(buf, bufsize, "StopCommunications_OK");
		break;
	case DIAG_KW2K_RC_ATPPR:
		snprintf(buf, bufsize, "AccessTimingParameters_OK");
		break;

	case DIAG_KW2K_RC_NR:
		if (msg->len < 3)
		{
			snprintf(buf, bufsize,  "General_Error, no response code");
		}
		else
		{

			snprintf(buf, bufsize,  "General_Error, Requested_SID_%s ",
						l3_iso14230_sidlookup(msg->data[1]));

			snprintf(buf2, sizeof(buf2), "Error_%s",
						l3_iso14230_neglookup(msg->data[2]));

			/* Don't overflow our buffers. */

			smartcat(buf, bufsize, buf2 );
		}
		break;
	}

	return(buf);
}


static int
diag_l3_iso14230_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	struct diag_l2_conn *d_conn;
	uint8_t buf[32];
	struct diag_msg newmsg;
	uint8_t cksum;
	int i;

	/* Get l2 connection info */
	d_conn = d_l3_conn->d_l3l2_conn;

	if (diag_l3_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr,FLFMT "send %d bytes, l2 flags 0x%x\n",
			FL, msg->len,  d_l3_conn->d_l3l2_flags);
/*{int cnt; printf("[CJH] %s %d: ",__FILE__,__LINE__);
for(cnt=0;cnt<msg->len;cnt++) printf("0x%02x ",msg->data[cnt]);printf("\n");}*/

	/* Note source address on 1st send */
	if (d_l3_conn->src == 0)
		d_l3_conn->src = msg->src;
	//TODO : clarify the following, do we need to check flags etc.

	if (1)// CJH (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DATA_ONLY)
	{
		// L2 does framing, adds addressing and CRC, so do nothing
		rv = diag_l2_send(d_conn, msg);
	}
	else
	{
		/* Put data in buffer */
		memcpy(&buf[3], msg->data, msg->len);

		/*
		 * Add addresses. Were using default addresses here, suitable
		 * for ISO9141 and one of the J1850 protocols. However our
		 * L2 J1850 code does framing for us, so thats no issue.
		 */
		if (msg->data[0] >= 0x40)
		{
			/* Response */
			buf[0] = 0x48;
			buf[1] = 0x6B;  /* We chose to overide msg->dest */
		}
		else
		{
			/* Request */
			buf[0] = 0x68;
			buf[1] = 0x6A;  /* We chose to overide msg->dest */
		}
		buf[2] = msg->src;

		/*
		 * We do an ISO type checksum as default. This wont be
		 * right for J1850, but that is handled by our L2 J1850 code
		 * so thats no issue.
		 */
		if ( ((d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DOESCKSUM)==0)
			&& ((d_l3_conn->d_l3l1_flags & DIAG_L1_DOESL2CKSUM)==0))
		{
			/* No one else does checksum, so we do it */
			for (i=0, cksum = 0; i<msg->len+3; i++)
				cksum += buf[i];
			buf[msg->len+3] = cksum;

			newmsg.len = msg->len + 4; /* Old len + hdr + cksum */
		}
		else
			newmsg.len = msg->len + 3;	/* Old len + hdr */

		newmsg.data = buf;

		/* And send message */
		rv = diag_l2_send(d_conn, &newmsg);
	}
	return(rv);
}


/*
 * RX callback, called as data received from L2. If we get a full message,
 * call L3 callback routine
 */
 // CJH: that comment is simply from the j1979 code! Not sure how much of this we need
static void
diag_l3_rcv_callback(void *handle, struct diag_msg *msg)
{
// CJH: All of this is copied from j1979. No idea if its right for here!
	/*
	 * Got some data from L2, build it into a L3 message, if
	 * message is complete call next layer callback routine
	 */
	struct diag_l3_conn *d_l3_conn = (struct diag_l3_conn *)handle;
	char buffer[200];

	if (diag_l3_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr,FLFMT "rcv_callback for %d bytes fmt 0x%x conn rxoffset %d\n",
			FL, msg->len, msg->fmt, d_l3_conn->rxoffset);
	}
	if (diag_l3_iso14230_decode_response(msg, buffer, sizeof(buffer)))
	{
	 fprintf(stderr, "DECODED: %s\n",buffer);
	}

	if (msg->fmt & DIAG_FMT_FRAMED)
	{
		if ( (msg->fmt & DIAG_FMT_DATAONLY) == 0)
		{
			/* Remove header etc */
			struct diag_msg *tmsg;
			/*
			 * Have to remove L3 header and checksum from each response
			 * on the message
			 *
			 * XXX checksum check needed ...
			 */
			for (tmsg = msg ; tmsg; tmsg = tmsg->next)
			{
				tmsg->fmt |= DIAG_FMT_ISO_FUNCADDR;
				tmsg->fmt |= DIAG_FMT_DATAONLY;
				tmsg->type =  tmsg->data[0];
				tmsg->dest = tmsg->data[1];
				tmsg->src =  tmsg->data[2];
				/* Length sanity check */
				if (tmsg->len >= 4)
				{
					tmsg->data += 3;
					tmsg->len -= 4;	/* Remove header and checksum */
				}
			}
		}
		else
		{
			/* XXX check checksum */

		}
		/* And send data upward if needed */
		if (d_l3_conn->callback)
			d_l3_conn->callback(d_l3_conn->handle, msg);
	}
	else
	{
	printf("[CJH] WARNING!! Should we be here? %s %d\n",__FUNCTION__,__LINE__);
			/* Add data to the receive buffer on the L3 connection */
			memcpy(&d_l3_conn->rxbuf[d_l3_conn->rxoffset],
				msg->data, msg->len);
			d_l3_conn->rxoffset += msg->len;
		}
}


static int
diag_l3_iso14230_recv(struct diag_l3_conn *d_l3_conn, int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle)
{
	int rv;
	struct diag_msg *msg;
	int tout;
	int state;

#define ST_STATE1 1
#define ST_STATE2 2
#define ST_STATE3 3
#define ST_STATE4 4

/* XXX stuff do do here */

	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_FRAMED)
	{
		/*
		 * L2 does framing stuff , which means we get one message
		 * with nicely formed frames
		 */
		state = ST_STATE4;
		tout = timeout;
	}
	else
	{
		state = ST_STATE1;
		tout = 0;
	}

	/* Store the callback routine for use if needed */
	d_l3_conn->callback = rcv_call_back;
	d_l3_conn->handle = handle;

	/*
	 * This works by doing a read with a timeout of 0, to collect
	 * any data that was present on the link, if no messages complete
	 * then read with the normal timeout, then read with a timeout
	 * of p4max ms until no more data is left (or timeout), then call
	 * the callback routine (if there is a message complete)
	 */
	while (1)
	{
		/* State machine for setting timeout values */
		if (state == ST_STATE1)
			tout = 0;
		else if (state == ST_STATE2)
			tout = timeout;
		else if (state == ST_STATE3)
			tout = 5; /* XXX should be p4max */
		else if (state == ST_STATE4)
			tout = timeout;

		if (diag_l3_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr,FLFMT "recv state %d tout %d\n",
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

		if (rv == DIAG_ERR_TIMEOUT)
		{
			if ( (state == ST_STATE3) || (state == ST_STATE4) )
			{
				/* Finished */
				break;
			}
			if ( (state == ST_STATE1) && (d_l3_conn->msg == NULL) )
			{
				/*
				 * Try again, with real timeout
				 * (and thus sleep)
				 */
				state = ST_STATE2;
				tout = timeout;
				continue;
			}
		}

		if (state != ST_STATE4)
		{
			/* Process the data into messages */
printf("[CJH] %s %d: No process_data function!\n",__FUNCTION__,__LINE__);
			//diag_l3_j1979_process_data(d_l3_conn);

			if (diag_l3_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr,FLFMT "recv process_data called, msg %p rxoffset %d\n",
					FL, d_l3_conn->msg,
					d_l3_conn->rxoffset);

			/*
			 * If there is a full message, remove it, call back
			 * the user call back routine with it, and free it
			 */
			msg = d_l3_conn->msg;
			if (msg)
			{
				d_l3_conn->msg = msg->next;

				if ( (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_DATA_ONLY) == 0)
				{
					/* Strip hdr/checksum */
					msg->data += 3;
					msg->len -= 4;
				}
				rcv_call_back(handle, msg);
				if (msg->len)
					free (msg->data);
				free (msg);
				rv = 0;
				/* And quit while we are ahead */
				break;
			}
		}
		/* We do not have a complete message (yet) */
		if (state == ST_STATE2)
		{
			/* Part message, see if we get some more */
			state = ST_STATE3;
		}
		if (state == ST_STATE1)
		{
			/* Ok, try again with proper timeout */
			state = ST_STATE2;
		}
		if ((state == ST_STATE3) || (state == ST_STATE4))
		{
			/* Finished, we only do read once in this state */
			break;
		}
	}

	return(rv);
}



static char *
diag_l3_iso14230_decode(UNUSED(struct diag_l3_conn *d_l3_conn),
struct diag_msg *msg, char *buf, size_t bufsize)
{
	if (msg->data[0] & 0x40)
		snprintf(buf, bufsize, "ISO14230 response ");
	else
		snprintf(buf, bufsize, "ISO14230 request ");

	return(buf);
}


/*
 * Timer routine, called with time (in ms) since the "timer" value in
 * the L3 structure
 * This handles only P3 timeout (keepalive => TesterPresent)
 */
static void
diag_l3_iso14230_timer(struct diag_l3_conn *d_l3_conn, int ms)
{
	struct diag_msg msg;
	uint8_t data[6];

	/* ISO14230 ? XXX J1979 needs keepalive at least every 5 seconds, we use 3.5s */
	// "keepalive" corresponds to P3max, defined as 5000 ms by iso9141 and
	// ISO14230 (unless it has been adjusted with AccessTimingParameters)
	//P3 starts withthe last bit of all responses from the vehicle.
	if (ms < ISO14230_KEEPALIVE)
		return;

	/* Does L2 do keepalive for us ? */
	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_KEEPALIVE)
		return;

	/* OK, do keep alive on this connection */

	if (diag_l3_debug & DIAG_DEBUG_TIMER)
	{
		/* XXX Not async-signal-safe */
		fprintf(stderr, FLFMT "P3 timeout impending for %p %d ms\n",
				FL, d_l3_conn, ms);
	}

	msg.data = data;
	msg.len = 1;
	data[0] = DIAG_KW2K_SI_TP ;

	/*
	 * And set the source address, if no sends have happened, then
	 * the src address will be 0, so use the default used in J1979
	 */
	if (d_l3_conn->src)
		msg.src = d_l3_conn->src;
	else
		msg.src = 0xF1;	 /* Default as used in SAE J1979 */

	/* Send it */
	(void)diag_l3_send(d_l3_conn, &msg);

	/* Get and ignore the response */
	(void)diag_l3_recv(d_l3_conn, 50, NULL, NULL);
	// TODO : actually check if it responded positively !
	// And signal to close the connection if it didn't respond...

	return;
}


/*
 * Table of english descriptions of the ISO14230 SIDs
 */
static const struct
{
	const int id;
	const char *service;
} sids[] =
{
	{DIAG_KW2K_SI_STADS, 	"startDiagnosticSession"},
	{DIAG_KW2K_SI_ER, 	"ecuReset"},
	{DIAG_KW2K_SI_RDFFD, 	"readFreezeFrameData"},
	{DIAG_KW2K_SI_RDTC, 	"readDiagnosticTroubleCodes"},
	{DIAG_KW2K_SI_CDI, 	"clearDiagnosticInformation"},
	{DIAG_KW2K_SI_RDSODTC, 	"readStatusOfDiagnosticTroubleCodes"},
	{DIAG_KW2K_SI_RDTCBS, 	"readDiagnosticTroubleCodesByStatus"},
	{DIAG_KW2K_SI_REID, 	"readEcuId"},
	{DIAG_KW2K_SI_STODS,	"stopDiagnosticSession"},
	{DIAG_KW2K_SI_RDDBLI, 	"readDataByLocalId"},
	{DIAG_KW2K_SI_RDDBCI, 	"readDataByCommonId"},
	{DIAG_KW2K_SI_RDMBA, 	"readMemoryByAddress"},
	{DIAG_KW2K_SI_SRDT, 	"stopRepeatedDataTransmission"},
	{DIAG_KW2K_SI_SDR, 	"setDataRates"},
	{DIAG_KW2K_SI_SA, 	"securityAccess"},
	{DIAG_KW2K_SI_DDLI, 	"dynamicallyDefineLocalId"},
	{DIAG_KW2K_SI_WRDBCI, 	"writeDataByCommonId"},
	{DIAG_KW2K_SI_IOCBCI,	"inputOutputControlByCommonId"},
	{DIAG_KW2K_SI_IOCBLI, 	"inputOutputControlByLocalId"},
	{DIAG_KW2K_SI_STARBLI, 	"startRoutineByLocalID"},
	{DIAG_KW2K_SI_STORBLI, 	"stopRoutineByLocalID"},
	{DIAG_KW2K_SI_RRRBLI, 	"requestRoutineResultsByLocalId"},
	{DIAG_KW2K_SI_RD, 	"requestDownload"},
	{DIAG_KW2K_SI_RU, 	"requestUpload"},
	{DIAG_KW2K_SI_TD, 	"transfer data"},
	{DIAG_KW2K_SI_RTE, 	"request transfer exit"},
	{DIAG_KW2K_SI_STARBA, 	"startRoutineByAddress"},
	{DIAG_KW2K_SI_STORBA, 	"stopRoutineByAddress"},
	{DIAG_KW2K_SI_RRRBA,	"requestRoutineResultsByAddress"},
	{DIAG_KW2K_SI_WRDBLI, 	"writeDataByLocalId"},
	{DIAG_KW2K_SI_WRMBA, 	"writeMemoryByAddress"},
	{DIAG_KW2K_SI_TP, 	"testerPresent"},
	{DIAG_KW2K_SI_ESC,	"EscCode"},
	{DIAG_KW2K_SI_SCR, 	"startCommunication"},
	{DIAG_KW2K_SI_SPR, 	"stopCommunication"},
	{DIAG_KW2K_SI_ATP, 	"accessTimingParameters"},
};

static const char *l3_iso14230_sidlookup(const int id)
{
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(sids); i++)
		if (sids[i].id == id)
			return sids[i].service;

	return "Unknown SID";
}


/*
 * Table of english descriptions of the ISO14230 NegResponse codes
 */
static const struct
{
	const int id;
	const char *response;
} negresps[] =
{
	{DIAG_KW2K_RC_GR,	"generalReject"},
	{DIAG_KW2K_RC_SNS,	"serviceNotSupported"},
	{DIAG_KW2K_RC_SFNS_IF,	"subFunctionNotSupported-Invalid Format"},
	{DIAG_KW2K_RC_B_RR,	"busy-repeatRequest"},
	{DIAG_KW2K_RC_CNCORSE,	"conditionsNoteCorrectOrRequestSequenceError"},
	{DIAG_KW2K_RC_RNC,	"routineNotCompleteOrServiceInProgress"},
	{DIAG_KW2K_RC_ROOT,	"requestOutOfRange"},
	{DIAG_KW2K_RC_SAD_SAR,	"securityAccessDenied-securityAccessRequested"},
	{DIAG_KW2K_RC_IK,	"invalidKey"},
	{DIAG_KW2K_RC_ENOA,	"exceedNumberOfAttempts"},
	{DIAG_KW2K_RC_RTDNE,	"requiredTimeDelayNotExpired"},
	{DIAG_KW2K_RC_DNA,	"downloadNotAccepted"},
	{DIAG_KW2K_RC_IDT,	"improperDownloadType"},
	{DIAG_KW2K_RC_CNDTSA, 	"canNotDownloadToSpecifiedAddress"},
	{DIAG_KW2K_RC_CNDNOBR,	"canNotDownloadNumberOfBytesRequested"},
	{DIAG_KW2K_RC_UNA,	"uploadNotAccepted"},
	{DIAG_KW2K_RC_IUT,	"improperUploadType"},
	{DIAG_KW2K_RC_CNUFSA,	"canNotUploadFromSpecifiedAddress"},
	{DIAG_KW2K_RC_CNUNOBR,	"canNotUploadNumberOfBytesRequested"},
	{DIAG_KW2K_RC_TS,	"transferSuspended"},
	{DIAG_KW2K_RC_TA,	"transferAborted"},
	{DIAG_KW2K_RC_IAIBT,	"illegalAddressInBlockTransfer"},
	{DIAG_KW2K_RC_IBCIBT,	"illegalByteCountInBlockTransfer"},
	{DIAG_KW2K_RC_IBTT,	"illegalBlockTrasnferType"},
	{DIAG_KW2K_RC_BTCDE,	"blockTransferDataChecksumError"},
	{DIAG_KW2K_RC_RCR_RP,	"requestCorrectyRcvd-RspPending"},
	{DIAG_KW2K_RC_IBCDBT,	"incorrectByteCountDuringBlockTransfer"},
	{DIAG_KW2K_RC_SNSIADS,	"serviceNotSupportedInActiveDiagnosticMode//Mfg-Specific"},
	{0, 			NULL},
};

static const char *l3_iso14230_neglookup(const int id)
{
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(negresps); i++)
		if (negresps[i].id == id)
			return negresps[i].response;

	return "Unknown Response code";
}


const diag_l3_proto_t diag_l3_iso14230 = {
	"ISO14230", diag_l3_base_start, diag_l3_base_stop,
	diag_l3_iso14230_send, diag_l3_iso14230_recv, NULL,
	diag_l3_iso14230_decode, diag_l3_iso14230_timer
};
