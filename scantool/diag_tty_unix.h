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

/****** OS-specific implementation selectors ******/
/*	These are for testing/debugging only, to force compilation of certain implementations
	for diag_tty* and diag_os* functions.

	### Map of features with more than one implementation related to POSIX ###

	## tty-related features ##
	SEL_TIMEOUT: diag_tty_{read,write}() timeouts
		S_POSIX) needs _POSIX_TIMERS, uses timer_create + sigaction for a SIGUSR1 handler
		S_OTHER) use select(timeout) + read + manual timeout check loop
		S_LINUX) needs __linux__ && /dev/rtc
		X) (ugly, not implemented) : increase OS periodic callback frequency, control timeout manually
	SEL_TTYOPEN: diag_tty_open() : open() flags:
		ALT1) needs O_NONBLOCK; open non-blocking then clear flag
		ALT2) don't set O_NONBLOCK.
	SEL_TTYBAUD: diag_tty_setup() : tty settings (bps, parity etc)
		ALT1) needs __linux__ : termios2 + BOTHER
		ALT2) needs __linux__ : uses TIOCSSERIAL, ASYNC_SPD_CUST, CBAUD.
		ALT3) needs "B9600 == 9600" etc. Calls cfset{i,o}speed with speed in bps (very non-portable)
		ALTx) picks nearest standard Bxxxx; calls cfset{i,o}speed (universal fallback)
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
#define S_ALT3	3
/** Insert desired selectors here **/
//example:
#define SEL_TIMEOUT S_OTHER
//#define SEL_TIMEOUT S_LINUX
//#define SEL_TTYBAUD S_ALT3

/* Default selectors: anything still undefined is set to S_AUTO which
	means "force nothing", i.e. "use most appropriate implementation". */
#ifndef SEL_TIMEOUT
#define SEL_TIMEOUT	S_AUTO
#endif
#ifndef SEL_TTYOPEN
#define SEL_TTYOPEN	S_AUTO
#endif
#ifndef SEL_TTYBAUD
#define SEL_TTYBAUD	S_AUTO
#endif
/****** ******/


/*
 There are two possible definitions for "struct termios". One is found
 in <termios.h> provided by glibc; the other is in <asm/termios.h> provided
 by linux kernel headers ! They differ and are mutually exclusive, of course.
*/

/** FUGLY HACKS BELOW **/
#if defined(__linux__) && (SEL_TTYBAUD==S_ALT1 || SEL_TTYBAUD==S_AUTO)
	#define USE_TERMIOS2
#endif

#ifdef USE_TERMIOS2
	#include <asm/termbits.h>	//including <asm/termios.h> causes duplicate def errors

	/* Ugliness necessary because :
	 0- <asm/termios.h>, or at least <asm/termbits.h> is needed for BOTHER and struct termios2
	 1- ioctl() is provided by glibc, and defined in <sys/ioctl.h>
	 2- <asm/ioctls.h> (included through sys/ioctl.h -> bits/ioctls.h -> asm/ioctls.h) is needed for TCSETS2
	 3- <asm/termios.h> defines winsize and some other junk covered by bits/ioctl-types;
	 3b- <bits/ioctl-types.h> is included by <sys/ioctl.h> !

	Could we someday have a sane way of setting integer baud rates ? pfah.
	 */
	extern int cfsetispeed(struct termios *__termios_p, speed_t __speed);
	extern int cfsetospeed(struct termios *__termios_p, speed_t __speed);
#else
	#include <termios.h>	//has speed_t, tcsetattr, struct termios, cfset*speed, etc
#endif // USE_TERMIOS2
/** END OF FUGLINESS **/

#include <sys/ioctl.h>

#if defined(_POSIX_TIMERS)
	#include <time.h>
#endif

#if defined(__linux__)
	#include <linux/rtc.h>
	#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
#endif

#include "diag_tty.h"

#define DL0D_INVALIDHANDLE -1


//struct tty_int : internal data, one per L0 struct
struct unix_tty_int {
	char *name;		/* port name */
	int fd;						/* File descriptor */

#if defined(__linux__)
	//struct serial_struct : only used with TIOCGSERIAL + TIOCSSERIAL ioctls,
	// which are not always available. Hence this flag:
	int tioc_works;		//indicate if TIOCGSERIAL + TIOCSSERIAL work
	//TODO : expand to a more general "detected tty capabilities" set of flags.

	/* For recording state before/while/after messing with the interface: */
	struct serial_struct ss_orig;	//original backup
	struct serial_struct ss_cur;	//current state

#endif

	//backup & current termios structs
	struct termios st_orig;
	struct termios st_cur;
#ifdef USE_TERMIOS2
	struct termios2 st2_cur;
	struct termios2 st2_orig;
#endif

	//flags backup (ioctl TIOCMGET, TIOCMSET)
	int modemflags;

#if defined(_POSIX_TIMERS)
	timer_t timerid;		//Used for read() and write() timeouts
	volatile sig_atomic_t pt_expired;	//flag timeout expiry
#endif

	unsigned long int byte_write_timeout_us; //single byte write timeout in microseconds
};

#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_TTY_UNIX_H_ */
