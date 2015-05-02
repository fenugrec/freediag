/*
 * diag_tty_unix.c
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

#include <assert.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty_unix.h"

#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
volatile sig_atomic_t pt_expired;	//flag timeout expiry
static void
diag_tty_rw_timeout_handler(UNUSED(int sig))
{
	pt_expired = 1;
	return;
}
#endif

int diag_tty_open(struct diag_l0_device **ppdl0d,
	const char *subinterface,
	const struct diag_l0 *dl0,
	void *dl0_handle)
{
	int rv;
	struct diag_ttystate	*dt;
	struct diag_l0_device *dl0d;
	struct unix_tty_int *uti;
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	struct sigevent to_sigev;
	struct sigaction sa;
	clockid_t timeout_clkid;
#endif

	if ((rv=diag_calloc(&dl0d, 1)))		//free'd in diag_tty_close
		return diag_iseterr(rv);

	if ((rv=diag_calloc(&uti,1))) {
		free(dl0d);
		return diag_iseterr(rv);
	}

#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
	//set-up the r/w timeouts clock - here we just create it; it will be armed when needed
#ifdef _POSIX_MONOTONIC_CLOCK
	timeout_clkid = CLOCK_MONOTONIC;
#else
	timeout_clkid = CLOCK_REALTIME;
#endif // _POSIX_MONOTONIC_CLOCK
	sa.sa_flags = 0;
	sa.sa_handler = diag_tty_rw_timeout_handler;
	sigemptyset(&sa.sa_mask);
	if(sigaction(SIGUSR1, &sa, NULL) != 0) {
		fprintf(stderr, FLFMT "Could not set-up action for timeout timer... report this\n", FL);
		free(uti);
		free(dl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	to_sigev.sigev_notify = SIGEV_SIGNAL;
	to_sigev.sigev_signo = SIGUSR1;
	to_sigev.sigev_value.sival_ptr = &uti->timerid;
	if(timer_create(timeout_clkid, &to_sigev, &uti->timerid) != 0) {
		fprintf(stderr, FLFMT "Could not create timeout timer... report this\n", FL);
		free(uti);
		free(dl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
#endif

	dl0d->tty_int = uti;
	uti->fd = DL0D_INVALIDHANDLE;
	dl0d->dl0_handle = dl0_handle;
	dl0d->dl0 = dl0;

	if ((rv=diag_calloc(&uti->ttystate, 1))) {
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
		timer_delete(uti->timerid);
#endif
		free(uti);
		free(dl0d);
		return diag_iseterr(rv);
	}
	//past this point, we can call diag_tty_close(dl0d) to abort in case of errors

	*ppdl0d = dl0d;

	size_t n = strlen(subinterface) + 1;

	if ((rv=diag_malloc(&dl0d->name, n))) {
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(rv);
	}
	strncpy(dl0d->name, subinterface, n);


	errno = 0;

#if defined(O_NONBLOCK) && (SEL_TTYOPEN==S_ALT1 || SEL_TTYOPEN==S_AUTO)
	/*
	 * For POSIX behavior:  Open serial device non-blocking to avoid
	 * modem control issues, then set to blocking.
	 */
	{
		int fl;
		uti->fd = open(dl0d->name, O_RDWR | O_NONBLOCK);

		if (uti->fd > 0) {
			errno = 0;
			if ((fl = fcntl(uti->fd, F_GETFL, 0)) < 0) {
				fprintf(stderr,
					FLFMT "Can't get flags with fcntl on fd %d: %s.\n",
					FL, uti->fd, strerror(errno));
				(void)diag_tty_close(ppdl0d);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			fl &= ~O_NONBLOCK;
			errno = 0;
			if (fcntl(uti->fd, F_SETFL, fl) < 0) {
				fprintf(stderr,
					FLFMT "Can't set flags with fcntl on fd %d: %s.\n",
					FL, uti->fd, strerror(errno));
				(void)diag_tty_close(ppdl0d);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}
	}
#else
	#ifndef O_NONBLOCK
	#warning No O_NONBLOCK on your system ?! Please report this
	#endif
	uti->fd = open(dl0d->name, O_RDWR);

#endif // O_NONBLOCK

	if (uti->fd >= 0) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr, FLFMT "Device %s opened, fd %d\n",
				FL, dl0d->name, uti->fd);
	} else {
		fprintf(stderr,
			FLFMT "Open of device interface \"%s\" failed: %s\n",
			FL, dl0d->name, strerror(errno));
		fprintf(stderr, FLFMT
			"(Make sure the device specified corresponds to the\n", FL );
		fprintf(stderr,
			FLFMT "serial device your interface is connected to.\n", FL);

		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dt = uti->ttystate;

	/*
	 * Save original settings so can reset
	 * device on close - we also set "current" settings to
	 * those we just read aswell
	 */

#if defined(__linux__)
	if (ioctl(uti->fd, TIOCGSERIAL, &dt->dt_osinfo) < 0)
	{
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCGSERIAL failed %d\n", FL, errno);
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dt->dt_sinfo = dt->dt_osinfo;
#endif

	if (ioctl(uti->fd, TIOCMGET, &dt->dt_modemflags) < 0)
	{
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCMGET failed: %s\n", FL, strerror(errno));
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (tcgetattr(uti->fd, &dt->dt_otinfo) < 0)
	{
		fprintf(stderr, FLFMT "open: tcgetattr failed %s\n",
			FL, strerror(errno));
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dt->dt_tinfo = dt->dt_otinfo;

#if defined(_POSIX_TIMERS) || defined(__linux__)
	//arbitrarily set the single byte write timeout to 1ms (1000us)
	uti->byte_write_timeout_us = 1000ul;
#endif

	return 0;
}

/* Close up the TTY and restore. */
void diag_tty_close(struct diag_l0_device **ppdl0d)
{
	struct unix_tty_int *uti;
	if (ppdl0d) {
		struct diag_l0_device *dl0d = *ppdl0d;
		if (dl0d) {
			uti = (struct unix_tty_int *)dl0d->tty_int;
			if(uti) {
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
				timer_delete(uti->timerid);
#endif
				if (uti->ttystate) {
					if (uti->fd != DL0D_INVALIDHANDLE) {
				#if defined(__linux__)
						(void)ioctl(uti->fd,
							TIOCSSERIAL, &uti->ttystate->dt_osinfo);
				#endif

						(void)tcsetattr(uti->fd,
							TCSADRAIN, &uti->ttystate->dt_otinfo);
						(void)ioctl(uti->fd,
							TIOCMSET, &uti->ttystate->dt_modemflags);
					}
					free(uti->ttystate);
				}

				if (uti->fd != DL0D_INVALIDHANDLE) {
					(void)close(uti->fd);
				}

				free(uti);
			}

			if (dl0d->name) {
				free(dl0d->name);
			}

			free(dl0d);
			*ppdl0d = NULL;
		}
	}
	return;
}

/*
 * Set speed/parity etc, return 0 if ok
 */
int
diag_tty_setup(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pset)
{
	int iflag;
	int fd;
	struct diag_ttystate	*dt;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;

	fd = uti->fd;
	dt = uti->ttystate;
	if (fd == DL0D_INVALIDHANDLE || dt == 0) {
		fprintf(stderr, FLFMT "setup: something is not right\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Copy original settings to "current" settings */
	dt->dt_tinfo = dt->dt_otinfo;

	/*
	 * This sets the interface to the speed closest to that requested.
	 */
	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
	{
		fprintf(stderr, FLFMT "setup: device fd %d dt %p ",
			FL, fd, (void *)dt);
		fprintf(stderr, "speed %d databits %d stopbits %d parity %d\n",
			pset->speed, pset->databits, pset->stopbits, pset->parflag);
	}
/* Here starts baud rate hell. Status in 2015 seems to be :
	- ASYNC_SPD_CUST + B38400 + custom divisor trick is "deprecated",
	 and needs the TIOCSSERIAL ioctl (not ubiquitous)
	- take a chance with cfsetispeed & co with an integer argument ?
	 (non standard, shot in the dark except maybe on BSD??)
	- termios2 struct + BOTHER flag + TCSETS2 ioctl; questionable availability

 * TODO : try up to 3 techniques:
	1) check if standard baud rate => set it (easy)
	2) try hacks for non-standard speeds (hard)
	3) warn user; pick nearest std baud rate (error if out of range)
*/
#if defined(__linux__) && (SEL_TTYBAUD==S_ALT1 || SEL_TTYBAUD==S_AUTO)
	/*
	 * Linux iX86 method of setting non-standard baud rates:
	 *
	 * As it happens, all speeds on a Linux tty are calculated as a divisor
	 * of the base speed - for instance, base speed is normally 115200
	 * on a 16550 standard serial port
	 * this allows us to get
	 *	10472 baud  (115200 / 11)
	 *	       - NB, this is not +/- 0.5% as defined in ISO 14230 for
	 *		a tester, but it is if it was an ECU ... but that's a
	 *		limitation of the PC serial port ...
	 *	9600 baud  (115200 / 12)
	 *	5 baud	    (115200 / 23040 )
	 */
	/* Copy original settings to "current" settings */
	dt->dt_sinfo = dt->dt_osinfo;

	/* Now, mod the "current" settings */
	dt->dt_sinfo.custom_divisor = dt->dt_sinfo.baud_base  / pset->speed;

	/* Turn of other speed flags */
	dt->dt_sinfo.flags &= ~ASYNC_SPD_MASK;
	/*
	 * Turn on custom speed flags and low latency mode
	 */
	dt->dt_sinfo.flags |= ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;

	/* And tell the kernel the new settings */
	if (ioctl(fd, TIOCSSERIAL, &dt->dt_sinfo) < 0)
	{
		fprintf(stderr,
			FLFMT "Ioctl TIOCSSERIAL failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/*
	 * Set the baud rate and force speed to 38400 so that the
	 * custom baud rate stuff set above works
	 */
	dt->dt_tinfo.c_cflag &= ~CBAUD;
	dt->dt_tinfo.c_cflag |= B38400;	//see termios.h
#else
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
 	 * Since some interfaces (i.e., the BR1) work with standard buad rates,
	 * this is the fallback baud rate method.
	 */
	errno = 0;
	if (cfsetispeed(&dt->dt_tinfo, (speed_t)pset->speed) < 0) {
		fprintf(stderr,
			FLFMT "cfsetispeed failed: %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (cfsetospeed(&dt->dt_tinfo, (speed_t)pset->speed) < 0) {
		fprintf(stderr,
			FLFMT "cfsetospeed failed: %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
#endif // set baud rate
	errno = 0;
	if (tcsetattr(fd, TCSAFLUSH, &dt->dt_tinfo) < 0)
	{
		/* Its not clear to me why this call sometimes fails (clue:
			the error is usually "interrupted system call) but
			simply retrying is usually enough to sort it....    */
		int retries=9, result;
		do
		{
			fprintf(stderr, FLFMT "Couldn't set baud rate....retry %d\n", FL, retries);
			result = tcsetattr(fd, TCSAFLUSH, &dt->dt_tinfo);
		}
		while ((result < 0) && (--retries));
		if (result < 0)
		{
			// It just isn't working; give it up
			fprintf(stderr,
				FLFMT "Can't set baud rate to %d.\n"
				"tcsetattr returned \"%s\".\n", FL, pset->speed, strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}

	/* "stty raw"-like iflag settings: */
	iflag = dt->dt_tinfo.c_iflag;

	/* Clear a bunch of un-needed flags */

	iflag  &= ~ (IGNBRK | BRKINT | IGNPAR | PARMRK
		| INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF
		| IXANY | IMAXBEL);
#ifdef __linux__
	iflag  &= ~(IUCLC);   /* Not in Posix */
#endif

	dt->dt_tinfo.c_iflag  = iflag;

	dt->dt_tinfo.c_oflag &= ~(OPOST);

	/* Clear canonical input and disable keyboard signals.
	+* There is no need to also clear the many ECHOXX flags, both because
	+* many systems have non-POSIX flags and also because the ECHO
	+* flags don't don't matter when ICANON is clear.
	 */
	dt->dt_tinfo.c_lflag &= ~( ICANON | ISIG );

	/* CJH: However, taking 'man termios' at its word, the ECHO flag is
	     *not* affected by ICANON, and it seems we do need to clear it  */
	dt->dt_tinfo.c_lflag &= ~ECHO;

	/* Turn off RTS/CTS, and loads of others, similar to "stty raw" */
	dt->dt_tinfo.c_cflag &= ~( CRTSCTS );
	/* Turn on ... */
	dt->dt_tinfo.c_cflag |= (CLOCAL);

	dt->dt_tinfo.c_cflag &= ~CSIZE;
	switch (pset->databits) {
		case diag_databits_8:
			dt->dt_tinfo.c_cflag |= CS8;
			break;
		case diag_databits_7:
			dt->dt_tinfo.c_cflag |= CS7;
			break;
		case diag_databits_6:
			dt->dt_tinfo.c_cflag |= CS6;
			break;
		case diag_databits_5:
			dt->dt_tinfo.c_cflag |= CS5;
			break;
		default:
			fprintf(stderr, FLFMT "bad bit setting used (%d)\n", FL, pset->databits);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}
	switch (pset->stopbits) {
		case diag_stopbits_2:
			dt->dt_tinfo.c_cflag |= CSTOPB;
			break;
		case diag_stopbits_1:
			dt->dt_tinfo.c_cflag &= ~CSTOPB;
			break;
		default:
			fprintf(stderr, FLFMT "bad stopbit setting used (%d)\n",
				FL, pset->stopbits);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}

	switch (pset->parflag) {
		case diag_par_e:
			dt->dt_tinfo.c_cflag |= PARENB;
			dt->dt_tinfo.c_cflag &= ~PARODD;
			break;
		case diag_par_o:
			dt->dt_tinfo.c_cflag |= (PARENB | PARODD);
			break;
		case diag_par_n:
			dt->dt_tinfo.c_cflag &= ~PARENB;
			break;
		default:
			fprintf(stderr,
				FLFMT "bad parity setting used (%d)\n", FL, pset->parflag);
			return diag_iseterr(DIAG_ERR_GENERAL);
	}

	errno = 0;
	if (tcsetattr(fd, TCSAFLUSH, &dt->dt_tinfo) < 0) {
		fprintf(stderr,
			FLFMT
			"Can't set input flags (databits %d, stop bits %d, parity %d).\n"
			"tcsetattr returned \"%s\".\n",
			FL, pset->databits, pset->stopbits, pset->parflag,
			strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

#if defined(_POSIX_TIMERS) || defined(__linux__)
	//calculate write timeout for a single byte
	//gross bits per byte: 1 start bit + count of data bits + count of stop bits + parity bit, if set
	int gross_bits_per_byte = 1 + pset->databits + pset->stopbits + (pset->parflag == diag_par_n ? 0 : 1);
	//single byte timeout to: (gross_bits_per_byte / (baudrate[1/s]/1000000[us/s]))[us];
	uti->byte_write_timeout_us = (gross_bits_per_byte * 1000000ul / pset->speed);
#endif

	return 0;
}

/*
 * Set/Clear DTR and RTS lines, as specified
 */
int
diag_tty_control(struct diag_l0_device *dl0d,  unsigned int dtr, unsigned int rts)
{
	int flags;	/* Current flag values. */
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
	int setflags = 0, clearflags = 0;

	if (dtr)
		setflags = TIOCM_DTR;
	else
		clearflags = TIOCM_DTR;

	if (rts)
		setflags = TIOCM_RTS;
	else
		clearflags = TIOCM_RTS;

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

	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		unsigned long tc=diag_os_chronoms(0);
		fprintf(stderr, FLFMT "%lu : DTR/RTS changed\n", FL, tc);
	}

	return 0;
}

ssize_t
diag_tty_write(struct diag_l0_device *dl0d, const void *buf, const size_t count)
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
{
	ssize_t rv;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
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
	//set interval to 0 so that the timer expires just once
	it.it_interval.tv_sec = it.it_interval.tv_nsec = 0;

	//arm the timer
	timer_settime(uti->timerid, 0, &it, NULL);

	rv = write(uti->fd, p, count);
	if(rv < 0 && errno == EINTR) {
		//the timer has expired before any data were written
		errno = 0;
		return diag_iseterr(DIAG_ERR_TIMEOUT);
	}

	//disarm the timer in case it hasn't expired yet
	it.it_value.tv_sec = it.it_value.tv_nsec = 0;
	timer_settime(uti->timerid, 0, &it, NULL);

	//something has been written
	if(rv >= 0) {
		//wait until the data is transmitted
		tcdrain(uti->fd);
		return rv;
	}

	//errors other than EINTR
	fprintf(stderr, FLFMT "write to fd %d returned %s.\n", FL, uti->fd, strerror(errno));

	//unspecified error
	return diag_iseterr(DIAG_ERR_GENERAL);
}

#elif defined(__linux__) && (SEL_TIMEOUT==S_LINUX || SEL_TIMEOUT==S_AUTO)
//fallback code for linux. TODO: timeout using /dev/rtc

{
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
	return write(uti->fd, buf, count);
}

#else	 //no posix timers and it's not linux. TODO : regular signal handler?

{
	ssize_t rv;
	ssize_t n;
	size_t c = count;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
	const uint8_t *p;

	errno = 0;
	p = (const uint8_t *)buf;	/* For easy pointer I/O */
	n = 0;
	rv = 0;

	/* Loop until timeout or we've gotten something. */
	errno = 0;

	while (c > 0 &&
	(((rv = write(uti->fd,  &p[n], c)) >= 0) ||
	(rv == -1 && errno == EINTR))) {
		if (rv == -1) {
			rv = 0;
			errno = 0;
		}
		c -= rv;
		n += rv;
	}

	if (n > 0 || rv >= 0) {
		//wait until the data is transmitted
		tcdrain(uti->fd);
		return n;
	}

	fprintf(stderr, FLFMT "write to fd %d returned %s.\n",
		FL, uti->fd, strerror(errno));

	/* Unspecific Error */
	return diag_iseterr(DIAG_ERR_GENERAL);
}
#endif	//tty_write() implementations


ssize_t
diag_tty_read(struct diag_l0_device *dl0d, void *buf, size_t count, int timeout)
#if defined(_POSIX_TIMERS) && (SEL_TIMEOUT==S_POSIX || SEL_TIMEOUT==S_AUTO)
{
	ssize_t rv;
	size_t n;
	uint8_t *p;
	volatile int expired;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;

	struct itimerspec it;
	//the timeout
	it.it_value.tv_sec = timeout / 1000;
	it.it_value.tv_nsec = (timeout % 1000) * 1000000;
	//set interval to 0 so that the timer expires just once
	it.it_interval.tv_sec = it.it_interval.tv_nsec = 0;

	//arm the timer
	timer_settime(uti->timerid, 0, &it, NULL);

	n = 0;
	p = (uint8_t *)buf;
	expired = 0;
	errno = 0;

	while(n < count) {
		struct itimerspec curr_value;
		rv = read(uti->fd, &p[n], count-n);
		if(rv < 0) {
			//interrupted?
			if(errno == EINTR) {
				//not an error - just a timeout
				rv = 0;
				errno = 0;
				expired = 1;
			}
			break;
		} else
			n += rv;
		//check whether the time has finished
		timer_gettime(uti->timerid, &curr_value);
		if(curr_value.it_value.tv_sec == 0 && curr_value.it_value.tv_nsec == 0) {
			//if in the last call to read() we got all we wanted, then no timeout
			expired = n < count;
			break;
		}
	}

	//always disarm the timer
	it.it_value.tv_sec = it.it_value.tv_nsec = 0;
	timer_settime(uti->timerid, 0, &it, NULL);

	//if anything has been read, then return the number of read bytes; return timeout error otherwise
	if(rv >= 0) {
		if(n > 0)
			return n;
		else if(expired)
			return DIAG_ERR_TIMEOUT;
	}

	//errors other than EINTR
	fprintf(stderr, FLFMT "read on fd %d returned %s.\n", FL, uti->fd, strerror(errno));

	//Unspecified error
	return diag_iseterr(DIAG_ERR_GENERAL);
}

#elif defined(__linux__) && (SEL_TIMEOUT==S_LINUX || SEL_TIMEOUT==S_AUTO)//fallback code for linux

/*
 * We have to be read to loop in write since we've cleared SA_RESTART.
 *
 * Note : an old implementation used select() with a timeout. The flaw with that
 * is that select() "blocks the program until input or output is ready [...]
 * or until a timer expires, whichever comes first [...]. A file descriptor 
 * is considered ready for reading if a 'read' call will not block."
 * (source : manpages)
 * Problem: select() doesn't guarantee that (count) bytes are available !
 */

{
	struct timeval tv;
	int time,rv,fd,retval;
	unsigned long data;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;;

	assert(timeout < 10000);

	if (diag_l0_debug & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "Entered diag_tty_read with count=%u, timeout=%dms\n", FL,
			(unsigned int) count, timeout);
	}

	errno = 0;
	time = 0;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	timeout = (int)((unsigned long) timeout * 4096/2000);		//watch for overflow !

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
	time+=(int)data;

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
		time+=(int)data;
		if (time>=timeout)
			break;
	}

	/* Disable periodic interrupts */
	ioctl(fd, RTC_PIE_OFF, 0);
	close(fd);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		if (time>=timeout){
			fprintf(stderr, FLFMT "timed out: %dms\n",FL,timeout*2000/4096);
		}

	switch (rv)
	{
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
		 * We've enabled SA_RESTART in the alarm handler, so this could
		 * never return.
		 */
		if (count)
			rv = read(uti->fd, buf, count);
		return (rv);

	default:
		fprintf(stderr, FLFMT "select on fd %d returned %s.\n",
			FL, uti->fd, strerror(errno));

		/* Unspecific Error */
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
}	//diag_tty_read

#else //no posix timers and it's not linux
	//TODO : add another signal handler, similar to POSIX timeouts.
	//This, in its current form, cannot work reliably.
	//Plan B : make a ghetto implementation looping with { select() with a timeout;
	// read() 1 byte at a time ; manually check timeout}
{
	ssize_t rv;
	ssize_t n;
	uint8_t *p;
	unsigned long long tstart, incr, tdone, tdone_us;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;

	volatile int expired = 0;
	tstart=diag_os_gethrt();
	incr = timeout * 1000;	//us

	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, "timeout=%d, start=%llu, delta=%llu\n",
			timeout, tstart, incr);
	}

	errno = 0;
	p = (uint8_t *)buf;	/* For easy pointer I/O */
	n = 0;
	rv = 0;

	/* Loop until timeout or we've gotten something. */
	errno = 0;

	while (count > 0 && expired == 0) {

		rv = read(uti->fd,  &p[n], count);
		
		if ((rv == -1) && (errno == EINTR)) {
			rv = 0;
			errno=0;
		}
		if (rv < 0) {
			//real error
			break;
		}

		count -= rv;
		n += rv;

		tdone = diag_os_gethrt() - tstart;
		tdone_us = diag_os_hrtus(tdone);
		if (tdone_us >= incr) {
			expired = 1;
			rv=0;
		}
		if (diag_l0_debug & DIAG_DEBUG_TIMER) {
			fprintf(stderr, "%lluus elapsed\n", tdone_us);
		}
	}

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
}
#endif //_tty_read() implementations


/*
 * POSIX serial I/O input flush +
 * diag_tty_read with IFLUSH_TIMEOUT.
 * Ret 0 if ok
 */
int diag_tty_iflush(struct diag_l0_device *dl0d) {
	uint8_t buf[MAXRBUF];
	int rv;
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
	
	errno = 0;

	if (tcflush(uti->fd, TCIFLUSH) < 0) {
		fprintf(stderr, FLFMT "TCIFLUSH on fd %d returned %s.\n",
			FL, uti->fd, strerror(errno));
	}

	/* Read any old data hanging about on the port */
	rv = diag_tty_read(dl0d, buf, sizeof(buf), IFLUSH_TIMEOUT);
	if ((rv > 0) && (diag_l0_debug & DIAG_DEBUG_DATA)) {
		fprintf(stderr, FLFMT "tty_iflush: >=%d junk bytes discarded: 0x%X...\n", FL, rv, buf[0]);
//		diag_data_dump(stderr, buf, rv);		//could flood the screen
//		fprintf(stderr, "\n");
	}

	return 0;
} //diag_tty_iflush



// ideally use TIOCSBRK fs defined (probably in sys/ioctl.h)
int diag_tty_break(struct diag_l0_device *dl0d, const unsigned int ms)
{
#ifdef TIOCSBRK
// TIOCSBRK sounds like the ideal way to send a break. TIOCSBRK is not POSIX.
/*
 * This one returns right after clearing the break. This is more generic and
 * can be used to bit-bang a 5bps byte.
 */
	struct unix_tty_int *uti = (struct unix_tty_int *)dl0d->tty_int;
	if (tcdrain(uti->fd)) {
			fprintf(stderr, FLFMT "tcdrain returned %s.\n",
				FL, strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

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

	return diag_tty_fastbreak(dl0d, ms);
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
int diag_tty_fastbreak(struct diag_l0_device *dl0d, const unsigned int ms) {
	uint8_t cbuf;
	unsigned long long tv1,tv2,tvdiff;
	struct diag_serial_settings set;
	unsigned int msremain;

	if (ms<25)
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	/* Set baud rate etc to 360 baud, 8, N, 1 */
	set.speed = 360;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if (diag_tty_setup(dl0d, &set)) {
		fprintf(stderr, FLFMT "Could not set 360bps for fastbreak !\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	tv1 = diag_os_gethrt();
	/* Send a 0x00 byte message */
	diag_tty_write(dl0d, "", 1);
	//Alternate method ; we can write() ourselves and then tcdrain() to make
	//sure data is sent ?

	/*
	 * And read back the single byte echo, which shows TX completes
 	 */
	if (diag_tty_read(dl0d, &cbuf, 1, 1000) != 1) {
		fprintf(stderr, FLFMT "tty_fastbreak: echo read error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//we probably have a few ms left;
	//restore 10400bps:
	set.speed = 10400;
	if (diag_tty_setup(dl0d, &set)) {
		fprintf(stderr, FLFMT "Could not restore settings after fastbreak!\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Now wait the requested number of ms */
	tv2=diag_os_gethrt();
	tvdiff = diag_os_hrtus(tv2 - tv1);	//us

	if (tvdiff >= (ms*1000))
		return 0;	//already finished

	msremain = ms - (tvdiff / 1000);

	diag_os_millisleep(msremain);

	tv2=diag_os_gethrt();
	tvdiff = diag_os_hrtus(tv2 - tv1);	//us

	//XXX this message may need to be removed if timing is impaired
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "Fast break finished : tWUP=%llu\n", FL, tvdiff);
	}

	return 0;
}
