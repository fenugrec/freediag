#ifndef _DIAG_L2_ISO14230_H_
#define _DIAG_L2_ISO14230_H_
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
 * Diag
 *
 * L2 driver for ISO14230-2 layer 2
 *
 */

/*
 * ISO 14230 specific data
 */
struct diag_l2_14230
{
	uint8_t type;		/* FAST/SLOW/CARB */

	uint8_t srcaddr;	/* Src address used */
	uint8_t dstaddr;	/* Dest address used (for connect) */
	uint16_t modeflags;	/* Flags; see diag_l2.h */

	uint8_t state;

	uint8_t first_frame;	/* First frame flag, used mainly for
					monitor mode when we need to find
					out whether we see a CARB or normal
					init */

	uint8_t rxbuf[MAXRBUF];	/* Receive buffer, for building message in */
	int rxoffset;		/* Offset to write into buffer */
};

#define STATE_CLOSED	  0	/* Established comms */
#define STATE_CONNECTING  1	/* Connecting */
#define STATE_ESTABLISHED 2	/* Established */




#if defined(__cplusplus)
extern "C" {
#endif

int diag_l2_14230_add(void);

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_ISO14230_H_ */
