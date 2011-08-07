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
 *
 * OS specific stuff
 * Originally LINUX specific stuff, but it is now a little more generic.
 *
 *
 * This code tweaks things to behave how we want it to, and does
 * very OS specific stuff
 *
 * We run the process in
 *	(1) Real time mode, as we need to do some very accurate sleeps
 *		for fast init purposes
 *	(2) As root in order to establish (1)
 *	(3) The os specific and IO driver code allows "interruptible syscalls"
 *		BSD and Linux defaults is that signals don't interrupt syscalls
 *			(i.e restartable system calls)
 *		SYSV does, so you see lots of code that copes with EINTR
 *
 */

#ifdef WIN32
#include <process.h>

//#include <winsock.h>

#else
#include <unistd.h>

#include <sys/ioctl.h>

#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "diag.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"

CVSID("$Id$");


int diag_os_init_done;

/*
 * SIGALRM handler.
+* XXX Should be replaced with Posix timers, where available.
+* Those are much better behaved, you get one handler per installed
+* handler.
+* Also, the current implementation uses non-async-signal-safe functions
+* in the signal handlers.  Their behavior is undefined if they happen
+* to occur during any other non-async-signal-safe function.
 */
#ifdef WIN32
void
diag_os_sigalrm(int unused)
#else
void
diag_os_sigalrm(int unused __attribute__((unused)))
#endif
{
	diag_l3_timer();	/* Call L3 Timer */
	diag_l2_timer();	/* Call L2 timers, which will call L1 timer */
}

int
diag_os_init(void)
{
#ifdef WIN32
	struct sigaction_t stNew;
#else
	struct sigaction stNew;
	struct itimerval tv;
#endif
	long tmo = 1;	/* 1 ms */

	if (diag_os_init_done)
		return(0);
	diag_os_init_done = 1;

	/*
	 * Install alarm handler
	 */
	memset(&stNew, 0, sizeof(stNew));
	stNew.sa_handler = diag_os_sigalrm;
	stNew.sa_flags = 0;

	/*
	 * I want to use POSIX timers to interrupt the reads, but I can't
	 * if SA_RESTART is in effect.  The best thing would be to use
	 * POSIX threads since the behavior is well-defined.
	 */
#if defined(__linux__) && (TRY_POSIX == 0)
	stNew.sa_flags = SA_RESTART;
#else
	stNew.sa_flags = 0;
#endif
	sigaction(SIGALRM, &stNew, NULL);

#ifndef WIN32
	/* 
	 * Start repeating timer
	 */
	tv.it_interval.tv_sec = tmo / 1000;	/* Seconds */
	tv.it_interval.tv_usec = (tmo % 1000) * 1000; 	/* ms */

	tv.it_value = tv.it_interval;

	setitimer(ITIMER_REAL, &tv, 0); /* Set timer */
#endif

	return(0);
}


//different os_millisleep implementations
#if defined(__linux__) && (TRY_POSIX == 0)

int
diag_os_millisleep(int ms)
{

	int i, fd, retval;
	unsigned long tmp,data;
	struct rtc_time rtc_tm;

	/* adjust time for 2048 rate */

	ms *= 4096/2000;

	if (ms > 2)
		ms-=2;

	fd = open ("/dev/rtc", O_RDONLY);

	if (fd ==  -1) {
		perror("/dev/rtc");
		exit(errno);
	}

	/* Read periodic IRQ rate */
	retval = ioctl(fd, RTC_IRQP_READ, &tmp);
	if (retval == -1) {
		perror("ioctl");
		exit(errno);
	}

	if (retval != 2048) {

		retval = ioctl(fd, RTC_IRQP_SET, 2048);
		if (retval == -1) {
			perror("ioctl");
			exit(errno);
		}
	}

	/* Enable periodic interrupts */
	retval = ioctl(fd, RTC_PIE_ON, 0);
	if (retval == -1) {
		perror("ioctl");
		exit(errno);
	}

	i = 0;
	while (1) {
		/* This blocks */
		retval = read(fd, &data, sizeof(unsigned long));
		if (retval == -1) {
			perror("read");
			exit(errno);
		}
		data >>= 8;
		i+=(int)data;
		if (i>=(ms*2))
			break;
	}

	/* Disable periodic interrupts */
	retval = ioctl(fd, RTC_PIE_OFF, 0);
	if (retval == -1) {
		perror("ioctl");
		exit(errno);
	}

	close(fd);

	return (0);
}

//old millisleep
#if 0

