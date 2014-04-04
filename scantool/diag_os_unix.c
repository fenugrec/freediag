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


#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "diag_tty.h"
#include "diag.h"

#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>



static int diag_os_init_done=0;

/*
 * SIGALRM handler.
+* XXX Should be replaced with Posix timers, where available.
+* Those are much better behaved, you get one handler per installed
+* handler.
+* Also, the current implementation uses non-async-signal-safe functions
+* in the signal handlers.  Their behavior is undefined if they happen
+* to occur during any other non-async-signal-safe function.
 */

void
diag_os_sigalrm(UNUSED(int unused))
{
	diag_l3_timer();	/* Call L3 Timer */
	diag_l2_timer();	/* Call L2 timers, which will call L1 timer */
}

//diag_os_init : a bit of a misnomer. This sets up a periodic callback
//to call diag_l3_timer and diag_l2_timer; that would sound like a job
//for "diag_os_sched". The WIN32 version of diag_os_init also
//calls diag_os_sched to increase thread priority.
//return 0 if ok
int
diag_os_init(void)
{
	struct sigaction stNew;
	struct itimerval tv;
	long tmo = ALARM_TIMEOUT;

	if (diag_os_init_done)
		return 0;
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
#endif

	sigaction(SIGALRM, &stNew, NULL);	//install handler for SIGALRM
	/*
	 * Start repeating timer
	 */
	tv.it_interval.tv_sec = tmo / 1000;	/* Seconds */
	tv.it_interval.tv_usec = (tmo % 1000) * 1000; 	/* ms */

	tv.it_value = tv.it_interval;

	setitimer(ITIMER_REAL, &tv, 0); /* Set timer : it will SIGALRM upon expiration */
	diag_os_calibrate();
	diag_os_init_done = 1;
	return 0;
}	//diag_os_init

//diag_os_close: delete alarm handlers / periodic timers
//return 0 if ok
int diag_os_close() {
	//I know nothing about unix timers. Viewer discretion is advised.
	//stop the interval timer:
	struct itimerval tv={{0,0},{0, 0}};
	setitimer(ITIMER_REAL, &tv, 0);

	//and  set the SIGALRM handler to default, whatever that is
	struct sigaction disable_tmr;
	memset(&disable_tmr, 0, sizeof(disable_tmr));
	disable_tmr.sa_handler=SIG_DFL;
	sigaction(SIGALRM, &disable_tmr, NULL);
	return 0;

} 	//diag_os_close


//different os_millisleep implementations
//TODO : add timing verification for *unix

int
diag_os_millisleep(unsigned int ms)
{
#if defined(__linux__) && (TRY_POSIX == 0)

	int fd, retval;
	unsigned int i;
	unsigned long tmp,data;
	//struct rtc_time rtc_tm;	//not used ?

	/* adjust time for 2048 rate */

	ms = (unsigned int)((unsigned long) ms* 4096/2000);	//if we do it as uint the *4096 could overflow?

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
		i += (unsigned int) data;
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

	return 0;

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
				return -1;	/* Some other failure */

		}
	}

	return 0;
#endif //of #if 0 (old millisleep)

#else	// from initial "if linux && !posix" :

/*
 * I think this implementation works in all cases, with less overhead.
 */
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
	return 0;

#endif	//initial "if linux && !posix"
}	//diag_os_millisleep

/*
 * diag_os_ipending: Is input available on stdin. ret 1 if yes.
 *
 *
 */
int
diag_os_ipending(void) {
	fd_set set;
	int rv;
	struct timeval tv;

	FD_ZERO(&set);	// empty set of FDs;
	FD_SET(fileno(stdin), &set);	//adds an FD to the set
	tv.tv_sec = 0;
	tv.tv_usec = 0;		//select() with 0 timeout (return immediately)

	/*
	 * poll for input using select():
	 */
	errno = 0;
	//int select(nfds, readset, writeset, exceptset, timeout) ; return number of ready FDs found
	rv = select(fileno(stdin) + 1,  &set, NULL, NULL, &tv);
	//this will return immediately since timeout=0. NOTE : not the same thing as passing NULL instead of &tv :
	// in that case, it would NOT return until something is ready, in this case readset.

	return rv == 1 ;

}

//diag_os_sched : set high priority for this thread/process.
//this is called from most diag_l0_* devices; calling more than once
//will harm nothing. There is no "opposite" function of this, to
//reset normal priority.
//we ifdef the body of the function according to the OS capabilities
int
diag_os_sched(void)
{
	static int os_sched_done=0;
	int rv=0;

	if (os_sched_done)
		return 0;	//don't do it more than once

#ifdef _POSIX_PRIORITY_SCHEDULING
#include <sched.h>
	int r = 0;
	struct sched_param p;

#ifndef __linux__		//&& _POSIX_PRIO SCHED
	/*
	+* If we're not running on Linux, we're not sure if what is
	+* being done is remotely applicable for our flavor of POSIX
	+* priority scheduling.
	+* For example, you set the scheduling priority to 1.  Ouch.
	 */
#warning Scheduling setup should be examined on your particular platform !
#warning Please report this !

	/* Code block */
	{
		static int setup_warned;
		if (setup_warned == 0) {
			setup_warned = 1;
			fprintf(stderr,
				FLFMT "Scheduling setup should be examined.\n", FL);
		}
	}
#else		//so it's __linux__
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
				FLFMT "WARNING: Could not set real-time mode. "
				"Things will not work correctly\n", FL);
		}
	}
