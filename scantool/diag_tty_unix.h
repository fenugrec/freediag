/*
 * diag_tty_unix.h
 *
 * This is totally unix-exclusive and should only be included by diag_tty_unix.c !
 * Public functions are in diag_tty.h
 *
 * This file is part of freediag - Vehicle Diagnostic Utility
 *
 * Copyright (C) 2001-2004 Richard Almeida & others
 * Copyright (C) 2004 Steve Baker <sjbaker@users.sourceforge.net>
 * Copyright (C) 2004 Steve Meisner <meisner@users.sourceforge.net>
 * Copyright (C) 2004 Vasco Nevoa <vnevoa@users.sourceforge.net>
 * Copyright (C) 2011-2015 fenugrec <fenugrec@users.sourceforge.net>
 * Copyright (C) 2015 Tomasz Kaźmierczak <tomek-k@users.sourceforge.net>
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

#ifndef _DIAG_TTY_UNIX_H_
#define _DIAG_TTY_UNIX_H_

#if defined(__cplusplus)
extern "C" {
#endif


#include <unistd.h>
#include <termios.h>

#if defined(_POSIX_TIMERS)
	#include <time.h>
#endif

#if defined(__linux__)
	#include <linux/rtc.h>
	#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
#endif

#include "diag_tty.h"

#define DL0D_INVALIDHANDLE -1

struct diag_ttystate
{
	/*
	 * For recording state before we mess with the interface:
	 */
#if defined(__linux__)
	struct serial_struct dt_osinfo;
#endif
	struct termios dt_otinfo;
	int dt_modemflags;

	/* For recording state after/as we mess with the interface */
#if defined(__linux__)
	struct serial_struct dt_sinfo;
#endif
	struct termios dt_tinfo;

};

//struct tty_int : internal data, one per L0 struct
struct unix_tty_int {
	int fd;						/* File descriptor */
	struct diag_ttystate *ttystate;	/* Holds OS specific tty info */

#if defined(_POSIX_TIMERS)
	timer_t timerid;		//Used for read() and write() timeouts
	volatile sig_atomic_t pt_expired;	//flag timeout expiry
#endif

#if defined(_POSIX_TIMERS) || defined(__linux__)
	unsigned long int byte_write_timeout_us; //single byte write timeout in microseconds
#endif
};

/****** OS-specific implementation selectors ******/
/*	These are for testing/debugging only, to force compilation of certain implementations
	for diag_tty* and diag_os* functions.

	### Map of features with more than one implementation related to POSIX ###

	## tty-related features ##
	SEL_TIMEOUT: diag_tty_{read,write}() timeouts
		A) needs _POSIX_TIMERS, uses timer_create + sigaction for a SIGUSR1 handler
		B) needs __linux__ && /dev/rtc
		C) (not implemented) could try setitimer + SIGUSR1 handler, similar to A)
	SEL_TTYOPEN: diag_tty_open() : open() flags:
		ALT1) needs O_NONBLOCK; open non-blocking then clear flag
		ALT2) don't set O_NONBLOCK.
	SEL_TTYCTL: diag_tty_{open,close}() : tty settings
		A) needs __linux__ : tries TIOCGSERIAL (known to fail on some cheap hw)
		B) TODO
	SEL_TTYBAUD: diag_tty_setup() : tty settings (bps, parity etc)
		A) needs __linux__ : uses TIOCSSERIAL, ASYNC_SPD_CUST, CBAUD.
		B) cfset{i,o}speed : tries setting bps directly (non-portable)
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
//#define SEL_TIMEOUT S_LINUX

/* Default selectors: anything still undefined is set to S_AUTO which
	means "force nothing", i.e. "use most appropriate implementation". */
#ifndef SEL_TIMEOUT
#define SEL_TIMEOUT	S_AUTO
#endif
#ifndef SEL_TTYOPEN
#define SEL_TTYOPEN	S_AUTO
#endif
#ifndef SEL_TTYCTL
#define SEL_TTYCTL	S_AUTO
#endif
#ifndef SEL_TTYBAUD
#define SEL_TTYBAUD	S_AUTO
#endif
/****** ******/

#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_TTY_UNIX_H_ */
