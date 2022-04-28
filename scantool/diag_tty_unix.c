/*
 * diag_tty_unix.c
 *
 * This file is part of freediag - Vehicle Diagnostic Utility
 *
 * Copyright (C) 2001-2004 Richard Almeida & others
 * Copyright (C) 2004 Steve Baker <sjbaker@users.sourceforge.net>
 * Copyright (C) 2004 Steve Meisner <meisner@users.sourceforge.net>
 * Copyright (C) 2004 Vasco Nevoa <vnevoa@users.sourceforge.net>
 * Copyright (C) 2011-2016 fenugrec <fenugrec@users.sourceforge.net>
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

#include <assert.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_tty_unix.h"

#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
#define PT_REPEAT	1000	//after the nominal timeout period the timer will expire every PT_REPEAT us.
static void
diag_tty_rw_timeout_handler(UNUSED(int sig), siginfo_t *si, UNUSED(void *uc)) {
	assert(si->si_value.sival_ptr != NULL);
	((struct unix_tty_int *)(si->si_value.sival_ptr))->pt_expired = 1;
	return;
}
#endif

ttyp *diag_tty_open(const char *portname) {
	int rv;
	struct unix_tty_int *uti;
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	struct sigevent to_sigev;
	struct sigaction sa;
	clockid_t timeout_clkid;
#endif

	assert(portname);

	rv = diag_calloc(&uti,1);
	if (rv != 0) {
		return diag_pseterr(rv);
	}

#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	//set-up the r/w timeouts clock - here we just create it; it will be armed when needed
	#ifdef _POSIX_MONOTONIC_CLOCK
	timeout_clkid = CLOCK_MONOTONIC;
	#else
	timeout_clkid = CLOCK_REALTIME;
	#endif // _POSIX_MONOTONIC_CLOCK
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = diag_tty_rw_timeout_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		fprintf(stderr, FLFMT "Could not set-up action for timeout timer... report this\n", FL);
		free(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	to_sigev.sigev_notify = SIGEV_SIGNAL;
	to_sigev.sigev_signo = SIGUSR1;
	to_sigev.sigev_value.sival_ptr = uti;
	if (timer_create(timeout_clkid, &to_sigev, &uti->timerid) != 0) {
		fprintf(stderr, FLFMT "Could not create timeout timer... report this\n", FL);
		free(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}
#endif

	uti->fd = DL0D_INVALIDHANDLE;

	size_t n = strlen(portname) + 1;

	if ((rv=diag_malloc(&uti->name, n))) {
		free(uti);
		return diag_pseterr(rv);
	}
	strncpy(uti->name, portname, n);

	//past this point, we can call diag_tty_close(uti) to abort in case of errors

	errno = 0;

#if defined(O_NONBLOCK) && (SEL_TTYOPEN==S_ALT1 || SEL_TTYOPEN==S_AUTO)
	/*
	 * For POSIX behavior:  Open serial device non-blocking to avoid
	 * modem control issues, then set to blocking.
	 */
	{
		int fl;
		uti->fd = open(uti->name, O_RDWR | O_NONBLOCK);

		if (uti->fd > 0) {
			errno = 0;
			if ((fl = fcntl(uti->fd, F_GETFL, 0)) < 0) {
				fprintf(stderr,
					FLFMT "Can't get flags with fcntl on fd %d: %s.\n",
					FL, uti->fd, strerror(errno));
				diag_tty_close(uti);
				return diag_pseterr(DIAG_ERR_GENERAL);
			}
			fl &= ~O_NONBLOCK;
			errno = 0;
			if (fcntl(uti->fd, F_SETFL, fl) < 0) {
				fprintf(stderr,
					FLFMT "Can't set flags with fcntl on fd %d: %s.\n",
					FL, uti->fd, strerror(errno));
				diag_tty_close(uti);
				return diag_pseterr(DIAG_ERR_GENERAL);
			}
		}
	}
#else
	#ifndef O_NONBLOCK
	#warning No O_NONBLOCK on your system ?! Please report this
	#endif
	uti->fd = open(uti->name, O_RDWR);

