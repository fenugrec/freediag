#ifndef _DIAG_L2_ISO9141_H_
#define _DIAG_L2_ISO9141_H_
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
 * L2 driver for ISO 9141 and ISO 9141-2 interface
 *
 * NOTE: this is only the startcommunications routine, raw routines are
 * used for read/write
 *
 * NOTE: ISO9141/9141-2 do not specify any formatting of the data sent, except
 * that ISO9141-2 says if the address is 0x33 then SAEJ1979 protocol is used.
 *
 * Therefore it is the responsibility of layers above this to format the
 * whole frame, unlike in the ISO14230 L2 code which does this
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

/* This is used in VAG */

int diag_l2_9141_add(void);
int diag_l2_9141_2_add(void);
int diag_l2_proto_9141_int_recv (
        struct diag_l2_conn *d_l2_conn,
        int timeout ) ;

int diag_l2_proto_9141_send ( 
        struct diag_l2_conn *d_l2_conn, 
        struct diag_msg *msg ) ;

int diag_l2_proto_9141_recv (
        struct diag_l2_conn *d_l2_conn,
        int timeout,
        void (*callback)(void *handle, struct diag_msg *msg),
        void *handle ) ;

struct diag_msg *diag_l2_proto_9141_request ( 
        struct diag_l2_conn *d_l2_conn, 
        struct diag_msg *msg,
        int *errval ) ;

int diag_l2_proto_9141_sc_common (
        struct diag_l2_conn *d_l2_conn,
        int bitrate,
        target_type target,
        source_type source __attribute__((unused)),
        int kb1, int kb2 ) ;

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_ISO9141_H_ */
