#ifndef _DIAG_L2_RAW_H_
#define _DIAG_L2_RAW_H_
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
 * L2 driver for "raw" interface (just sends and receives data without
 * modifying it
 *
 */

/*
*/

#if defined(__cplusplus)
extern "C" {
#endif

#include "diag.h"
struct diag_l2_conn;

int
dl2p_raw_startcomms( struct diag_l2_conn *d_l2_conn, flag_type flags,
	unsigned int bitrate, target_type target, source_type source);

/*
*/
int
dl2p_raw_stopcomms(struct diag_l2_conn *pX);

/*
 * Just send the data, with no processing etc
 */
int
dl2p_raw_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg);

/*
*/
int
dl2p_raw_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
	void (*callback)(void *handle, struct diag_msg *msg), void *handle);

/*
*/
struct diag_msg *
dl2p_raw_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval);


#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_RAW_H_ */
