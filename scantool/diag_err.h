#ifndef _DIAG_ERR_H_
#define _DIAG_ERR_H_

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
 * Error codes returned from diag layers
 *
 */
#if defined(__cplusplus)
extern "C" {
#endif


#define	DIAG_ERR_GENERAL	-1	/* Unspecified */
#define	DIAG_ERR_BADFD		-2	/* Invalid FileDescriptor passed to routine */
#define	DIAG_ERR_NOMEM		-3	/* Malloc/Calloc/Strdup/etc failed - ran out of memory  */

#define DIAG_ERR_INIT_NOTSUPP	-4	/* Initbus type not supported by H/W */
#define DIAG_ERR_PROTO_NOTSUPP	-5	/* Initbus type not supported by H/W */
#define DIAG_ERR_IOCTL_NOTSUPP	-6	/* Ioctl type not supported */
#define DIAG_ERR_BADIFADAPTER	-7	/* L0 adapter comms failed */

#define DIAG_ERR_TIMEOUT	-8	/* Read/Write timeout */

#define DIAG_ERR_BUSERROR	-16	/* We detected write error on diag bus */
#define DIAG_ERR_BADLEN		-17	/* Bad length for this i/f */
#define DIAG_ERR_BADDATA	-18	/* Cant decode msg (ever) */
#define DIAG_ERR_BADCSUM	-19	/* Bad checksum in recvd message */
#define DIAG_ERR_INCDATA	-20	/* Incomplete data, need to receive more */
#define DIAG_ERR_WRONGKB	-21	/* Wrong KeyBytes received */
#define DIAG_ERR_BADRATE	-22	/* Bit rate specified doesn't match ECU */

#define DIAG_ERR_ECUSAIDNO	-32	/* Ecu returned negative */

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_ERR_H_ */
