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
 * Volvo protocol application layer
 *
 * This protocol is used by the engine and chassis ECUs for extended
 * diagnostics on the 1996-1998 Volvo 850, S40, C70, S70, V70, XC70, V90 and
 * possibly other models. On the aforementioned models, it is used over
 * KWP6227 (keyword D3 B0) transport. It seems that the same protocol is also
 * used over CAN bus on more recent models.
 *
 * Information on this command set is available at:
 *   http://jonesrh.info/volvo850/volvo_850_obdii_faq.rtf
 * Thanks to Richard H. Jones for sharing this information.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l7_volvo.h"

/*
 * Service Identifier hex values are in the manufacturer defined range.
 * The service names used here are based on KWP2000. Original service names
 * are unknown. Request and response message formats for these services are
 * NOT according to KWP2000.
 */
enum {
	stopDiagnosticSession = 0xA0,
	testerPresent = 0xA1,
	readDataByLocalIdentifier = 0xA5,
	readDataByLongLocalIdentifier = 0xA6, /* CAN bus only? */
	readMemoryByAddress = 0xA7,
	readFreezeFrameByDTC = 0xAD,
	readDiagnosticTroubleCodes = 0xAE,
	clearDiagnosticInformation = 0xAF,
	readNVByLocalIdentifier = 0xB9
} service_id;

/*
 * Indicates whether a response was a positive acknowledgement of the request.
 */
static bool
success_p(uint8_t req[], uint8_t resp[])
{
	return(resp[0] == (req[0] | 0x40));
}

/*
 * Verify communication with the ECU.
 */
int
diag_l7_volvo_ping(struct diag_l2_conn *d_l2_conn)
{
	uint8_t req[] = { testerPresent };
	int errval = 0;
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;

	msg.data = req;
	msg.len = sizeof(req);

	resp = diag_l2_request(d_l2_conn, &msg, &errval);
	if (resp == NULL)
		return errval;

	if (resp->len>=1 && success_p(req, resp->data)) {
		diag_freemsg(resp);
		return 0;
	} else {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
}

/* The request message for reading memory */
static int
read_MEMORY_req(uint8_t **msgout, unsigned int *msglen, uint16_t addr, uint8_t count)
{
	static uint8_t req[] = { readMemoryByAddress, 0, 99, 99, 1, 99 };

	req[2] = (addr>>8)&0xff;
	req[3] = addr&0xff;
	req[5] = count;
	*msgout = req;
	*msglen = sizeof(req);
	return 0;
}

/* The request message for reading live data by 1-byte identifier */
static int
read_LIVEDATA_req(uint8_t **msgout, unsigned int *msglen, uint16_t addr, UNUSED(uint8_t count))
{
	static uint8_t req[] = { readDataByLocalIdentifier, 99, 1 };

	req[1] = addr&0xff;
	if (addr > 0xff) {
		fprintf(stderr, FLFMT "read_LIVEDATA_req invalid address %x\n", FL, addr);
		return DIAG_ERR_GENERAL;
	}

	*msgout = req;
	*msglen = sizeof(req);
	return 0;
}

/* The request message for reading live data by 2-byte ident (CAN bus only?) */
static int
read_LIVEDATA2_req(uint8_t **msgout, unsigned int *msglen, uint16_t addr, UNUSED(uint8_t count))
{
	static uint8_t req[] = { readDataByLongLocalIdentifier, 99, 99, 1 };
	req[1] = (addr>>8)&0xff;
	req[2] = addr&0xff;
	*msgout = req;
	*msglen = sizeof(req);
	return 0;
}

/* The request message for reading non-volatile data */
static int
read_NV_req(uint8_t **msgout, unsigned int *msglen, uint16_t addr, UNUSED(uint8_t count))
{
	static uint8_t req[] = { readNVByLocalIdentifier, 99 };

	req[1] = addr&0xff;
	if (addr > 0xff) {
		fprintf(stderr, FLFMT "read_NV_req invalid address %x\n", FL, addr);
		return DIAG_ERR_GENERAL;
	}

	*msgout = req;
	*msglen = sizeof(req);
	return 0;
}

/* The request message for reading freeze frames */
static int
read_FREEZE_req(uint8_t **msgout, unsigned int *msglen, uint16_t addr, UNUSED(uint8_t count))
{
	static uint8_t req[] = { readFreezeFrameByDTC, 99, 0 };

	req[1] = addr&0xff;
	if (addr > 0xff) {
		fprintf(stderr, FLFMT "read_FREEZE_req invalid address %x\n", FL, addr);
		return DIAG_ERR_GENERAL;
	}

	*msgout = req;
	*msglen = sizeof(req);
	return 0;
}

/*
 * Read memory, live data or non-volatile data.
 *
 * Return value is actual byte count received, or negative on failure.
 *
 * For memory reads, a successful read always copies the exact number of bytes
 * requested into the output buffer.
 *
 * For live data and non-volatile data reads, copies up to the number of bytes
 * requested. Returns the actual byte count received, which may be more or
 * less than the number of bytes requested.
 */
int
diag_l7_volvo_read(struct diag_l2_conn *d_l2_conn, enum namespace ns, uint16_t addr, int buflen, uint8_t *out)
{
	struct diag_msg req = {0};
	struct diag_msg *resp = NULL;
	int datalen;
	int rv;

	switch (ns) {
	case NS_MEMORY:
		rv = read_MEMORY_req(&req.data, &req.len, addr, buflen);
		break;
	case NS_LIVEDATA:
		rv = read_LIVEDATA_req(&req.data, &req.len, addr, buflen);
		break;
	case NS_LIVEDATA2:
		rv = read_LIVEDATA2_req(&req.data, &req.len, addr, buflen);
		break;
	case NS_NV:
		rv = read_NV_req(&req.data, &req.len, addr, buflen);
		break;
	case NS_FREEZE:
		rv = read_FREEZE_req(&req.data, &req.len, addr, buflen);
		break;
	default:
		fprintf(stderr, FLFMT "diag_l7_volvo_read invalid namespace %d\n", FL, ns);
		return DIAG_ERR_GENERAL;
	}

	if (rv != 0)
		return rv;

	resp = diag_l2_request(d_l2_conn, &req, &rv);
	if (resp == NULL)
		return rv;

	if (resp->len<2 || !success_p(req.data, resp->data) || resp->data[1]!=req.data[1]) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}

	if (ns==NS_MEMORY) {
		if (resp->len!=(unsigned int)buflen+4 || memcmp(req.data+1, resp->data+1, 3)!=0) {
			diag_freemsg(resp);
			return DIAG_ERR_ECUSAIDNO;
		}
		memcpy(out, resp->data+4, buflen);
		diag_freemsg(resp);
		return buflen;
	}

	datalen = resp->len - 2;
	if (datalen > 0)
		memcpy(out, resp->data+2, (datalen>buflen)?buflen:datalen);
	diag_freemsg(resp);
	return datalen;
}

