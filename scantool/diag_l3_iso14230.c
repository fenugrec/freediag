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

#include "diag.h"
#include "diag_err.h"
#include "diag_iso14230.h"
#include "diag_l1.h"
#include "diag_l2.h"

CVSID("$Id$");

/*
 * We'll use a big switch statement here, and rely on the compiler
 * to make it efficient
 */
char *
diag_l3_iso14230_decode_response(struct diag_msg *msg,
char *buf, const size_t bufsize)
{
	char buf2[80];

	switch (*msg->data)
	{
	default:
		snprintf(buf, bufsize, "Unknown_response_code 0x%x\n", msg->data[0]);
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
			snprintf(buf, bufsize,  "General_Error no response code\n");
		} else {

			snprintf(buf, bufsize,  "General_Error, Request_ID_0x%x ", msg->data[1]);
			switch (msg->data[2])
			{
			default:
				snprintf(buf2, sizeof(buf2), "Error_0x%x", msg->data[2]);
				break;
			case DIAG_KW2K_RC_GR:
				snprintf(buf2, sizeof(buf2), "General_Reject");
			case DIAG_KW2K_RC_SNS:
				snprintf(buf2, sizeof(buf2), "Service_Not_Supported");
			case DIAG_KW2K_RC_SFNS_IF:
				snprintf(buf2, sizeof(buf2), "SubFunction_Not_Supported");
				break;
			case DIAG_KW2K_RC_B_RR:
				snprintf(buf2, sizeof(buf2), "Busy_Repeat_Request");
				break;
			case DIAG_KW2K_RC_CNCORSE:
				snprintf(buf2, sizeof(buf2), "Conditions_Not_Correct");
				break;
			case DIAG_KW2K_RC_RNC:
				snprintf(buf2, sizeof(buf2), "Routine_Not_Complete");
				break;
			}

			/* Don't overflow our buffers. */

			smartcat(buf, bufsize, buf2 );
		}
		break;
	}

	return(buf);
}

#if notdef

#define DIAG_KW2K_RC_ROOT	0x31	/* requestOutOfRange */
#define DIAG_KW2K_RC_SAD_SAR	0x33	/* securityAccessDenied-securityAccessRequested */
#define DIAG_KW2K_RC_IK	0x35	/* invalidKey */
#define DIAG_KW2K_RC_ENOA	0x36	/* exceedNumberOfAttempts */
#define DIAG_KW2K_RC_RTDNE	0x37	/* requiredTimeDelayNotExpired */
#define DIAG_KW2K_RC_DNA	0x40	/* downloadNotAccepted */
#define DIAG_KW2K_RC_IDT	0x41	/* improperDownloadType */

#define DIAG_KW2K_RC_CNDTSA	0x42	/* canNotDownloadToSpecifiedAddress */
#define DIAG_KW2K_RC_CNDNOBR	0x43	/* canNotDownloadNumberOfBytesRequested */
#define DIAG_KW2K_RC_UNA	0x50	/* uploadNotAccepted */
#define DIAG_KW2K_RC_IUT	0x51	/* improperUploadType */
#define DIAG_KW2K_RC_CNUFSA	0x52	/* canNotUploadFromSpecifiedAddress */
#define DIAG_KW2K_RC_CNUNOBR	0x53	/* canNotUploadNumberOfBytesRequested */
#define DIAG_KW2K_RC_TS	0x71	/* transferSuspended */
#define DIAG_KW2K_RC_TA	0x72	/* transferAborted */
#define DIAG_KW2K_RC_IAIBT	0x74	/* illegalAddressInBlockTransfer */
#define DIAG_KW2K_RC_IBCIBT	0x75	/* illegalByteCountInBlockTransfer */
#define DIAG_KW2K_RC_IBTT	0x76	/* illegalBlockTransferType */
#define DIAG_KW2K_RC_BTCDE	0x77	/* blockTransferDataChecksumError */
#define DIAG_KW2K_RC_RCR_RP	0x78	/* requestCorrectRcvd-RspPending */
#define DIAG_KW2K_RC_IBCDBT	0x79	/* incorrectByteCountDuringBlockTransfer */
#define DIAG_KW2K_RC_SNSIADS	0x80	/* serviceNotSupportedInActiveDiagnosticMode */


/* 81-8F	reserved */
/* 90-F9	vehicle manufacturer specific */
/* FA-FE	system supplier specific */
/* FF		reserved by document */

#endif