/*
+* Original LINUX implementation for a millisecond sleep:
 *
 * Sleep for N milliseconds
 *
 * This is aimed at small waits as it does very accurate "busy wait"
 * sleeping 2ms at a time (nanosleep does busy wait sleeps for <=2ms requests)
 *
+* XXX I'm not sure what the above comment means.  The POSIX definition
+* of nanosleep is:
+*
+* "The nanosleep() function shall cause the current thread to be
+* suspended from execution until hte time interval specified by the
+* rqtp argument has elapsed, a signal is delivered to the calling
+* thread and the action of the signal is to invoke a signal-catching
+* function, or the process is terminated"
+*
+* Note that this is thread specific, the process is suspended (it
+* doesn't busy wait), and signals wake it up again.  I've provided what
+* I think is the correct implementation below.  This one is much more overhead
+* than it needs to be.
 */
int
diag_os_millisleep(int ms)
{
	struct timespec rqst, resp;

	if (ms > 1)
		ms /= 2;

	while (ms)
	{
		if (ms > 2)
		{
			rqst.tv_nsec = 2000000;
			ms -= 2;

		}
		else
		{
			rqst.tv_nsec = ms * 1000000;
			ms = 0;
		}
		rqst.tv_sec = 0;

		while (nanosleep(&rqst, &resp) != 0)
		{
			if (errno == EINTR)
			{
				/* Interrupted, continue */
				memcpy(&rqst, &resp, sizeof(resp));
			}
			else
				return(-1);	/* Some other failure */

		}
	}

	return(0);
}

#endif

//following from initial "if linux && !posix" :
#else

/*
+* I think this implementation works in all cases, with less overhead.
 */
int
diag_os_millisleep(int ms) {
#ifndef WIN32
	struct timespec rqst, resp;

	rqst.tv_sec = ms / 1000;
	rqst.tv_nsec = (ms - (rqst.tv_sec * 1000)) * 1000000L;

	errno = 0;
	while (nanosleep(&rqst, &resp) == -1) {
		if (errno == EINTR) {
			rqst = resp;
			errno = 0;
		}
		else
			return -1;
	}
#else
    Sleep(ms);
#endif
	return 0;
}
#endif

/*
 * diag_os_ipending: Is input avilable at the given file descriptor?
 *
 * This is what is left of the read function that was used both
 * for the TTY read and to check for input at a file descriptor.
 */
int
diag_os_ipending(int fd)
{
	fd_set set;
	int rv;
	struct timeval tv;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	/*
	 * poll for input using select():
	 */
	errno = 0;
	rv = select(fd + 1,  &set, NULL, NULL, &tv);

	return rv == 1 ;
}

/* POSIX fixed priority scheduling. */

#ifdef _POSIX_PRIORITY_SCHEDULING
#include <sched.h>
int
diag_os_sched(void)
{
	int r = 0;
	struct sched_param p;

#ifndef __linux__
	/*
	+* If we're not running on Linux, we're not sure if what is
	+* being done is remotely applicable for our flavor of POSIX
	+* priority scheduling.
	+* For example, you set the scheduling priority to 1.  Ouch.
	 */
#if NOWARNINGS == 0
	#warning Scheduling setup should be examined
#endif
	/* Code block */
	{
		static int setup_warned;
		if (setup_warned == 0) {
			setup_warned = 1;
			fprintf(stderr,
				FLFMT "Scheduling setup should be examined.\n", FL);
		}
	}
#else
	/*
	 * Check privileges
	 */
	if (getuid() != 0)
	{
		static int suser_warned;
		if (suser_warned == 0) {
			suser_warned = 1;
			fprintf(stderr,
				FLFMT "WARNING: Not running as superuser\n", FL);
			fprintf(stderr,
				FLFMT "WARNING:  "
				"Could not set real-time mode.  "
				"Things will not work correctly\n", FL);
		}
	}
#endif

	/* Set real time UNIX scheduling */
	p.sched_priority = 1;
  	if ( sched_setscheduler(getpid(), SCHED_FIFO, &p) < 0)
	{
		fprintf(stderr, FLFMT "sched_setscheduler failed: %s.\n",
			FL, strerror(errno));
		r = -1;
	}
	return r;
}
#else
/*
+* This OS doesn't seem to have POSIX real time scheduling.
 */
int
diag_os_sched(void)
{
#ifdef WIN32

    //
    // Must start a callback timer. Not sure about the frequency yet.
    //

#else

#if NOWARNINGS == 0
	#warning No special scheduling support
#endif
	fprintf(stderr,
		FLFMT "diag_os_sched: No special scheduling support.\n", FL);
	return -1;
#endif
}
#endif
