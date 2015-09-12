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
 * This doesn't duplicate what is provided by the L3 J1979 handler.
 * It provides iso14230 SID + response code string decoding,
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
	char buf2[80];

	switch (msg->data[0]) {
		//for these 3 SID's,
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
		if (msg->len < 3) {
			snprintf(buf, bufsize,  "General_Error, no response code");
		} else {

			snprintf(buf, bufsize,  "General_Error, Requested_SID_%s ",
						l3_iso14230_sidlookup(msg->data[1]));

			snprintf(buf2, sizeof(buf2), "Error_%s",
						l3_iso14230_neglookup(msg->data[2]));

			/* Don't overflow our buffers. */

			smartcat(buf, bufsize, buf2 );
		}
		break;
	default:
		//data[0] is either a "positive response service identifier (SNPR)"
		//or a NACK (0x7F); which we already checked... bit 6 is set on all
		// positive responses (14230-3 table 1)
		if (msg->data[0] & 0x40) {
			snprintf(buf, bufsize, "Positive response, %s ",
					l3_iso14230_sidlookup(msg->data[0] & ~0x40));
			switch (msg->data[0] & ~0x40) {
				case DIAG_KW2K_SI_REID:
					snprintf(buf2, sizeof(buf2), "identOption 0x%02X", msg->data[1]);
					smartcat(buf, bufsize, buf2);
					break;
				case DIAG_KW2K_SI_RDDBLI:
					snprintf(buf2, sizeof(buf2), "RLOCID 0x%02X", msg->data[1]);
					smartcat(buf, bufsize, buf2);
					break;
				default:
					break;
			}
			//TODO : add "custom" printout for every SID that has a local ID ?
			//or maybe expand the SID tables to include a field that indicates how many
			//extra bytes are meaningful. For example, SID 27 SecurityAccess

		} else {
			snprintf(buf, bufsize, "Unknown_response_code 0x%02X",
					msg->data[0]);
		}
		break;
	}

	return buf;
}


//return 0 if ok.
//the data in *msg should have no headers and no checksum, obviously.
//address information should be already set as well; this function just
//forwards the request to l2_send (presumably the user is using this L3
//proto over an iso14230 L2 proto. Running an iso14230 L3 over anything other
//than iso14230 is meaningless.
static int
diag_l3_iso14230_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg)
{
	int rv;
	struct diag_l2_conn *d_conn;

	/* Get l2 connection info */
	d_conn = d_l3_conn->d_l3l2_conn;

	if (diag_l3_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr,FLFMT "_send %d bytes, l2 flags 0x%X\n",
			FL, msg->len,  d_l3_conn->d_l3l2_flags);

		if ((diag_l3_debug & DIAG_DEBUG_DATA) && (diag_l3_debug & DIAG_DEBUG_WRITE))
			diag_data_dump(stderr, (void *) msg->data, (size_t)msg->len);
	}

	/* Note source address on 1st send */
	if (d_l3_conn->src == 0)
		d_l3_conn->src = msg->src;

	// L2 does framing, adds addressing and CRC, so do nothing special

	rv = diag_l2_send(d_conn, msg);
	return rv? diag_iseterr(rv):0 ;
}


/*
 * RX callback, called as data received from L2 (from l3_recv). If we get a full message,
 * call L3 callback routine
 */
static void
diag_l3_14230_rxcallback(void *handle, struct diag_msg *msg)
{
	/*
	 * Got some data from L2, build it into a L3 message, if
	 * message is complete call next layer callback routine
	 */
	struct diag_l3_conn *d_l3_conn = (struct diag_l3_conn *)handle;
	char buffer[200];

	if (diag_l3_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr,FLFMT "rcv_callback for %d bytes fmt 0x%X conn rxoffset %d\n",
			FL, msg->len, msg->fmt, d_l3_conn->rxoffset);
	}
	if (diag_l3_iso14230_decode_response(msg, buffer, sizeof(buffer)))
	{
	 fprintf(stderr, "DECODED: %s\n",buffer);
	}

	if (msg->fmt & DIAG_FMT_FRAMED) {

		/* Send data upward if needed */
		if (d_l3_conn->callback)
			d_l3_conn->callback(d_l3_conn->handle, msg);
	} else {
		fprintf(stderr, FLFMT "diag_l3_14230_rxcallback: problem: got an unframed message!\n"
								"Report this !\n", FL);
			/* Add data to the receive buffer on the L3 connection */
			memcpy(&d_l3_conn->rxbuf[d_l3_conn->rxoffset],
				msg->data, msg->len);	//XXX possible buffer overflow !
			d_l3_conn->rxoffset += msg->len;
	}
}


//This just forwards the request to the L2 recv function. I can't see how we could use an
// iso14230 L3 over anything else than an iso14230 L2, so there's no reason to do anything else
// in here.
static int
diag_l3_iso14230_recv(struct diag_l3_conn *d_l3_conn, int timeout,
		void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle) {

	int rv;

	if (d_l3_conn->d_l3l2_flags & DIAG_L2_FLAG_FRAMED) {
		/*
		 * L2 does framing stuff , which means we get one message
		 * with nicely formed frames
		 */
		/* Store the callback routine for use if needed */
		d_l3_conn->callback = rcv_call_back;
		d_l3_conn->handle = handle;

		/*
		 * Call L2 receive, L2 will build up the datapacket and
		 * call the callback routine if needed
		 */

		rv = diag_l2_recv(d_l3_conn->d_l3l2_conn, timeout,
			diag_l3_14230_rxcallback, (void *)d_l3_conn);

		if (diag_l3_debug & DIAG_DEBUG_READ)
			fprintf(stderr,FLFMT "_recv returns %d\n",
				FL, rv);
	} else {
		//problem : the only time DIAG_L2_FLAG_FRAMED is not set is if
		//L2 is raw. Who uses a raw L2 instead of 14230 L2 ???
		//so we'll complain loudly
		fprintf(stderr, FLFMT "*** Error : using iso14230 L3 code on a non-iso14230\n", FL);
		fprintf(stderr, FLFMT "*** L2 interface !! Please report this.\n", FL);
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}


	return rv;
}



static char *
diag_l3_iso14230_decode(UNUSED(struct diag_l3_conn *d_l3_conn),
struct diag_msg *msg, char *buf, size_t bufsize)
{
	if (msg->data[0] & 0x40)
		snprintf(buf, bufsize, "ISO14230 response ");
	else
		snprintf(buf, bufsize, "ISO14230 request ");

	return buf;
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
	diag_l3_iso14230_send, diag_l3_iso14230_recv, NULL, diag_l3_base_request,
	diag_l3_iso14230_decode, NULL
};
