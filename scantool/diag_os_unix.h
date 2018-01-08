/*
 * diag_os_unix.h
 *
 * This is totally unix-exclusive and should only be included by diag_os_unix.c !
 * Public functions are in diag_os.h
 *
 * This file is part of freediag - Vehicle Diagnostic Utility
 *
 * Copyright (C) 2015 fenugrec <fenugrec@users.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef _DIAG_OS_UNIX_H_
#define _DIAG_OS_UNIX_H_

#if defined(__cplusplus)
extern "C" {
#endif

/****** OS-specific implementation selectors ******/
/*	These are for testing/debugging only, to force compilation of certain implementations
	for diag_tty* and diag_os* functions.

	### Map of features with more than one implementation related to POSIX ###

	## time-related features ##
	SEL_PERIODIC: Periodic timer callback (for L2+L3 keepalives)
		A) needs _POSIX_TIMERS, uses timer_create()
		B) (available everywhere?) setitimer+sigaction to install a SIGALRM handler
	SEL_SLEEP: diag_os_millisleep()
		A) needs _POSIX_TIMERS, uses clock_nanosleep()
		B) needs __linux__ && (uid==root), uses /dev/rtc
		C) (available everywhere?) nanosleep() loop
	SEL_HRT: diag_os_gethrt()
		A) needs _POSIX_TIMERS, uses clock_gettime()
		B) (available everywhere?) gettimeofday()

	######
	For every feature listed above, it's possible to force compilation of
	a specific implementation using the #defines below.
	TODO: add compile tests to cmake?
*/
#define S_AUTO	0
/* First set, for obviously OS-dependant features: */
#define	S_POSIX 1
#define	S_LINUX 2
#define S_OTHER 3
/* Second set, not necessarily OS-dependant */
#define S_ALT1	1
#define S_ALT2	2
/** Insert desired selectors here **/
//example:
//#define	SEL_PERIODIC S_OTHER

/* Default selectors: anything still undefined is set to S_AUTO which
	means "force nothing", i.e. "use most appropriate implementation". */
#ifndef SEL_PERIODIC
#define SEL_PERIODIC	S_AUTO
#endif
#ifndef SEL_SLEEP
#define SEL_SLEEP	S_AUTO
#endif
#ifndef SEL_HRT
#define SEL_HRT	S_AUTO
#endif

/****** ******/


#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_OS_UNIX_H_ */