#endif	//ndef linux (inside _POSIX_PRIO_SCHED)

	/* Set real time UNIX scheduling */
	p.sched_priority = 1;
  	if ( sched_setscheduler(getpid(), SCHED_FIFO, &p) < 0)
	{
		fprintf(stderr, FLFMT "sched_setscheduler failed: %s.\n",
			FL, strerror(errno));
		r = -1;
	}
	rv=r;
#else	//not POSIX_PRIO_SCHED and not WIN32
#warning No special scheduling support in diag_os.c for your OS!
#warning Please report this !

	fprintf(stderr,
		FLFMT "diag_os_sched: No special scheduling support.\n", FL);
	rv=-1;
#endif
	os_sched_done=1;
	return rv;
}	//of diag_os_sched


#ifndef HAVE_GETTIMEOFDAY
	#error No implementation of gettimeofday() for your system!
#endif	//HAVE_GETTIMEOFDAY

#ifndef HAVE_TIMERSUB
	#error No implementation of timersub() for your system !
#endif //HAVE_TIMERSUB


//diag_os_geterr : get OS-specific error string.
//Either gets the last error if os_errno==0, or print the
//message associated with the specified os_errno
// XXX this is not async-safe / re-entrant !
//
const char * diag_os_geterr(OS_ERRTYPE os_errno) {
	//we'll suppose strerr is satisfactory.
	return (const char *) strerror(os_errno? os_errno : errno);
//	static char errbuf[30];

//	snprintf(errbuf, sizeof(errbuf), "OS Error %d", os_errno);
//	return (const char *) errbuf;

}

//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.

void diag_os_calibrate(void) {
	//TODO: implement linux/unix diag_os_calibrate !
	//For the moment we only check the resolution of diag_os_getms(),
	//and test diag_os_chronoms().
	static int calibrate_done=0;
	unsigned long t1, t2, t3;

	if (calibrate_done)
		return;

	//test getms()
	t1=diag_os_getms();
	while ( ((t2=diag_os_getms())-t1) ==0) {}
	printf("diag_os_getms() resolution: ~%lums\n", t2-t1);

	//now test chronoms()
	t3=diag_os_chronoms(0);	//get current relative time
	t1=diag_os_chronoms(t3);	//reset stopwatch & get current time (~0)
	while ( ((t2=diag_os_chronoms(0))-t1) ==0) {}
	(void) diag_os_chronoms(-t3); //and restore previous offset

	printf("diag_os_chronoms() : initial time %lums; resolution: ~%lums\n",
		t3, t2-t1);

	calibrate_done=1;
	return;
}	//diag_os_calibrate


// TODO: verify if this is portable. Linux and anything POSIX should definitely
// have clock_gettime().
unsigned long diag_os_getms(void) {
#ifdef _POSIX_MONOTONIC_CLOCK	//this should be yes on a lot of linux/unix systems
	struct timespec curtime;
	unsigned long rv;

	clock_gettime(CLOCK_MONOTONIC, &curtime);
	rv= (curtime.tv_nsec / 1000000)+(curtime.tv_sec * 1000);
	return rv;
#else
#warning ****** no POSIX monotonic clock on your system ! Report this!
	//but we'll use gettimeofday anyway as a stopgap. This is evil
	//because gettimeofday isn't guaranteed to be monotonic (always increasing)
	struct timeval tv;
	unsigned long rv;
	gettimeofday(&tv, NULL);
	rv=(tv.tv_usec/1000) + (tv.tv_sec * 1000);
	return rv;
#endif
}

//arbitrarily resettable stopwatch. See comments in diag_os.h
unsigned long diag_os_chronoms(unsigned long treset) {
#ifdef _POSIX_TIMERS	//this guarantees clock_gettime and CLOCK_REALTIME will work
	static struct timespec offset={0,0};
	struct timespec curtime;
	unsigned long rv;

	clock_gettime(CLOCK_REALTIME, &curtime);

	offset.tv_sec += treset / 1000;
	offset.tv_nsec += (treset % 1000) * 1000*1000;

	rv = (curtime.tv_nsec / 1000000)+(curtime.tv_sec * 1000);
	rv -= (offset.tv_nsec / 1000000)+(offset.tv_sec * 1000);
	return rv;
#else
#warning ****** no POSIX timers ! Please report this.
	//as we did for diag_os_getms(), we'll use gettimeofday.
	//But in this case it's not a big problem
	static struct timeval offset={0,0};
	struct timeval tv;
	unsigned long rv;

	gettimeofday(&tv, NULL);

	offset.tv_sec += treset / 1000;
	offset.tv_usec += (treset % 1000);

	rv = (tv.tv_usec/1000) + (tv.tv_sec * 1000);
	rv -= (offset.tv_usec/1000) + (offset.tv_sec * 1000);

	return rv;
#endif
}
