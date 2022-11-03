#ifndef _DIAG_L2_ISO14230_H_
#define _DIAG_L2_ISO14230_H_
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
 * Diag
 *
 * L2 driver for ISO14230-2 layer 2
 *
 */

#include <stdbool.h>
#include <stdint.h>

#include "diag.h"

/*
 * ISO 14230 specific data
 */
struct diag_l2_14230 {
	uint8_t initype;                /* init type : FAST/SLOW/CARB */

	uint8_t srcaddr;        /* Src address used */
	uint8_t dstaddr;        /* Dest address used (for connect) */
	int modeflags;  /* 14230-specific Flags; see below */

	enum {
		STATE_CLOSED=0,         /* Established comms */
		STATE_CONNECTING=1,             /* Connecting */
		STATE_ESTABLISHED=2,    /* Established */
	} state;

	bool first_frame;       /* First frame flag, used mainly for
	                                monitor mode when we need to find
	                                out whether we see a CARB or normal
	                                init */
	bool monitor_mode;      /* if set, expect possible spurious 0x00 bytes caused by fastinit break */

	uint8_t rxbuf[MAXRBUF]; /* Receive buffer, for building message in */
	int rxoffset;           /* Offset to write into buffer */
};

// ******* flags for ->modeflags :
//(these are set in 14230_startcomms() according to init type and keybytes received)

// ISO14230_SHORTHDR (for iso14230) : if set, the ECU supports address-less
// headers; this is set according to the keybytes received during StartComms.
// By default we send fully addressed headers (iso14230 5.2.4.1)
#define ISO14230_SHORTHDR 0x01

//ISO14230_LONGHDR : if set, we can send headers with the address bytes.
// Set according to the keybytes. If this and SHORTHDR are set, we
// send addressless headers by default.
#define ISO14230_LONGHDR 0x2

//ISO14230_LENBYTE: If set, tell the iso14230 code to always use messages
// with a length byte. This is primarily for SSF14230 - the Swedish vehicle
// implementation of ISO14230, but is set if required by the StartComms keybytes.
//If FMTLEN and LENBYTE are set, then we choose the most adequate.
#define ISO14230_LENBYTE 0x4

//ISO14230_FMTLEN: if set, we can send headers with the length encoded in
// the format byte. (iso14230)
#define ISO14230_FMTLEN 0x8

//ISO14230_FUNCADDR : if set, functional addressing (instead of phys) is used.
#define ISO14230_FUNCADDR 0x10

//ISO14230_IDLE_J1978 : use SID 1 PID 0 keep-alive messages instead of the normal
//TesterPresent SID.
#define ISO14230_IDLE_J1978 0x20




#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_ISO14230_H_ */
