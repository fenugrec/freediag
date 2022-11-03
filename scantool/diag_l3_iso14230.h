#ifndef _DIAG_L3_ISO14230_H_
#define _DIAG_L3_ISO14230_H_
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
 */
#if defined(__cplusplus)
extern "C" {
#endif

// XXX L2 uses a nifty "2/3 * P3max" formula in to calculate keepalive interval.
// I'm not sure why we need this as well.
#define ISO14230_KEEPALIVE 3500         //ms timeout before keepalive signal on OBD bus.
//That keepalive corresponds to the P3 timing value; certain ECUs allow
// changes (SID 83, AccessTimingParameter) to modify P3. But by default they
// should be configured to accept 55 ms < P3 < 5000 ms

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L3_ISO14230_H_ */