#endif // O_NONBLOCK

	if (uti->fd >= 0) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			FLFMT "Device %s opened, fd %d\n",
			FL, uti->name, uti->fd);
	} else {
		fprintf(stderr,
			FLFMT "Could not open \"%s\" : %s. "
			"Make sure the device specified corresponds to the "
			"serial device your interface is connected to.\n",
			FL, uti->name, strerror(errno));
		diag_tty_close(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	/*
	 * Save original settings so can reset
	 * device on close - we also set "current" settings to
	 * those we just read aswell
	 */

#if defined(__linux__)
	if (ioctl(uti->fd, TIOCGSERIAL, &uti->ss_orig) < 0) {
		fprintf(stderr,
			FLFMT "open: TIOCGSERIAL failed: %s\n", FL, strerror(errno));
		uti->tioc_works = 0;
	} else {
		uti->ss_cur = uti->ss_orig;
		uti->tioc_works = 1;
	}
#endif

	if (ioctl(uti->fd, TIOCMGET, &uti->modemflags) < 0) {
		fprintf(stderr,
			FLFMT "open: TIOCMGET failed: %s\n", FL, strerror(errno));
		diag_tty_close(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

#ifdef 	USE_TERMIOS2
	rv = ioctl(uti->fd, TCGETS, &uti->st_orig);
#else
	rv = tcgetattr(uti->fd, &uti->st_orig);
#endif
	if (rv != 0) {
		fprintf(stderr, FLFMT "open: could not get orig settings: %s\n",
			FL, strerror(errno));
		diag_tty_close(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	//and set common flags
	uti->st_cur = uti->st_orig;

	/* "stty raw"-like iflag settings: */
	/* Clear a bunch of un-needed flags */
	uti->st_cur.c_iflag &= ~ (IGNBRK | BRKINT | IGNPAR | PARMRK
		| INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF
		| IXANY | IMAXBEL);
#ifdef __linux__
	uti->st_cur.c_iflag  &= ~(IUCLC);   /* non-posix; disable ucase/lcase conversion */
#endif

	uti->st_cur.c_oflag &= ~(OPOST);	//disable impl-defined output processing

	/* Disable canonical input and keyboard signals.
	+* There is no need to also clear the many ECHOXX flags, both because
	+* many systems have non-POSIX flags and also because the ECHO
	+* flags don't don't matter when ICANON is clear.
	 */
	/* CJH: However, taking 'man termios' at its word, the ECHO flag is
	 *not* affected by ICANON, and it seems we do need to clear it  */
	uti->st_cur.c_lflag &= ~( ICANON | ISIG | ECHO | IEXTEN);

	uti->st_cur.c_cflag &= ~( CRTSCTS );	//non-posix; disables hardware flow ctl
	uti->st_cur.c_cflag |= (CLOCAL | CREAD);	//ignore modem control lines; enable read

	uti->st_cur.c_cc[VMIN] = 1;		//Minimum # of bytes before read() returns (default: 0!!!)

	//and update termios with new flags.
#ifdef USE_TERMIOS2
	rv = ioctl(uti->fd, TCSETS, &uti->st_cur);
	rv |= ioctl(uti->fd, TCGETS2, &uti->st2_cur);
#else
	rv=tcsetattr(uti->fd, TCSAFLUSH, &uti->st_cur);
#endif
	if (rv != 0) {
		fprintf(stderr, FLFMT "open: can't set input flags: %s.\n",
				FL, strerror(errno));
		diag_tty_close(uti);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	//arbitrarily set the single byte write timeout to 1ms
	uti->byte_write_timeout_us = 1000ul;

	return uti;
}

/* Close up the TTY and restore. */
void diag_tty_close(ttyp *tty_int) {
	struct unix_tty_int *uti = tty_int;

	if (!uti) {
		return;
	}
	if (uti->name) {
		free(uti->name);
	}
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	timer_delete(uti->timerid);
#endif
	if (uti->fd != DL0D_INVALIDHANDLE) {
#if defined(__linux__)
		if (uti->tioc_works) {
			(void)ioctl(uti->fd, TIOCSSERIAL, &uti->ss_orig);
		}
#endif
#ifdef USE_TERMIOS2
		(void)ioctl(uti->fd, TCSETS2, &uti->st2_orig);
#else
		(void)tcsetattr(uti->fd, TCSADRAIN, &uti->st_orig);
#endif
		(void)ioctl(uti->fd, TIOCMSET, &uti->modemflags);
		(void)close(uti->fd);
	}

	free(uti);

	return;
}


/* Baud rate hell. Status in 2015 seems to be :
	- termios2 struct + BOTHER flag + TCSETS2 ioctl; questionable availability
	- ASYNC_SPD_CUST + B38400 + custom divisor trick is "deprecated",
	 and needs the linux TIOCSSERIAL ioctl (far from ubiquitous)
	- take a chance with cfsetispeed etc. with an integer argument, if
	 B9600 == 9600 (non standard, shot in the dark except maybe on BSD??)
	- use nearest standard speed + cfsetispeed
	- OSX >10.4 : (unconfirmed, TODO)	: IOSSIOSPEED ioctl ?
	- BSD ? (unconfirmed, TODO) : IOSSIOSPEED ioctl ?
*/
/** internal use : _tty_setspeed, used inside diag_tty_setup.
*
* @return  actual new speed, or 0 if failed.
* updates ->tty_int struct;
* should probably called last (i.e. after setting other flags)
*/
static int _tty_setspeed(ttyp *tty_int, unsigned int spd) {
	struct unix_tty_int *uti = tty_int;
	unsigned int	spd_real;	//validate baud rate precision
	struct termios st_new;
	int spd_done=0;	//flag success
	int rv = 1;
	int fd;

	fd = uti->fd;

	const unsigned int std_table[] = {
		0, 50, 75, 110,
		134, 150, 200, 300,
		600, 1200, 1800, 2400,
		4800, 9600, 19200, 38400,
	#ifdef B57600
		57600,
	#endif
	#ifdef B115200
		115200
	#endif
	};
	//std_names must match speeds in std_table !
	const speed_t std_names[] = {
		B0, B50, B75, B110,
		B134, B150, B200, B300,
		B600, B1200, B1800, B2400,
		B4800, B9600, B19200, B38400,
		#ifdef B57600
			B57600,
		#endif
		#ifdef B115200
			B115200
		#endif
	};

	st_new = uti->st_cur;

#if defined(__linux__) && defined(USE_TERMIOS2)
	/* Try setting BOTHER flag in .c_cflag, and literal speed in .c_ispeed */
	while (!spd_done) {
		struct termios2 st2;
		st2 = uti->st2_cur;
		st2.c_cflag &= ~CBAUD;
		st2.c_cflag |= BOTHER;
		st2.c_ispeed = spd;
		st2.c_ospeed = spd;
		if ((rv = ioctl(fd, TCSETS2, &st2)) != 0) {
			break;
		}
		//re-read to get actual speed
		if ((rv = ioctl(fd, TCGETS2, &uti->st2_cur)) != 0) {
			break;
		}
		spd_real = uti->st2_cur.c_ospeed;
		spd_done = 1;
		//Setting other flags without termios2 would "erase" speed,
		//unless TCGETS returns a "safe" termios?
		if ((rv = ioctl(fd, TCGETS, &uti->st_cur)) != 0) {
			break;
		}

		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
			FLFMT "Speed set using TCSETS + BOTHER.\n", FL);
		return spd_real;
	}
	if (rv != 0) {
		fprintf(stderr, FLFMT "setspeed(BOTHER) ioctl failed: %s.\n",
			FL, strerror(errno));
	}

#endif // BOTHER flag trick

#if defined(__linux__) && (SEL_TTYBAUD==S_ALT2 || SEL_TTYBAUD==S_AUTO)
//#pragma message("Warning : using deprecated ASYNC_SPD_CUST method as a fallback.")

	/*
	 * Linux iX86 method of setting non-standard baud rates.
	 * This method is apparently deprecated, or at the very least not recommended
	 * and definitely not supported by all hardware because of TIOCSSERIAL.
	 *
	 * Works by manually setting the baud rate divisor and special flags.
	 *
	 */
	if (uti->tioc_works && !spd_done) {
		struct serial_struct ss_new;
		/* Copy current settings to working copy */
		ss_new = uti->ss_cur;

		/* Now, mod the "current" settings */
		ss_new.custom_divisor = ss_new.baud_base  / spd;
		spd_real = ss_new.baud_base / ss_new.custom_divisor;

		/* Turn of other speed flags */
		ss_new.flags &= ~ASYNC_SPD_MASK;
		/*
		 * Turn on custom speed flags and low latency mode
		 */
		ss_new.flags |= ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;

		/* And tell the kernel the new settings */
		if (ioctl(fd, TIOCSSERIAL, &ss_new) < 0) {
			fprintf(stderr,
				FLFMT "setspeed(cust): ioctl failed: %s\n", FL, strerror(errno));
			return 0;
		}
		//sucess : update current settings
		uti->ss_cur = ss_new;

		/*
		 * Set the baud rate and force speed to 38400 so that the
		 * custom baud rate stuff set above works
		 */
		st_new.c_cflag &= ~CBAUD;
		st_new.c_cflag |= B38400;
		spd_done = 1;
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
			FLFMT "Speed set using TIOCSSERIAL + ASYNC_SPD_CUST.\n", FL);
	}
#endif	//deprecated ASYNC_SPD_CUST trick

	/* "POSIXY" version of setting non-standard baud rates.
	 * This works at least for FreeBSD.
	 *
	 * POSIX states that it is unspecified what will happen with an
	 * unsupported baud rate.  On FreeBSD, the baud rate defines match the
	 * baud rate (i.e., B9600 == 9600), and it attempts to set the baud
	 * rate you pass in, only complaining if the result is off by more
	 * than three percent.
	 *
	 * Unfortunately, I tried this on Mac OS X, and Mac OS X asserts that
	 * the baud rate must match one of the selections.  A little research
	 * shows that not only does their iokit driver Serial class permit
	 * only the specific baud rates in the termios header, but at least
	 * the sample driver asserts the requested baud rate is 50 or more.
	 * I don't have the source code for the driver for the Keyspan device
	 * I can't tell if it would work if I modified the iokit serial class.
	 *
	 * use either spd_nearest (currently == nearest Bxxx value) as-is,
	 * or use spd (only if B9600 == 9600, etc.)
	 */

	while (!spd_done) {
		errno = 0;
		int spd_nearest=0;	//index of nearest std value
		int32_t besterror=1000;

		for (size_t i=0; i< ARRAY_SIZE(std_table); i++) {
			int32_t test;
			test = 1000 * ((long int)spd - std_table[i]) / spd;
			test = (test >= 0)? test : -test;
			if (test < besterror) {
				besterror = test;
				spd_nearest = i;
				spd_real = std_table[i];
			}
		}

	#if (B9600 == 9600) && (SEL_TTYBAUD==S_ALT3 || SEL_TTYBAUD==S_AUTO)
		//try feeding the speed directly
		if ( !cfsetispeed(&st_new, spd) &&
				!cfsetospeed(&st_new, spd)) {
			spd_real = spd;
			spd_done = 1;
			DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
				FLFMT "Speed set with cfset*speed(uint).\n", FL);

			break;
		}
		fprintf(stderr,
				"cfset*speed with direct speed failed: %s\n", strerror(errno));
	#endif
		if ( !cfsetispeed(&st_new, std_names[spd_nearest]) &&
				!cfsetospeed(&st_new, std_names[spd_nearest])) {
			//spd_real already ok
			spd_done = 1;
			DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
				FLFMT "Speed set with cfset*speed(B%u).\n",
				FL, std_table[spd_nearest]);
			break;
		}
		fprintf(stderr,
				"cfset*speed with Bxxxx failed: %s\n", strerror(errno));
		break;
	}	//while !spd_done for cfset*speed attempts

	if (!spd_done) {
		fprintf(stderr, "Error : all attempts at changing speed failed !\n");
		return 0;
	}

#ifdef USE_TERMIOS2
	//should never get here anyway
	return 0;
#else
	errno = 0;
	for (int retries=1; retries <=10; retries++) {
		/* Apparently this sometimes failed with EINTR,
		 so we retry.
		 */

		rv=tcsetattr(fd, TCSAFLUSH, &st_new);
		if (rv == 0) {
			break;
		} else {
			fprintf(stderr, FLFMT "Couldn't set baud rate....retry %d\n", FL, retries);
			continue;
		}
	}

	if (rv != 0) {
		fprintf(stderr, FLFMT "Can't set baud rate to %u: %s.\n",
			FL, spd, strerror(errno));
		return 0;
	}
#endif
	return spd_real;

}
/*
 * Set speed/parity etc, return 0 if ok
 */
int
diag_tty_setup(ttyp *tty_int, const struct diag_serial_settings *pset) {
	int rv;
	int fd;
	struct unix_tty_int *uti = tty_int;
	struct termios st_new;
	unsigned int spd_real;
	long int spd_err;

	fd = uti->fd;

	assert(fd != DL0D_INVALIDHANDLE);

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "setup: fd=%d, %ubps, %d bits, %d stop, parity %d\n",
		FL, fd, pset->speed, pset->databits, pset->stopbits, pset->parflag);

	/* Copy current settings to working copy */
	st_new = uti->st_cur;
	st_new.c_cflag &= ~CSIZE;
	switch (pset->databits) {
		case diag_databits_8:
			st_new.c_cflag |= CS8;
			break;
		case diag_databits_7:
			st_new.c_cflag |= CS7;
			break;
		case diag_databits_6:
			st_new.c_cflag |= CS6;
			break;
		case diag_databits_5:
			st_new.c_cflag |= CS5;
			break;
		default:
			fprintf(stderr, FLFMT "bad bit setting used (%d)\n", FL, pset->databits);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}
	switch (pset->stopbits) {
		case diag_stopbits_2:
			st_new.c_cflag |= CSTOPB;
			break;
		case diag_stopbits_1:
			st_new.c_cflag &= ~CSTOPB;
			break;
		default:
			fprintf(stderr, FLFMT "bad stopbit setting used (%d)\n",
				FL, pset->stopbits);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}

	switch (pset->parflag) {
		case diag_par_e:
			st_new.c_cflag |= PARENB;
			st_new.c_cflag &= ~PARODD;
			break;
		case diag_par_o:
			st_new.c_cflag |= (PARENB | PARODD);
			break;
		case diag_par_n:
			st_new.c_cflag &= ~PARENB;
			break;
		default:
			fprintf(stderr,
				FLFMT "bad parity setting used (%d)\n", FL, pset->parflag);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}

	errno = 0;
#ifdef USE_TERMIOS2
	rv=ioctl(fd, TCSETS, &st_new);
	rv |= ioctl(fd, TCGETS2, &uti->st2_cur);
#else
	rv=tcsetattr(fd, TCSAFLUSH, &st_new);
#endif
	if (rv != 0) {
		fprintf(stderr,
			FLFMT
			"Can't set input flags (databits %d, stop bits %d, parity %d).\n"
			"tcsetattr returned \"%s\".\n",
			FL, pset->databits, pset->stopbits, pset->parflag,
			strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//update current settings
	uti->st_cur = st_new;

#if defined(_POSIX_TIMERS) || defined(__linux__)
	//calculate write timeout for a single byte
	//gross bits per byte: 1 start bit + count of data bits + count of stop bits + parity bit, if set
	int gross_bits_per_byte = 1 + pset->databits + pset->stopbits + (pset->parflag == diag_par_n ? 0 : 1);
	//single byte timeout to: (gross_bits_per_byte / (baudrate[1/s]/1000000[us/s]))[us];
	uti->byte_write_timeout_us = (gross_bits_per_byte * 1000000ul / pset->speed);
#endif

	spd_real = _tty_setspeed(uti, pset->speed);
	if (!spd_real) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//warn if actual speed is far off
	spd_err = ((long int)spd_real - pset->speed)*100 / pset->speed;
	spd_err = (spd_err >=0)? spd_err: -spd_err;
	if (spd_err >= 5) {
		fprintf(stderr, "Warning : speed off by >= 5%% !\n");
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "Speed set to %u, error~%ld%%\n",
		FL, spd_real, spd_err);

	return 0;
}	//diag_tty_setup

/*
 * Set/Clear DTR and RTS lines, as specified
 */
int
diag_tty_control(ttyp *tty_int,  unsigned int dtr, unsigned int rts) {
	int flags;	/* Current flag values. */
	struct unix_tty_int *uti = tty_int;
	int setflags = 0, clearflags = 0;

	if (dtr) {
		setflags = TIOCM_DTR;
	} else {
		clearflags = TIOCM_DTR;
	}

	if (rts) {
		setflags = TIOCM_RTS;
	} else {
		clearflags = TIOCM_RTS;
	}

	errno = 0;
	if (ioctl(uti->fd, TIOCMGET, &flags) < 0) {
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCMGET failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	flags |= setflags;
	flags &= ~clearflags;

	if (ioctl(uti->fd, TIOCMSET, &flags) < 0) {
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCMSET failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_TIMER, DIAG_DBGLEVEL_V,
		FLFMT "%lu : DTR/RTS changed\n", FL, diag_os_getms());

	return 0;
}

// diag_tty_write: return # of bytes written; <0 if error.
// In addition, this calculates + enforces a write timeout based on the number of bytes.
// But write timeouts should be very rare, and are considered an error
ssize_t
diag_tty_write(ttyp *tty_int, const void *buf, const size_t count) {
	assert(count > 0);
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	ssize_t rv;
	struct unix_tty_int *uti = tty_int;
	size_t n;
	const uint8_t *p;
	struct itimerspec it;

	errno = 0;
	p = (const uint8_t *)buf;
	rv = 0;

	//the timeout (the port is opened in blocking mode, and we don't want it to block indefinitely);
	//the single byte timeout * count of bytes + 10ms (10 thousand microseconds; an arbitrary value)
	long unsigned int timeout = uti->byte_write_timeout_us * count + 10000ul;
	it.it_value.tv_sec = (time_t)(timeout / 1000000ul);
	it.it_value.tv_nsec = (long)(timeout % 1000000ul)*1000l; //multiply by 1000 to get number of nanoseconds
	//set interval to PT_REPEAT to make sure we don't block inside read()
	it.it_interval.tv_sec=0;
	it.it_interval.tv_nsec = PT_REPEAT * 1000;

	uti->pt_expired=0;
	//arm the timer
	timer_settime(uti->timerid, 0, &it, NULL);

	n=0;

	while (n < count) {
		if (uti->pt_expired) {
			break;
		}

		rv = write(uti->fd, &p[n], count-n);
		if (rv < 0) {
			if (errno == EINTR) {
				//not an error, just interrupted (probably a signal handler)
				rv = 0;
				errno = 0;
			} else {
				//real error:
				break;
			}
		} else {
			n += rv;
		}
	}

	//disarm the timer in case it hasn't expired yet
	it.it_value.tv_sec = it.it_value.tv_nsec = 0;
	timer_settime(uti->timerid, 0, &it, NULL);

	if (rv < 0) {
		//errors other than EINTR
		fprintf(stderr, FLFMT "write to fd %d returned %s.\n", FL, uti->fd, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//wait until the data is transmitted
#ifdef USE_TERMIOS2
	/* no exact equivalent ioctl for tcdrain,
	  but TCSBRK with arg !=0 is "treated like tcdrain(fd)" according
	  to info tty_ioctl */
	if (ioctl(uti->fd, TCSBRK, 1) != 0) {
		static int tcsb_warned=0;
		if (!tcsb_warned) {
			fprintf(stderr, "TCSBRK doesn't work!\n");
		}
		tcsb_warned=1;
	}
#else
	tcdrain(uti->fd);
#endif

	return rv;
}	//_POSIX_TIMERS tty_write()

#elif (SEL_TIMEOUT==S_LINUX || SEL_TIMEOUT==S_OTHER || SEL_TIMEOUT==S_AUTO)
	/* No POSIX timers, this should be OK for everything else
	 * Technique: write loop, manually check timeout
	 */

	ssize_t rv = DIAG_ERR_GENERAL;
	ssize_t n;
	size_t c = count;
	struct unix_tty_int *uti = tty_int;
	const uint8_t *p;
	unsigned long long t1, t2;
	long unsigned int timeout = uti->byte_write_timeout_us * count + 10000ul;

	t1 = diag_os_gethrt();
	p = (const uint8_t *)buf;	/* For easy pointer I/O */
	n = 0;
	errno = 0;

	while (c > 0) {
		t2 = diag_os_hrtus(diag_os_gethrt() - t1);
		if (t2 >= timeout) {
			break;
		}

		rv = write(uti->fd,  &p[n], c);
		if (rv == -1 && errno == EINTR) {
			rv = 0;
			errno = 0;
			continue;
		}
		if (rv < 0) {
			fprintf(stderr, FLFMT "write error: %s.\n", FL, strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		c -= rv;
		n += rv;
	}

	if (n > 0 || rv >= 0) {
		//wait until the data is transmitted
#ifdef USE_TERMIOS2
		/* no exact equivalent ioctl for tcdrain, but
		 "TCSBRK : [...] treat tcsendbreak(fd,arg) with nonzero arg like tcdrain(fd)."
		 */
		ioctl(uti->fd, TCSBRK, 1);
#else
		tcdrain(uti->fd);
#endif
		return n;
	}

	fprintf(stderr, FLFMT "write to fd %d returned %s.\n",
		FL, uti->fd, strerror(errno));

	/* Unspecific Error */
	return diag_iseterr(DIAG_ERR_GENERAL);
}	//S_OTHER || S_LINUX write implem
#else
	#error Fell in the cracks of implementation selectors !
#endif	//tty_write() implementations


ssize_t
diag_tty_read(ttyp *tty_int, void *buf, size_t count, unsigned int timeout) {
	assert((count > 0) && ( timeout > 0) && (timeout < MAXTIMEOUT));
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	ssize_t rv;
	size_t n;
	int expired;
	uint8_t *p;
	struct unix_tty_int *uti = tty_int;

	struct itimerspec it;

	//the timeout
	it.it_value.tv_sec = timeout / 1000;
	it.it_value.tv_nsec = (timeout % 1000) * 1000000;
	//set interval to PT_REPEAT to make sure we don't block inside read()
	it.it_interval.tv_sec=0;
	it.it_interval.tv_nsec = PT_REPEAT * 1000;

	uti->pt_expired=0;
	expired=0;

	//arm the timer
	timer_settime(uti->timerid, 0, &it, NULL);

	n = 0;
	p = (uint8_t *)buf;
	errno = 0;

	while (n < count) {
		if (uti->pt_expired) {
			expired=1;
			break;
		}

		rv = read(uti->fd, &p[n], count-n);
		if (rv < 0) {
			if (errno == EINTR) {
				//not an error, just an interrupted syscall
				rv = 0;
				errno = 0;
			} else {
				//real error:
				break;
			}
		} else {
			n += rv;
		}
	}

	//always disarm the timer
	it.it_value.tv_sec = it.it_value.tv_nsec = 0;
	timer_settime(uti->timerid, 0, &it, NULL);

	//if anything has been read, then return the number of read bytes; return timeout error otherwise
	if (rv >= 0) {
		if (n > 0) {
			return n;
		}
		if (expired) {
			return DIAG_ERR_TIMEOUT; // without diag_iseterr() !
		}
	}

	//errors other than EINTR
	fprintf(stderr, FLFMT "read on fd %d returned %s.\n", FL, uti->fd, strerror(errno));

	//Unspecified error
	return diag_iseterr(DIAG_ERR_GENERAL);
}	//POSIX read implem

#elif (SEL_TIMEOUT==S_OTHER || SEL_TIMEOUT == S_AUTO)
 //no posix timers and it's not linux
	//Loop with { select() with a timeout;
	// read() ; manually check timeout}

	ssize_t rv = DIAG_ERR_GENERAL;
	ssize_t n;
	uint8_t *p;
	unsigned long long tstart, incr, tdone, tdone_us;
	struct unix_tty_int *uti = tty_int;

	int expired = 0;
	tstart=diag_os_gethrt();
	incr = timeout * 1000;	//us

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_TIMER, DIAG_DBGLEVEL_V,
		"timeout=%u, start=%llu, delta=%llu\n", timeout, tstart, incr);

	errno = 0;
	p = (uint8_t *)buf;	/* For easy pointer I/O */
	n = 0;

	while (count > 0 && expired == 0) {
		//select() loop to ensure read() won't block:
		while ( !expired ) {
			fd_set set;
			struct timeval tv;
			unsigned long long rmn;

			tdone = diag_os_gethrt() - tstart;
			tdone_us = diag_os_hrtus(tdone);

			if (tdone_us >= incr) {
				expired = 1;
				rv=0;
				goto finished;
			}
			rmn = timeout*1000 - tdone_us;	//remaining before timeout

			FD_ZERO(&set);
			FD_SET(uti->fd, &set);

			tv.tv_sec = rmn / (1000*1000);
			tv.tv_usec = rmn % (1000*1000);

			rv = select( uti->fd + 1,  &set, NULL, NULL, &tv );
			// 4 possibilities here:
			//	EINTR => retry
			//	FD ready => break
			//	timed out => retry (will DIAG_ERR_TIMEOUT)
			//	other errors => diag_iseterr
			if ((rv < 0) && (errno == EINTR)) {
				rv=0;
				errno=0;
				continue;
			}
			if (FD_ISSET(uti->fd, &set)) {
				//fd is ready:
				break;
			}
			if (rv < 0) {
				fprintf(stderr, FLFMT "select() error: %s.\n", FL, strerror(errno));
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}	//select loop

		rv = read(uti->fd,  &p[n], count);

		if ((rv < 0) && (rv == EINTR)) {
			rv=0;
			errno=0;
			continue;
		}

		if (rv<=0) {
			fprintf(stderr, FLFMT "read() says %ld: %s.\n", FL, (long) rv, strerror(errno));
			break;
		}

		count -= rv;
		n += rv;
	}	//total read loop
finished:
	if (rv >= 0) {
		if (n > 0)
			return n;
		else if (expired)
			return DIAG_ERR_TIMEOUT;
	}

	fprintf(stderr, FLFMT "read() returned %s.\n",
		FL, strerror(errno));

	/* Unspecified Error */
	return diag_iseterr(DIAG_ERR_GENERAL);
}	//S_OTHER read implem

#elif defined(__linux__) && (SEL_TIMEOUT==S_LINUX || SEL_TIMEOUT==S_AUTO)

/*
 * We have to read to loop since we've cleared SA_RESTART.
 *
 * This implementation uses /dev/rtc to time out, but seems flawed : it
 * also relies on select() to guarantee that (count) bytes are available.
 *
 * Also, it calls select() very very often (why is tv={0} ?)
 */

	struct timeval tv;
	unsigned int time;
	int rv,fd,retval;
	unsigned long data;
	struct unix_tty_int *uti = tty_int;;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "Entered diag_tty_read with count=%u, timeout=%ums\n",
		FL, (unsigned int) count, timeout);

	errno = 0;
	time = 0;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	timeout = (int)((unsigned long) timeout * 2048/1000);		//watch for overflow !

	fd = open ("/dev/rtc", O_RDONLY);
	if (fd <=0) {
		fprintf(stderr, FLFMT "diag_tty_read: error opening /dev/rtc !\n", FL);
		perror("\t");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Read periodic IRQ rate */
	retval = ioctl(fd, RTC_IRQP_READ, &data);

	if (retval != 2048)
		ioctl(fd, RTC_IRQP_SET, 2048);

	/* Enable periodic interrupts */
	ioctl(fd, RTC_PIE_ON, 0);

	read(fd, &data, sizeof(unsigned long));
	data >>= 8;
	time+=data;

	while ( 1 ) {
		fd_set set;

		FD_ZERO(&set);
		FD_SET(uti->fd, &set);

		rv = select ( uti->fd + 1,  &set, NULL, NULL, &tv ) ;

		if ( rv > 0 ) break ;

		if (errno != 0 && errno != EINTR) break;
		errno = 0 ;

		read(fd, &data, sizeof(unsigned long));
		data >>= 8;
		time+=data;
		if (time>=timeout)
			break;
	}

	/* Disable periodic interrupts */
	ioctl(fd, RTC_PIE_OFF, 0);
	close(fd);


	if (time>=timeout) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
			FLFMT "timed out: %ums\n",FL,timeout*1000/2048);
	}

	switch (rv) {
	case 0:
		/* Timeout */
	//this doesn't require a diag_iseterr() call, as is the case when diag_tty_read
	//is called to flush
		return DIAG_ERR_TIMEOUT;
	case 1:
		/* Ready for read */
		rv = 0;
		/*
		 * XXX Won't you hang here if "count" bytes don't arrive?
		 * XXX Yes, possibly !
		 */
		rv = read(uti->fd, buf, count);
		if (rv <= 0) {
			DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
				"read() returned %d?", rv);
		}
		return rv;

	default:
		fprintf(stderr, FLFMT "select on fd %d returned %s.\n",
			FL, uti->fd, strerror(errno));

		/* Unspecific Error */
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
}	//S_LINUX read implem

#else
	#error Fell in the cracks of implementation selectors !
#endif //_tty_read() implementations


/*
 * POSIX serial I/O input flush +
 * diag_tty_read with IFLUSH_TIMEOUT.
 * Ret 0 if ok
 */
int diag_tty_iflush(ttyp *tty_int) {
	uint8_t buf[MAXRBUF];
	int rv;
	struct unix_tty_int *uti = tty_int;

	errno = 0;

#ifdef USE_TERMIOS2
	rv=ioctl(uti->fd, TCFLSH, TCIFLUSH);
#else
	rv=tcflush(uti->fd, TCIFLUSH);
#endif // USE_TERMIOS2
	if ( rv != 0) {
		fprintf(stderr, FLFMT "TCIFLUSH on fd %d returned %s.\n",
			FL, uti->fd, strerror(errno));
	}

	/* Read any old data hanging about on the port */
	rv = diag_tty_read(uti, buf, sizeof(buf), IFLUSH_TIMEOUT);
	if (rv > 0) {
		//not dumping data : could flood screen
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_DATA, DIAG_DBGLEVEL_V,
			FLFMT "tty_iflush: >=%d junk bytes discarded: 0x%X...\n",
			FL, rv, buf[0]);
	}

	return 0;
} //diag_tty_iflush



// ideally use TIOCSBRK, if defined (probably in sys/ioctl.h)
int diag_tty_break(ttyp *tty_int, const unsigned int ms) {
#ifdef TIOCSBRK
// TIOCSBRK: set TX break until TIOCCBRK. Ideal for our use but not in POSIX.
/*
 * This one returns right after clearing the break. This is more generic and
 * can be used to bit-bang a 5bps byte.
 */
	struct unix_tty_int *uti = tty_int;
#ifdef USE_TERMIOS2
	/* no exact equivalent ioctl for tcdrain, but
	 "TCSBRK : [...] treat tcsendbreak(fd,arg) with nonzero arg like tcdrain(fd)."
	 */
	ioctl(uti->fd, TCSBRK, 1);
#else
	if (tcdrain(uti->fd)) {
		fprintf(stderr, FLFMT "tcdrain returned %s.\n",
			FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
#endif

	if (ioctl(uti->fd, TIOCSBRK, 0) < 0) {
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCSBRK failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	diag_os_millisleep(ms);

	if (ioctl(uti->fd, TIOCCBRK, 0) < 0) {
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCCBRK failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;

#else
#warning ******* Dont know how to implement diag_tty_break() on your OS !
#warning ******* Compiling diag_tty_break with a fixed 25ms setbreak !
#warning ******* DUMB interfaces may not work properly !!
	if (ms<25)
		return 0;

	return diag_tty_fastbreak(uti, ms);
#endif	//if .. for diag_tty_break
}	//diag_tty_break



/*
 * diag_tty_fastbreak
 * fixed 25ms (1 byte 0x00 @ 360bps !) break and returns [ms] after starting the break.
 * Assumes half-duplex interface of course;
 * it also sets 10.4kbps 8N1, hardcoded. XXX find a way to make this neater..
 * we'll probably have to add a ->pset member to diag_l0_device to store the
 * "desired" setting. And use that to call diag_tty_setup to restore settings...
 */
int diag_tty_fastbreak(ttyp *tty_int, const unsigned int ms) {
	struct unix_tty_int *uti=tty_int;
	uint8_t cbuf;
	unsigned long long tv1,tv2,tvdiff;
	struct diag_serial_settings set;
	unsigned int msremain;

	if (ms < 25) {
		return diag_iseterr(DIAG_ERR_TIMEOUT);
	}

	/* Set baud rate etc to 360 baud, 8, N, 1 */
	set.speed = 360;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if (diag_tty_setup(uti, &set)) {
		fprintf(stderr, FLFMT "Could not set 360bps for fastbreak !\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	tv1 = diag_os_gethrt();
	/* Send a 0x00 byte message */
	diag_tty_write(uti, "", 1);
	//Alternate method ; we can write() ourselves and then tcdrain() to make
	//sure data is sent ?

	/*
	 * And read back the single byte echo, which shows TX completes
 	 */
	if (diag_tty_read(uti, &cbuf, 1, 1000) != 1) {
		fprintf(stderr, FLFMT "tty_fastbreak: echo read error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//we probably have a few ms left;
	//restore 10400bps:
	set.speed = 10400;
	if (diag_tty_setup(uti, &set)) {
		fprintf(stderr, FLFMT "Could not restore settings after fastbreak!\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Now wait the requested number of ms */
	tv2=diag_os_gethrt();
	tvdiff = diag_os_hrtus(tv2 - tv1);	//us

	if (tvdiff >= (ms * 1000)) {
		return 0; // already finished
	}

	msremain = ms - (tvdiff / 1000);

	diag_os_millisleep(msremain);

	tv2=diag_os_gethrt();
	tvdiff = diag_os_hrtus(tv2 - tv1);	//us

	//XXX this message may need to be removed if timing is impaired
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_TIMER, DIAG_DBGLEVEL_V,
		FLFMT "Fast break finished : tWUP=%llu\n", FL, tvdiff);

	return 0;
}

//ret true if pname is a tty
static bool test_ttyness(const char *pname) {
	int testfd = -1;				// file descriptor for tested device files
	bool yes=0;

	testfd = open(pname, O_RDWR | O_NOCTTY | O_NDELAY);
	if (testfd != -1) {
		if (isatty(testfd)) {
			yes = 1;
		}
		close(testfd);
	}
	return yes;
}

/* To find available ports, iterate in /dev/ and /dev/usb/
 * to find & test possible port names.
 * Adapted from FreeSSM :
 * https://github.com/Comer352L/FreeSSM
 */
char **diag_tty_getportlist(int *numports) {
	char ffn[256] = "";				// full filename incl. path
	const char *devroot="/dev/";
	const char *devusbroot="/dev/usb";
	DIR *dp = NULL;
	struct dirent *fp = NULL;
	char **portlist = NULL;
	int elems;	//temp number of ports

	assert(numports != NULL);
	*numports = 0;
	elems = 0;

	/* 1: iterate in /dev/ */
	dp = opendir (devroot);
	if (dp != NULL) {
		while (1) {
			fp = readdir (dp);	// get next file in directory
			if (fp == NULL) {
				break;
			}
			if ((!strncmp(fp->d_name,"ttyS",4)) ||
					(!strncmp(fp->d_name,"ttyUSB",6)) ||
					(!strncmp(fp->d_name,"ttyACM",6))) {
				// CONSTRUCT FULL FILENAME:
				strcpy(ffn, devroot);
				strncat(ffn, fp->d_name, ARRAY_SIZE(ffn) - strlen(devroot) - 1);

				if (!test_ttyness(ffn)) {
					continue;
				}

				char **templist = strlist_add(portlist, ffn, elems);
				if (!templist) {
					strlist_free(portlist, elems);
					return diag_pseterr(DIAG_ERR_NOMEM);
				}
				portlist = templist;
				elems++;
			}
		}
		closedir (dp);
	}

	/* iterate in /dev/usb */
	dp = opendir (devusbroot);
	if (dp != NULL) {
		while (1) {
			fp = readdir (dp);	// get next file in directory
			if (fp == NULL) {
				break;
			}
			if (!strncmp(fp->d_name,"ttyUSB",6)) {
				// CONSTRUCT FULL FILENAME:
				strcpy(ffn, devusbroot);
				strncat(ffn, fp->d_name, ARRAY_SIZE(ffn) - strlen(devusbroot) - 1);

				if (!test_ttyness(ffn)) {
					continue;
				}

				char **templist = strlist_add(portlist, ffn, elems);
				if (!templist) {
					strlist_free(portlist, elems);
					return diag_pseterr(DIAG_ERR_NOMEM);
				}
				portlist = templist;
				elems++;
			}
		}	//while
		closedir (dp);
	}

	*numports = elems;
	return portlist;
}

