/*
 * diag_tty_unix.h
 *
 * This is totally unix-exclusive and should not be included directly;
 * other files should include diag_tty.h
 *
 * This file is part of freediag - Vehicle Diagnostic Utility
 *
 * Copyright (C) 2001-2004 ?
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
#elif defined(__linux__)
	#include <linux/rtc.h>
#endif
#if defined(__linux__) && (TRY_POSIX == 0)
	#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
#endif
#include "diag_tty.h"

#define DL0D_INVALIDHANDLE -1

struct diag_ttystate
{
	/*
	 * For recording state before we mess with the interface:
	 */
#if defined(__linux__) && (TRY_POSIX == 0)
	struct serial_struct dt_osinfo;
#endif
	struct termios dt_otinfo;
	int dt_modemflags;

	/* For recording state after/as we mess with the interface */
#if defined(__linux__) && (TRY_POSIX == 0)
	struct serial_struct dt_sinfo;
#endif
	struct termios dt_tinfo;

};

//struct tty_int : internal data, one per L0 struct
struct unix_tty_int {
	int fd;						/* File descriptor */
	struct diag_ttystate *ttystate;	/* Holds OS specific tty info */

#if defined(_POSIX_TIMERS)
	timer_t timerid;
#endif

#if defined(_POSIX_TIMERS) || defined(__linux__)
	unsigned long int byte_write_timeout_us; //single byte write timeuot in microseconds
#endif
};


#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_TTY_UNIX_H_ */
