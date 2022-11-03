#ifndef _DIAG_L7_KWP71_H_
#define _DIAG_L7_KWP71_H_
/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017 Adam Goldman
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
 * KWP71 application layer
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "diag.h"
#include "diag_l2.h"
#include "diag_l7.h"

int diag_l7_kwp71_ping(struct diag_l2_conn *d_l2_conn);
int diag_l7_kwp71_read(struct diag_l2_conn *d_l2_conn, enum l7_namespace ns, uint16_t addr, int buflen, uint8_t *out);
int diag_l7_kwp71_dtclist(struct diag_l2_conn *d_l2_conn, int buflen, uint8_t *out);
int diag_l7_kwp71_cleardtc(struct diag_l2_conn *d_l2_conn);

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L7_KWP71_H_ */
