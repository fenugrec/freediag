#ifndef _DIAG_ISO14230_H_
#define _DIAG_ISO14230_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id$
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
 * ISO 14230 (Keyword Protocol 2000) layer 3
 *
 * (c) 2001 R.P.Almeida
 *
 * This is NOT freely distributable
 * Once it is complete it will be released under the Gnu public
 * licence
 *
 * Read in conjunction with the ISO document, and the Swedish
 * recommended practice documents
 */

/***** ISO 14230-3, application interface *****/

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Service Identifier (request) codes, names made up.
 * Note : the strings are associated with the codes in diag_l3_iso14230.c
 * Entries marked "SSF only !" are SIDs not defined in ISO 14230-3 but
 * in the Swedish SSF 14230-3 document.
 */

/* 00->0F SAE J1979 Diagnostic Test Modes */
#define DIAG_KW2K_SI_STADS	0x10	/* startDiagnosticSession */
#define DIAG_KW2K_SI_ER		0x11	/* ecuReset */
#define DIAG_KW2K_SI_RDFFD	0x12	/* readFreezeFrameData */
#define DIAG_KW2K_SI_RDTC	0x13	/* readDiagnosticTroubleCodes */
#define DIAG_KW2K_SI_CDI	0x14	/* clearDiagnosticInformation */

#define DIAG_KW2K_SI_RDSODTC	0x17	/* readStatusOfDiagnosticTroubleCodes */
#define DIAG_KW2K_SI_RDTCBS	0x18	/* readDiagnosticTroubleCodesByStatus */

#define DIAG_KW2K_SI_REID	0x1A	/* readEcuId */

#define DIAG_KW2K_SI_STODS	0x20	/* stopDiagnosticSession */
#define DIAG_KW2K_SI_RDDBLI	0x21	/* readDataByLocalId */
#define DIAG_KW2K_SI_RDDBCI	0x22	/* readDataByCommonId */
#define DIAG_KW2K_SI_RDMBA	0x23	/* readMemoryByAddress */

#define DIAG_KW2K_SI_SRDT	0x25	/* stopRepeatedDataTransmission - SSF only! */
#define DIAG_KW2K_SI_SDR	0x26	/* setDataRates - SSF only!*/
#define DIAG_KW2K_SI_SA		0x27	/* securityAccess */

#define DIAG_KW2K_SI_DDLI	0x2C	/* dynamicallyDefineLocalId */
#define DIAG_KW2K_SI_WRDBCI	0x2E	/* writeDataByCommonId */
#define DIAG_KW2K_SI_IOCBCI	0x2F	/* inputOutputControlByCommonId */
#define DIAG_KW2K_SI_IOCBLI	0x30	/* inputOutputControlByLocalId */
#define DIAG_KW2K_SI_STARBLI	0x31	/* startRoutineByLocalID */
#define DIAG_KW2K_SI_STORBLI	0x32	/* stopRoutineByLocalID */
#define DIAG_KW2K_SI_RRRBLI	0x33	/* requestRoutineResultsByLocalId */
#define DIAG_KW2K_SI_RD		0x34	/* requestDownload */
#define DIAG_KW2K_SI_RU		0x35	/* requestUpload */
#define DIAG_KW2K_SI_TD		0x36	/* transfer data */
#define DIAG_KW2K_SI_RTE	0x37	/* request transfer exit */
#define DIAG_KW2K_SI_STARBA	0x38	/* startRoutineByAddress */
#define DIAG_KW2K_SI_STORBA	0x39	/* stopRoutineByAddress */
#define DIAG_KW2K_SI_RRRBA	0x3A	/* requestRoutineResultsByAddress */
#define DIAG_KW2K_SI_WRDBLI	0x3B	/* writeDataByLocalId */

#define DIAG_KW2K_SI_WRMBA	0x3D	/* writeMemoryByAddress */
#define DIAG_KW2K_SI_TP		0x3E	/* testerPresent */
#define DIAG_KW2K_SI_ESC	0x80	/* EscCode */

#define DIAG_KW2K_SI_SCR	0x81	/* startCommunication */
#define DIAG_KW2K_SI_SPR	0x82	/* stopCommunication */
#define DIAG_KW2K_SI_ATP	0x83	/* accessTimingParameters */


/*
 * Responses
 *
 * Positive responses are  service ID + 0x40
 */
#define DIAG_KW2K_RC_NR		0x7F	/* negative Response */


/*
 * Service response codes
 * names from ISO book
 */

/* Negative Responses */

#define DIAG_KW2K_RC_GR		0x10	/* generalReject */
#define DIAG_KW2K_RC_SNS	0x11	/* serviceNotSupported */
#define DIAG_KW2K_RC_SFNS_IF	0x12	/* subFunctionNotSupported-Invalid Format */
#define DIAG_KW2K_RC_B_RR	0x21	/* busy-repeatRequest */
#define DIAG_KW2K_RC_CNCORSE	0x22	/* conditionsNoteCorrectOrRequestSequenceError */
#define DIAG_KW2K_RC_RNC	0x23	/* routineNotCompleteOrServiceInProgress */
#define DIAG_KW2K_RC_ROOT	0x31	/* requestOutOfRange */
#define DIAG_KW2K_RC_SAD_SAR	0x33	/* securityAccessDenied-securityAccessRequested */
#define DIAG_KW2K_RC_IK		0x35	/* invalidKey */
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
#define DIAG_KW2K_RC_TS		0x71	/* transferSuspended */
#define DIAG_KW2K_RC_TA		0x72	/* transferAborted */
#define DIAG_KW2K_RC_IAIBT	0x74	/* illegalAddressInBlockTransfer */
#define DIAG_KW2K_RC_IBCIBT	0x75	/* illegalByteCountInBlockTransfer */
#define DIAG_KW2K_RC_IBTT	0x76	/* illegalBlockTrasnferType */
#define DIAG_KW2K_RC_BTCDE	0x77	/* blockTransferDataChecksumError */
#define DIAG_KW2K_RC_RCR_RP	0x78	/* requestCorrectyRcvd-RspPending */
#define DIAG_KW2K_RC_IBCDBT	0x79	/* incorrectByteCountDuringBlockTransfer */
#define DIAG_KW2K_RC_SNSIADS	0x80	/* serviceNotSupportedInActiveDiagnosticMode - SSF only !*/
// Note : responses >= 0x80 are MfgSpecifiCodes in ISO14230 !

/* Positive Responses */

/* 81-8F	reserved */
/* 90-F9	vehicle manufacturer specific */
#define DIAG_KW2K_RC_SCRPR	0xC1	/* StartComms +ve response */
#define DIAG_KW2K_RC_SPRPR	0xC2	/* StopComms +ve response */
#define DIAG_KW2K_RC_ATPPR	0xC3	/* AccessTimingParams +ve response */
/* FA-FE	system supplier specific */
/* FF		reserved by document */

/* Exports */
char *diag_l3_iso14230_decode_response(struct diag_msg *, char *, const size_t);

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_ISO14230_H_ */
