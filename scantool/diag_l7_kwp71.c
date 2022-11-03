/*
 *      freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017 Adam Goldman
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
 * KWP71 application layer
 *
 * KWP71 is used by Bosch ECUs in various European cars from the 1990s.
 * KWP1281 is an extended(?) version of KWP71 with faster timing.
 *
 * KWP71 and KWP1281 are similar enough that this L7 can be used with
 * freediag's VAG (KWP1281) L2 with at least some KWP71 capable ECUs.
 *
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "diag_l7.h"

/*
 * The block title names used here are based on KWP2000. Original block titles
 * are unknown. Request and response message formats for these blocks are
 * NOT according to KWP2000.
 *
 * Not all block titles are supported by all ECUs.
 */
enum {
	/* requests */
	readMemoryByAddress = 0x01,
	writeMemoryByAddress = 0x02,
	readROMByAddress = 0x03,
	clearDiagnosticInformation = 0x05,
	stopDiagnosticSession = 0x06,
	readDiagnosticTroubleCodes = 0x07,
	readADC = 0x08,
	/* responses - no numerical relation to corresponding request */
	ack = 0x09, /* doubles as testerPresent request */
	nak = 0x0A,
	writeMemoryByAddress_resp = 0xED,
	readADC_resp = 0xFB,
	readDiagnosticTroubleCodes_resp = 0xFC,
	readROMByAddress_resp = 0xFD,
	readMemoryByAddress_resp = 0xFE,
} block_titles;

/*
 * Verify communication with the ECU.
 */
int
diag_l7_kwp71_ping(struct diag_l2_conn *d_l2_conn) {
	int errval = 0;
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;

	msg.type = ack;

	resp = diag_l2_request(d_l2_conn, &msg, &errval);
	if (resp == NULL) {
		return errval;
	}

	if (resp->type == ack) {
		diag_freemsg(resp);
		return 0;
	}

	diag_freemsg(resp);
	return DIAG_ERR_ECUSAIDNO;
}


#define KWP71_REQSIZE 3
/* Fill the request message for reading memory */
static int
read_MEMORY_req(struct diag_msg *msg, uint8_t *wantresp, uint16_t addr, uint8_t count) {
	uint8_t *data=msg->data;

	msg->type = readMemoryByAddress;
	msg->len = 3;
	data[0] = count;
	data[1] = (addr>>8)&0xff;
	data[2] = addr&0xff;
	*wantresp = readMemoryByAddress_resp;
	return 0;
}

/* Fill the request message for reading ROM */
static int
read_ROM_req(struct diag_msg *msg, uint8_t *wantresp, uint16_t addr, uint8_t count) {
    uint8_t *data=msg->data;

	msg->type = readROMByAddress;
	msg->len = 3;
	data[0] = count;
	data[1] = (addr>>8)&0xff;
	data[2] = addr&0xff;
	*wantresp = readROMByAddress_resp;
	return 0;
}

/* The request message for taking ADC readings */
static int
read_ADC_req(struct diag_msg *msg, uint8_t *wantresp, uint16_t addr) {
	uint8_t *data=msg->data;

	if (addr > 0xff) {
		fprintf(stderr, FLFMT "read_ADC_req invalid address %x\n", FL, addr);
		return DIAG_ERR_GENERAL;
	}

	msg->type = readADC;
	msg->len = 3;
	data[0] = addr;
	*wantresp = readADC_resp;
	return 0;
}

/*
 * Read memory, ROM or ADC.
 *
 * Return value is actual byte count received, or negative on failure.
 *
 * For memory and ROM reads, a successful read always copies the exact number
 * of bytes requested into the output buffer.
 *
 * For ADC reads, reads a single 2-byte value and copies up to the number of
 * bytes requested. Returns the actual byte count received.
 */
int
diag_l7_kwp71_read(struct diag_l2_conn *d_l2_conn, enum l7_namespace ns, uint16_t addr, int buflen, uint8_t *out) {
	struct diag_msg req;    //build request message in this
	uint8_t request_data[KWP71_REQSIZE];
	struct diag_msg *resp = NULL;
	uint8_t wantresp;
	int rv;

	req.data = request_data;
	switch (ns) {
	case NS_MEMORY:
		rv = read_MEMORY_req(&req, &wantresp, addr, buflen);
		break;
	case NS_ROM:
		rv = read_ROM_req(&req, &wantresp, addr, buflen);
		break;
	case NS_ADC:
		rv = read_ADC_req(&req, &wantresp, addr);
		break;
	default:
		fprintf(stderr, FLFMT "diag_l7_kwp71_read invalid namespace %d\n", FL, ns);
		return DIAG_ERR_GENERAL;
	}

	if (rv != 0) {
		return rv;
	}

	resp = diag_l2_request(d_l2_conn, &req, &rv);
	if (resp == NULL) {
		return rv;
	}

	if (resp->type!=wantresp) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}

	if (ns==NS_ADC && resp->len!=2) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
	if (ns != NS_ADC && resp->len != (unsigned int)buflen) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
	memcpy(out, resp->data, (resp->len>(unsigned int)buflen)?(unsigned int)buflen:resp->len);
	rv = (resp->len > (unsigned int)buflen) ? (unsigned int)buflen:resp->len;
	diag_freemsg(resp);
	return rv;
}

/*
 * Retrieve list of stored DTCs.
 *
 * Seems to return 5 bytes per DTC, but output format and size may vary by ECU.
 *
 * Returns the actual number of bytes read, even if the supplied buffer was too
 * small for the full response.
 */
int
diag_l7_kwp71_dtclist(struct diag_l2_conn *d_l2_conn, int buflen, uint8_t *out) {
	int errval = 0;
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;
	int count;

	msg.type = readDiagnosticTroubleCodes;

	resp = diag_l2_request(d_l2_conn, &msg, &errval);
	if (resp == NULL) {
		return errval;
	}

	count = resp->len;

	if (resp->type!=readDiagnosticTroubleCodes_resp) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}

	if (count == 1 && resp->data[0] == 0) { /* No DTCs set */
		count = 0;
	}

	if (count > 0) {
		memcpy(out, resp->data, (buflen < count) ? buflen : count);
	}
	diag_freemsg(resp);

	if (resp->next != NULL) {
		/*
		 * If there are more than 2 DTCs, ECU will send multiple
		 * responses. For now, we only look at the DTCs in the first
		 * response.
		 */
		fprintf(stderr, "Warning: retrieving only first %d DTCs\n", count/5);
	}

	return count;
}

/*
 * Attempt to clear stored DTCs.
 *
 * Returns 0 if there were no DTCs, 1 if there was at least one DTC and the
 * ECU returned positive acknowledgement for the clear request, <0 for errors.
 */
int
diag_l7_kwp71_cleardtc(struct diag_l2_conn *d_l2_conn) {
	uint8_t buf[1];
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;
	int rv;

	/*
	 * Call readDiagnosticTroubleCodes first, even though it's not
	 * required in KWP71.
	 */
	rv = diag_l7_kwp71_dtclist(d_l2_conn, sizeof(buf), buf);
	if (rv < 0) {
		return rv;
	}
	if (rv == 0) {
		return 0;
	}

	diag_os_millisleep(500);

	msg.type = clearDiagnosticInformation;
	resp = diag_l2_request(d_l2_conn, &msg, &rv);
	if (resp == NULL) {
		return rv;
	}

	if (resp->type == ack) {
		diag_freemsg(resp);
		return 1;
	}
	diag_freemsg(resp);
	return DIAG_ERR_ECUSAIDNO;
}