/*
 * Retrieve list of stored DTCs.
 */
int
diag_l7_volvo_dtclist(struct diag_l2_conn *d_l2_conn, int buflen, uint8_t *out)
{
	uint8_t req[] = { readDiagnosticTroubleCodes, 1 };
	int errval = 0;
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;
	int count;

	msg.data = req;
	msg.len = sizeof(req);

	resp = diag_l2_request(d_l2_conn, &msg, &errval);
	if (resp == NULL)
		return errval;

	if (resp->len<2 || !success_p(req, resp->data) || resp->data[1]!=1) {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}

	count = resp->len - 2;
	memcpy(out, resp->data+2, (buflen<count)?buflen:count);

	if (resp->len == 14) {
		/*
		 * If there are more than 12 DTCs, ECU will send multiple
		 * responses to a single readDiagnosticTroubleCodes request.
		 * Currently we just try to throw away any additional DTCs
		 * after the first response message.
		 */
		(void)diag_l2_recv(d_l2_conn, 1000, NULL, NULL);
		fprintf(stderr, "Warning: retrieving only first 12 DTCs\n");
	}

	diag_freemsg(resp);
	return count;
}

/*
 * Attempt to clear stored DTCs.
 *
 * Returns 0 if there were no DTCs, 1 if there was at least one DTC and the
 * ECU returned positive acknowledgement for the clear request, <0 for errors.
 */
int
diag_l7_volvo_cleardtc(struct diag_l2_conn *d_l2_conn)
{
	uint8_t req[] = { clearDiagnosticInformation, 1 };
	uint8_t buf[1];
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;
	int rv;

	/*
	 * ECU will reject clearDiagnosticInformation unless preceded by
	 * readDiagnosticTroubleCodes.
	 */
	rv = diag_l7_volvo_dtclist(d_l2_conn, sizeof(buf), buf);
	if (rv < 0)
		return rv;
	if (rv == 0)
		return 0;

	msg.data = req;
	msg.len = sizeof(req);
	resp = diag_l2_request(d_l2_conn, &msg, &rv);
	if (resp == NULL)
		return rv;

	if (resp->len==2 && success_p(req, resp->data) && resp->data[1]==1) {
		diag_freemsg(resp);
		return 1;
	} else {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
}
