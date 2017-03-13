#ifndef _DIAG_L7_H_
#define _DIAG_L7_H_
/*
 *	freediag - Vehicle Diagnostic Utility
 *
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
 * Application layer
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

enum namespace {
	NS_MEMORY,
	NS_ROM,
	NS_ADC,
	NS_LIVEDATA,
	NS_LIVEDATA2,
	NS_NV,
	NS_FREEZE
};

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L7_H_ */
