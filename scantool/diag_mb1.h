#ifndef _DIAG_MB1_H_
#define _DIAG_MB1_H_

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
 * Library user header file
 */

#if defined(__cplusplus)
extern "C" {
#endif

#define	DIAG_MB1_GETDTC		0x05	/* Get Diagnostic Trouble Codes */
#define	DIAG_MB1_GETID		0x0f	/* Ask for controller/hw/sw version */
#define	DIAG_MB1_UNKNOWN1	0x10	/* Dunno yet */
#define	DIAG_MB1_UNKNOWN2	0x30	/* Dunno yet */
#define	DIAG_MB1_UNKNOWN3	0x38	/* Dunno yet */
#define	DIAG_MB1_IDLE		0x50	/* Idle message */

#define DIAG_MB1_ANSWER_MASK	0x80	/* Bit set for answers */

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_MB1_H_ */
