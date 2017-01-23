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

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_l1.h"
#include "diag_l2.h"

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
	readMemoryByAddress = 0xA7,
        readDiagnosticTroubleCodes = 0xAE,
	clearDiagnosticInformation = 0xAF
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

	if (success_p(req, resp->data)) {
		diag_freemsg(resp);
		return 0;
	} else {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
}

/*
 * Read one or more bytes from RAM.
 */
int
diag_l7_volvo_peek(struct diag_l2_conn *d_l2_conn, uint16_t addr, uint8_t len, uint8_t *out)
{
	uint8_t req[] = { readMemoryByAddress, 0, (addr>>8)&0xff, addr&0xff, 1, len };
	int errval = 0;
	struct diag_msg msg = {0};
	struct diag_msg *resp = NULL;

	msg.data = req;
	msg.len = sizeof(req);

	resp = diag_l2_request(d_l2_conn, &msg, &errval);
	if (resp == NULL)
		return errval;

	if (success_p(req, resp->data) && memcmp(req+1, resp->data+1, 3)==0 && resp->len == (unsigned int)len+4) {
		memcpy(out, resp->data+4, len);
		diag_freemsg(resp);
		return 0;
	} else {
		diag_freemsg(resp);
		return DIAG_ERR_ECUSAIDNO;
	}
}
