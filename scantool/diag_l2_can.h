#ifndef _DIAG_L2_CAN_H_
#define _DIAG_L2_CAN_H_
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
 * CAN.  Not written, but put in its own file for modularity.  It used
 * to be inline in L2.
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

int diag_l2_can_add(void);

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_CAN_H_ */
