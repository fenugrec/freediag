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
 *
 * DTC handling routines
 *
 */
#include <string.h>

#include "diag.h"
#include "diag_dtc.h"



//do not use *allocs or open handles in diag_dtc_init !
void diag_dtc_init(void) {
}

/** DTC decoding routine.
 *
 * Generate description (or error) string for given DTCs.
 * @param data: Data representing the DTC
 * @param len: length of *data
 * @param vehicle: (optional) Vehicle name; unused.
 * @param ecu: (optional) ECU Name; unused
 * @param protocol: Protocol (see include file)
 * @param buf: Buffer to write the output
 * @param bufsize: size of *buf
 * @return pointer to *buf, which may be useful to printf or fprintf...
 */

char *diag_dtc_decode(uint8_t *data, int len,
	UNUSED(const char *vehicle), UNUSED(const char *ecu),
	enum diag_dtc_protocol protocol,
	char *buf, const size_t bufsize) {
	char area;

	switch (protocol) {
	case dtc_proto_j2012:
		if (len != 2) {
			strncpy(buf, "DTC too short for J1850 decode\n", bufsize);
			return buf;
		}

		switch ((data[0] >> 6) & 0x03) {	/* Top 2 bits are area */
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
			fprintf(stderr, "Illegal data[0] value\n");
			area = 'X';
			break;
		}
		snprintf(buf, bufsize, "%c%02X%02X ", area, data[0] & 0x3f, data[1]&0xff);
		break;

	case dtc_proto_int8:
	case dtc_proto_int16:
	case dtc_proto_int32:
	case dtc_proto_text:
		snprintf(buf, bufsize, "Unimplemented Protocol %d\n", protocol);
		break;

	default:
		snprintf(buf, bufsize, "Unknown Protocol %d\n", protocol);
		break;
	}
	return buf;
}
