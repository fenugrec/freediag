/*
 *	freediag - Vehicle Diagnostic Utility
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
 * L2 driver for ISO 9141-2 interface
 *
 * NOTE: this is only slimline wrapper - most of the code is in diag_l2_iso9141.c
 */

#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_raw.h"

#include "diag_l2_iso9141.h"
#include "diag_l2_iso9141_2.h" /* prototypes for this file */

CVSID("$Id$");

static int
diag_l2_proto_9141_2_startcomms( struct diag_l2_conn	*d_l2_conn, flag_type flags,
	int bitrate, target_type target, source_type source)
{
	int rv;
	uint8_t cbuf[2];
	struct diag_serial_settings set;

	if (diag_l2_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr,
			FLFMT "diag_l2_9141_2_startcomms conn %p\n",
				FL, d_l2_conn);
	/*
	 * If 0 has been specified, use the correct speed
	 * for ISO9141 protocol
	 */
	if (bitrate == 0)
		bitrate = 10400;
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	/* Set the speed as shown */
	rv = diag_l1_setspeed( d_l2_conn->diag_link->diag_l2_dl0d, &set);
	if (rv < 0)
		return (rv);

	/* Don't do 5 baud init if monitor mode */
	if ( (flags & DIAG_L2_TYPE_INITMASK) ==  DIAG_L2_TYPE_MONINIT)
		rv = 0;
	else
	{
		/* Do common init */
		rv = diag_l2_proto_9141_sc_common( d_l2_conn, bitrate,
			target, source, 0x00, 0x00);

		/*
		 * Check keybytes, these can be 0x08 0x08 or 0x94 0x94
		 * - In the case of the latter, we need to tweak some of
		 * the timing parameters for this connection.
		 */

		if (d_l2_conn->diag_l2_kb1 !=  d_l2_conn->diag_l2_kb2)
			return(diag_iseterr(DIAG_ERR_WRONGKB));
		if ( (d_l2_conn->diag_l2_kb1 != 0x08) &&
			(d_l2_conn->diag_l2_kb1 != 0x94) )
				return(diag_iseterr(DIAG_ERR_WRONGKB));

		if (d_l2_conn->diag_l2_kb1 == 0x94)
		{
			/* P2min is 0 for kb 0x94, 25ms for kb 0x08 */
			d_l2_conn->diag_l2_p2min = 0;
		}
	}

	if (rv < 0)
	{
		if (diag_l2_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr,
				FLFMT "startcomms con %p, common() error %d\n",
					FL, d_l2_conn, rv);
		return(rv);
	}
		
	/* Now do 9141-2 specific */

	/*
	 * Now transmit KB2 inverted, complying with
	 * w4 time (25-50ms) (unless L1 deals with this)
	 */
	if ( (d_l2_conn->diag_link->diag_l2_l1flags
			& DIAG_L1_DOESSLOWINIT) == 0)
	{
		diag_os_millisleep(25);
		cbuf[0] = ~ d_l2_conn->diag_l2_kb2;
		rv = diag_l1_send (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			cbuf, 1, 0);

		/*
		 * And wait for the address byte inverted, again within
		 * w4
		 */
		rv = diag_l1_recv (d_l2_conn->diag_link->diag_l2_dl0d, 0,
			cbuf, 1, 50);

		if (rv < 0)
		{
			if (diag_l2_debug & DIAG_DEBUG_OPEN)
				fprintf(stderr,
					FLFMT "startcomms con %p rx error %d\n",
						FL, d_l2_conn, rv);
			return(rv);
		}

	        if (cbuf[0] != ((~target) & 0xFF) )
		{
			if (diag_l2_debug & DIAG_DEBUG_OPEN)
				fprintf(stderr,
					FLFMT "startcomms 0x%x != 0x%x\n",
						FL, cbuf[0] & 0xff,
						(~target) &0xff);
			return(diag_iseterr(DIAG_ERR_WRONGKB));
		}
	}
	return (rv);
}


static const struct diag_l2_proto diag_l2_proto_9141_2 = {
	DIAG_L2_PROT_ISO9141_2, DIAG_L2_FLAG_FRAMED,
	diag_l2_proto_9141_2_startcomms,
	diag_l2_proto_raw_stopcomms,
	diag_l2_proto_9141_send,
	diag_l2_proto_9141_recv,
	diag_l2_proto_9141_request,
	NULL
};


int diag_l2_9141_2_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_9141_2);
}
