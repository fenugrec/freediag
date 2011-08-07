#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#if defined(__linux__) && (TRY_POSIX == 0)
#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
#endif

#include <sys/types.h>
#ifndef WIN32
#include <sys/ioctl.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <termios.h>	/* For struct termios */
#endif

#if !defined(__linux__) || (TRY_POSIX == 1)
#include <time.h>		/* For POSIX timers */
#endif

#include <linux/rtc.h>
#include <sys/time.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_err.h"
#include "diag_tty.h"

const struct diag_l0 *diag_l0_device_dl0(struct diag_l0_device *dl0d) {
	return dl0d->dl0;
}

int diag_tty_open(struct diag_l0_device **ppdl0d, 
const char *subinterface,
const struct diag_l0 *dl0,
void *dl0_handle)
{
	int rv;
	struct diag_ttystate	*dt;
	struct diag_l0_device *dl0d;

	char *endptr;
	int iInterface;

	const char *tty_template ="/dev/obdII%d";

	if (rv=diag_calloc(&dl0d, 1))		//free'd in diag_tty_close
		return diag_iseterr(rv);

	dl0d->fd = -1;
	dl0d->dl0_handle = dl0_handle;
	dl0d->dl0 = dl0;

	if ((rv=diag_calloc(&dl0d->ttystate, 1))) {
		free(dl0d);
		return diag_iseterr(rv);
	}

	*ppdl0d = dl0d;

	/*
	 * XXX this should probably be removed... "historical compatibility" with what ?? Who ?
	
	 * For historical compatibility, if the subinterface decodes cleanly
	 * as an integer we will write it into a string to get the name.
	 * You can create a symlink to "/dev/obdII<NUMBER>" if you want to,
	 * or just set the subinterface to a valid device name.
	 */

	iInterface = strtol(subinterface, &endptr, 10);
	if (*endptr == 0) {
		/* Entire string is a valid number: Provide compatibility.  */
		size_t n = strlen(tty_template) + 32;
		printf("Warning : using deprecated /dev/obdII<x> subinterface definition.\n");
		printf("Support for this will be discontinued shortly...\n");

		if ((rv=diag_malloc(&dl0d->name, n))) {
			(void)diag_tty_close(ppdl0d);;
			return diag_iseterr(rv);
		}
		(void)snprintf(dl0d->name, n, tty_template, iInterface);
	} else {
		size_t n = strlen(subinterface) + 1;

		if ((rv=diag_malloc(&dl0d->name, n))) {
			(void)diag_tty_close(ppdl0d);;
			return diag_iseterr(rv);
		}
		strncpy(dl0d->name, subinterface, n);
	}

	errno = 0;
#if defined(__linux__) && (TRY_POSIX == 0)
	dl0d->fd = open(dl0d->name, O_RDWR);
#else
	/*
	+* For POSIX behavior:  Open serial device non-blocking to avoid
	+* modem control issues, then set to blocking.
	 */
	/* CODE BLOCK */
	{
		int fl;
		dl0d->fd = open(dl0d->name, O_RDWR | O_NONBLOCK);

		if (dl0d->fd > 0) {
			errno = 0;
			if ((fl = fcntl(dl0d->fd, F_GETFL, 0)) < 0) {
				fprintf(stderr,
					FLFMT "Can't get flags with fcntl on fd %d: %s.\n",
					FL, dl0d->fd, strerror(errno));
				(void)diag_tty_close(ppdl0d);;
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			fl &= ~O_NONBLOCK;
			errno = 0;
			if (fcntl(dl0d->fd, F_SETFL, fl) < 0) {
				fprintf(stderr,
					FLFMT "Can't set flags with fcntl on fd %d: %s.\n",
					FL, dl0d->fd, strerror(errno));
				(void)diag_tty_close(ppdl0d);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}
	}
#endif

	if (dl0d->fd >= 0) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN)
			fprintf(stderr, FLFMT "Device %s opened, fd %d\n", 
				FL, dl0d->name, dl0d->fd);
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
	dt = dl0d->ttystate;

	/*
	 * Save original settings so can reset
	 * device on close - we also set "current" settings to
	 * those we just read aswell
	 */

#if defined(__linux__) && (TRY_POSIX == 0)
	if (ioctl(dl0d->fd, TIOCGSERIAL, &dt->dt_osinfo) < 0)
	{
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCGSERIAL failed %d\n", FL, errno);
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dt->dt_sinfo = dt->dt_osinfo;
#endif

	if (ioctl(dl0d->fd, TIOCMGET, &dt->dt_modemflags) < 0)
	{
		fprintf(stderr,
			FLFMT "open: Ioctl TIOCMGET failed: %s\n", FL, strerror(errno));
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (tcgetattr(dl0d->fd, &dt->dt_otinfo) < 0)
	{
		fprintf(stderr, FLFMT "open: tcgetattr failed %s\n",
			FL, strerror(errno));
		(void)diag_tty_close(ppdl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dt->dt_tinfo = dt->dt_otinfo;

	return 0;
}

/* Close up the TTY and restore. */
int diag_tty_close(struct diag_l0_device **ppdl0d)
{
	if (ppdl0d) {
		struct diag_l0_device *dl0d = *ppdl0d;
		if (dl0d) {
			if (dl0d->ttystate) {
				if (dl0d->fd != -1) {
			#if defined(__linux__) && (TRY_POSIX == 0)
					(void)ioctl(dl0d->fd,
						TIOCSSERIAL, &dl0d->ttystate->dt_osinfo);
			#endif

					(void)tcsetattr(dl0d->fd,
						TCSADRAIN, &dl0d->ttystate->dt_otinfo);
					(void)ioctl(dl0d->fd,
						TIOCMSET, &dl0d->ttystate->dt_modemflags);
				}
				free(dl0d->ttystate);
				dl0d->ttystate = 0;
			}

			if (dl0d->name) {
				free(dl0d->name);
				dl0d->name = 0;
			}
			if (dl0d->fd != -1) {
				(void)close(dl0d->fd);
				dl0d->fd = -1;
			}
			free(dl0d);
			*ppdl0d = 0;
		}
	}
	return 0;
}

void *
diag_l0_dl0_handle(struct diag_l0_device *dl0d) {
	return dl0d->dl0_handle;
}

struct diag_l2_link *
diag_l0_dl2_link(struct diag_l0_device *dl0d) {
	return dl0d->dl2_link;
}

void
diag_l0_set_dl2_link(struct diag_l0_device *dl0d,
	struct diag_l2_link *dl2_link) {
	dl0d->dl2_link = dl2_link;
}


/*
 * Set speed/parity etc
 */
int
diag_tty_setup(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pset)
{
	int iflag;
	int fd;
	struct diag_ttystate	*dt;

	fd = dl0d->fd;
	dt = dl0d->ttystate;
	if (fd == -1 || dt == 0) {
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
			FL, fd, dt);
		fprintf(stderr, "speed %d databits %d stopbits %d parity %d\n",
			pset->speed, pset->databits, pset->stopbits, pset->parflag);
	}

#if defined(__linux__)
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
#endif
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
	
	return 0;
}

/*
 * Set/Clear DTR and RTS lines, as specified
 */
int
diag_tty_control(struct diag_l0_device *dl0d,  int dtr, int rts)
{
	int flags;	/* Current flag values. */
	struct timeval tv;	//for getting timestamps
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
	if (ioctl(dl0d->fd, TIOCMGET, &flags) < 0) {
		fprintf(stderr, 
			FLFMT "open: Ioctl TIOCMGET failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	flags |= setflags;
	flags &= ~clearflags;

	if (ioctl(dl0d->fd, TIOCMSET, &flags) < 0) {
		fprintf(stderr, 
			FLFMT "open: Ioctl TIOCMSET failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	gettimeofday(&tv, NULL);
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "%04ld.%03ld : DTR/RTS changed\n", FL, tv.tv_sec, tv.tv_usec);
	}

	return 0;
}

/*
 * TTY handling routines
 */
//different tty_write and tty_read versions according to TRY_POSIX
#if defined(__linux__) && (TRY_POSIX == 0)

ssize_t
diag_tty_write(struct diag_l0_device *dl0d,
const void *buf, const size_t count)
{
	return write(dl0d->fd, buf, count);
}

#if 0
//old tty_read

/*
 * Non interruptible, sleeping posix like read() with a timeout
 * timeout is in ms. If timeout is 0, this acts as a non-blocking read,
 * and is used both to read from a TTY and to check for input available
 * at stdin.  I've split things out to separate the TTY reads from the
 * check for available input.
 * returns either 0 on succes, <0 on error, or # of bytes read on success. pah.
*/
ssize_t
diag_tty_read(struct diag_l0_device *dl0d, void *buf, size_t count, int timeout)
{
	struct timeval tv;
	int rv;

	if (diag_l0_debug & DIAG_DEBUG_READ) {
			fprintf(stderr, FLFMT "Entered diag_tty_read with count=%d, timeout=%dms", FL, count, timeout);
	}


	/*
	 * Do a select() with a timeout
	 */

	errno = 0;

	/*
	 * Portability :- select on linux updates the timeout when
	 * it is interrupted.
	 */

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	while ( 1 ) {
		fd_set set;

	 	FD_ZERO(&set);
		FD_SET(dl0d->fd, &set);

	        rv = select ( dl0d->fd + 1,  &set, NULL, NULL, &tv );

		if ( rv >= 0 ) break;

		if (errno != EINTR) break;

		errno = 0;
	}

	switch (rv) {
		case 0:
			/* Timeout */
			if (count)
				return diag_iseterr(DIAG_ERR_TIMEOUT);	//XXX who would sent count=0 ??
			return rv;
		case 1:
			/* Ready for read */
			rv = 0;
			/*
			 * XXX Won't you hang here if "count" bytes don't arrive?
			 * We've enabled SA_RESTART in the alarm handler, so this could
			 * never return.
			 */
			if (count)
				rv = read(dl0d->fd, buf, count);
			return rv;
		default:
			fprintf(stderr, FLFMT "select on fd %d returned %s.\n",
				FL, dl0d->fd, strerror(errno));
			/* Unspecific Error */
			return diag_iseterr(DIAG_ERR_GENERAL);
	}
}
#else
// goes with previous #if 0 (replacing old tty_read)

/*
 * We have to be read to loop in write since we've cleared SA_RESTART.
 */

//#if 1

ssize_t
diag_tty_read(struct diag_l0_device *dl0d, void *buf, size_t count, int timeout)
{
	struct timeval tv;
	int time,rv,fd,retval;
	unsigned long data;

	if (diag_l0_debug & DIAG_DEBUG_READ) {
			fprintf(stderr, FLFMT "Entered diag_tty_read with count=%d, timeout=%dms\n", FL, count, timeout);
	}

	errno = 0;
	time = 0;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	timeout *= 4096/2000;

	fd = open ("/dev/rtc", O_RDONLY);

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
		FD_SET(dl0d->fd, &set);

		rv = select ( dl0d->fd + 1,  &set, NULL, NULL, &tv ) ;

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
			fprintf(stderr, FLFMT "timed out: %dms\n",FL,timeout);
		}

	switch (rv)
	{
	case 0:
		/* Timeout */
	//this doesn't require a diag_iseterr() call, as is the case when diag_tty_read
	//is called to flush
		return (DIAG_ERR_TIMEOUT);
	case 1:
		/* Ready for read */
		rv = 0;
		/*
		 * XXX Won't you hang here if "count" bytes don't arrive?
		 * We've enabled SA_RESTART in the alarm handler, so this could
		 * never return.
		 */
		if (count)
			rv = read(dl0d->fd, buf, count);
		return (rv);

	default:
		fprintf(stderr, FLFMT "select on fd %d returned %s.\n",
			FL, dl0d->fd, strerror(errno));

		/* Unspecific Error */
		return (diag_iseterr(DIAG_ERR_GENERAL));
	}
}

//#else
#endif


#else
//# goes with previous #if linux && posix==0
ssize_t
diag_tty_write(struct diag_l0_device *dl0d,
const void *buf, const size_t count)
{
	ssize_t rv;
	ssize_t n;
	size_t c = count;
	const char *p;

	errno = 0;
	p = (const char *)buf;	/* For easy pointer I/O */
	n = 0;
	rv = 0;

	/* Loop until timeout or we've gotten something. */
	errno = 0;

	while (c > 0 &&
	(((rv = write(dl0d->fd,  p + n, c)) >= 0) ||
	(rv == -1 && errno == EINTR))) {
		if (rv == -1) {
			rv = 0;
			errno = 0;
		}
		c -= rv;
		n += rv;
	}

	if (n > 0 || rv >= 0) {
		return n;
	}

	fprintf(stderr, FLFMT "write to fd %d returned %s.\n",
		FL, dl0d->fd, strerror(errno));

	/* Unspecific Error */
	return diag_iseterr(DIAG_ERR_GENERAL);
}

/*
 * Use POSIX timers.
 */
ssize_t
diag_tty_read(struct diag_l0_device *dl0d, void *buf, size_t count, int timeout)
{
	ssize_t rv;
	ssize_t n;
	char *p;


#if defined(_POSIX_TIMERS)
	/*
	 * You have to create the timer at startup and then test this code.
	 */
#error "POSIX timer code not finished"
	/* Set our alarm to the timeout:
	 */
	struct itimerspec it;
	timerclear(&it.it_interval);
	timerclear(&it.it_value);

	tv.it_value.tv_sec = timeout / 1000;
	tv.it_value.tv_nsec = (timeout % 1000) * 1000000; 	/* ns */

	dl0d->expired = 0;							/* Clear flag */
	timer_settime(dl0d->timerid, 0, &tv, 0);	/* Arm timer */
#else
	/*
	 * No POSIX timers.  We're going to count on the alarm clock
	 * going off regularly to cause us to time out.
	 */
	struct timeval now, incr, then;

	dl0d->expired = 0;							/* Clear flag */
	(void)gettimeofday(&now, NULL);
	incr.tv_sec = timeout / 1000;
	incr.tv_usec = (timeout % 1000) * 1000;		/* us */
	timeradd(&now, &incr, &then);				/* Expiration time */
#if 0
	fprintf(stderr, "timeout %d now %d:%d incr %d:%d then %d:%d\n",
		timeout,
		now.tv_sec, now.tv_usec,
		incr.tv_sec, incr.tv_usec,
		then.tv_sec, then.tv_usec);
#endif
#endif
	
	errno = 0;
	p = (char *)buf;	/* For easy pointer I/O */
	n = 0;
	rv = 0;

	/* Loop until timeout or we've gotten something. */
	errno = 0;

	while (count > 0 &&
	dl0d->expired == 0 &&
	((rv = read(dl0d->fd,  p + n, count)) >= 0 ||
	(rv == -1 && errno == EINTR))) {
		if (rv == -1) {
			rv = 0;
			errno = 0;
		}
		count -= rv;
		n += rv;
#if !defined(_POSIX_TIMERS)
		(void)gettimeofday(&now, NULL);
		dl0d->expired = timercmp(&now, &then, >);
#if 0
		fprintf(stderr, "now %d:%d\n", now.tv_sec, now.tv_usec);
#endif
#endif
	}

	/*
	 * XXX I'm not exactly sure what we want here.  If we timeout and have
	 * read some characters, do we want to return that?  That's what
	 * I'm doing now.
	 */
	if (rv >= 0) {
		if (n > 0)
			return n;
		else if (dl0d->expired)
			return diag_iseterr(DIAG_ERR_TIMEOUT);
	}

	fprintf(stderr, FLFMT "read on fd %d returned %s.\n",
		FL, dl0d->fd, strerror(errno));

	/* Unspecific Error */
	return diag_iseterr(DIAG_ERR_GENERAL);
}
#endif


//different _iflush implementations (POSIX or not)
#if defined(__linux__) && (TRY_POSIX == 0)
/*
 * Original Linux input-flush implementation that uses the select timeout:
 */
int diag_tty_iflush(struct diag_l0_device *dl0d)
{
	char buf[MAXRBUF];
	int i, rv;

	/* Read any old data hanging about on the port */
	rv = diag_tty_read(dl0d, buf, sizeof(buf), 150);
	if ((rv > 0) && (diag_l0_debug & DIAG_DEBUG_OPEN))
	{
		fprintf(stderr, FLFMT "%d junk bytes discarded: ", FL,
			rv);
		for (i=0; i<rv; i++)
			fprintf(stderr, "0x%x ", buf[i] & 0xff); 
		fprintf(stderr,"\n");
	}

	return 0;
}

//#endif

#else
/*
 * POSIX serial I/O input flush:
 */
int diag_tty_iflush(struct diag_l0_device *dl0d) {
	errno = 0;
	if (tcflush(dl0d->fd, TCIFLUSH) < 0) {
		fprintf(stderr, FLFMT "TCIFLUSH on fd %d returned %s.\n",
			FL, dl0d->fd, strerror(errno));

		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}
#endif



//different tty_break implementations depending on __linux__ ; TRY_POSIX; TIOCSBRK; __CYGWIN__
#if defined(__linux__) && (TRY_POSIX == 0)

/*
 * diag_tty_break
 */
int diag_tty_break(struct diag_l0_device *dl0d, const int ms)
{
	char cbuf;
	struct timeval tv;
	int xferd;
	struct diag_serial_settings set;

	/*
	 *	We need to send an 25ms (+/- 1ms) break
	 *	and then wait 25ms. We must have waited Tidle (300ms) first
	 *
	 *	The trouble here is Linux doesn't let us be that accurate sending
	 *	a break - so we do it by sending a '0x00' at a baud rate that means
	 *	the output is low for 25ms, then we wait for 25ms, using busy
	 *	waits (which we get from being in real-time mode and doing nanosleeps
	 *	of less than 2ms each)
	 *
	 */

	/* Set baud rate etc to 360 baud, 8, N, 1 */
	set.speed = 360;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	diag_tty_setup(dl0d, &set);

	gettimeofday(&tv,NULL);
	/* Send a 0x00 byte message */
	diag_tty_write(dl0d, "", 1);
	
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "%04ld.%03ld : break start\n", FL, tv.tv_sec, tv.tv_usec);
	}

	/*
	 * And read back the single byte echo, which shows TX completes
 	 */
	while ( (xferd = diag_tty_read(dl0d, &cbuf, 1, 1000)) <= 0)
	{
		if (xferd == DIAG_ERR_TIMEOUT)
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		if (xferd == 0)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF.\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		if (errno != EINTR)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned error %s.\n", FL,
				strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}

	/* Now wait the requested number of ms */
	gettimeofday(&tv,NULL);
	diag_os_millisleep(ms);
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "%04ld.%03ld : end of break_L\n", FL, tv.tv_sec, tv.tv_usec);
	}

	return 0;
}
#elif defined(TIOCSBRK)
/*
 */
int diag_tty_break(struct diag_l0_device *dl0d, const int ms)
{
	if (tcdrain(dl0d->fd)) {
			fprintf(stderr, FLFMT "tcdrain returned %s.\n",
				FL, strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

	if (ioctl(dl0d->fd, TIOCSBRK, 0) < 0) {
		fprintf(stderr, 
			FLFMT "open: Ioctl TIOCSBRK failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	diag_os_millisleep(ms);

	if (ioctl(dl0d->fd, TIOCCBRK, 0) < 0) {
		fprintf(stderr, 
			FLFMT "open: Ioctl TIOCCBRK failed %s\n", FL, strerror(errno));
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
}
#elif defined(__CYGWIN__)
/* I really should submit patches to implement TIO[SC]BRK */

#include <io.h>
#include <w32api/windows.h>

#if NOWARNINGS != 1
#warning diag_tty_break on CYGWIN is untested but might work.
#endif

int diag_tty_break(struct diag_l0_device *dl0d, const int ms)
{
	HANDLE hd;
	long h;

	/*
	 * I'm going through this convoluted two-step conversion
	 * to avoid compiler warnings:
	 */

	h = get_osfhandle(dl0d->fd);
	hd = (HANDLE)h;

	if (tcdrain(dl0d->fd)) {
			fprintf(stderr, FLFMT "tcdrain returned %s.\n",
				FL, strerror(errno));
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

	SetCommBreak(hd);
	diag_os_millisleep(ms);
	ClearCommBreak(hd);

	return 0;
}
#else
/* On some systems, at least on AIX, you can use "tcsendbreak" because the
 * duration flag just happens to be the count in ms.  There is no good
 * POSIX way to get the short breaks, unfortunately.
 */
#error No known way to send short breaks on your system.
#endif
