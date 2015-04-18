/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * 2014-2015 fenugrec
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
 * OS abstraction & wrappers for unix, linux & OSX as much as possible.
 *
 * We run the process in
 *	(1) Real time mode, as we need to do some very accurate sleeps
 *		in diag_l0_dumb ;
 *	(2) As root in order to establish (1)
 *
 * Some notes on syscall interruption :
 * 	(3) The os specific and IO driver code allows "interruptible syscalls"
 *		BSD and Linux defaults is that signals don't interrupt syscalls
 *			(i.e restartable system calls)
 *		SYSV does, so you see lots of code that copes with EINTR
 *	(4)	EINTR handling code belongs inside diag_os* and diag_tty* functions only,
 *		to provide a clean OS-independant API to upper levels. (TODO)
 *
  * Goals : if _POSIX_TIMERS is defined, we attempt to use:
 *		1- POSIX timer_create() mechanisms for the periodic callbacks
 *		2- POSIX clock_gettime(), using best available clock, for _getms and _gethrt
 *		2b- using clock_gettime() removes requirement for gettimeofday() and associated compile-time checks
 *		3- clock_nanosleep() for diag_os_millisleep()
 *
 * Fallbacks for above:
 *		1- SIGALRM signal handler (_POSIX_TIMERS means timer_create() is available)
 *		2- gettimeofday(), yuck. TODO : OSX specific mach_absolute_time()
 *		3- nanosleep() loop + checking (nanosleep is interruptible !)
 *
 * more info on different OS high-resolution timers:
 *	http://nadeausoftware.com/articles/2012/04/c_c_tip_how_measure_elapsed_real_time_benchmarking
 */


#include <stdlib.h>
#include <string.h>

#include "diag_os.h"
#include "diag.h"

#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <linux/rtc.h>	//XXX todo : #ifdef
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>


#ifdef _POSIX_TIMERS
	/* This should be defined on a lot of linux/unix systems.
		Implications : timer_create(), clock_gettime(), clock_nanosleep() are available.
	*/
	//monoton_src : auto-selected best clockid in diag_os_discover()
	static clockid_t monoton_src = CLOCK_MONOTONIC;	//probably-safe default
	static timer_t ptimer_id;	//periodic timer ID

	static void diag_os_discover(void);
#else
#warning ****** no POSIX timers on your system !?!? Report this!
#endif

static int diag_os_init_done=0;



/* Also, the current implementation uses non-async-signal-safe functions
+* in the signal handlers.  Their behavior is undefined if they happen
+* to occur during any other non-async-signal-safe function.
 */


static void
#ifdef _POSIX_TIMERS
diag_os_periodic(UNUSED(union sigval sv))
#else
diag_os_periodic(UNUSED(int unused))
#endif
{
	printf("os_periodic\n");
	diag_l3_timer();	/* Call L3 Timer */
	diag_l2_timer();	/* Call L2 timers, which will call L1 timer */
}

//_os_init sets up a periodic callback (diag_os_periodic())
//for diag_l3_timer and diag_l2_timer (keepalive messages).
//return 0 if ok
int
diag_os_init(void)
{
	const long tmo = ALARM_TIMEOUT;

	if (diag_os_init_done)
		return 0;

	diag_os_calibrate();	//before starting periodic callbacks
	
#ifdef _POSIX_TIMERS
	struct itimerspec pti;
	struct sigevent pt_sigev;
#ifdef _POSIX_MONOTONIC_CLOCK
	//for some reason we can't use CLOCK_MONOTONIC_RAW for periodic timers, but
	//CLOCK_MONOTONIC will do just fine
	const clockid_t pt_cid = CLOCK_MONOTONIC;
#else
	//CLOCK_REALTIME is mandatory, _MONOTONIC was optional
	const clockid_t pt_cid = CLOCK_REALTIME;
#endif // _POSIX_MONOTONIC_CLOCK

	diag_os_discover();	//auto-select monoton_src
	
	pt_sigev.sigev_notify = SIGEV_THREAD;	//"Upon timer expiration, invoke sigev_notify_function "
	pt_sigev.sigev_notify_function = diag_os_periodic;
	pt_sigev.sigev_notify_attributes = NULL;	//not sure what we need here, if anything.
//	pt_sigev.sigev_value.sival_int = 0;	//not used
//	pt_sigev.siev_signo = 0;	//not used
	
	if (timer_create(pt_cid, &pt_sigev, &ptimer_id) != 0) {
		fprintf(stderr, FLFMT "Could not create periodic timer... report this\n", FL);
		diag_os_geterr(0);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//timer was created in disarmed state
	pti.it_interval.tv_sec = (tmo / 1000);
	pti.it_interval.tv_nsec = (tmo % 1000) * 1000*1000;
	pti.it_value.tv_sec = pti.it_interval.tv_sec;
	pti.it_value.tv_nsec = pti.it_interval.tv_nsec;

	if (timer_settime(ptimer_id, 0, &pti, NULL) != 0) {
		fprintf(stderr, FLFMT "Could not set periodic timer... report this\n", FL);
		diag_os_geterr(0);
		timer_delete(ptimer_id);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
#else	//so, no _POSIX_TIMERS ... sucks
	struct sigaction stNew;
	struct itimerval tv;
	
	/*
	 * Install alarm handler
	 */
	memset(&stNew, 0, sizeof(stNew));
	stNew.sa_handler = diag_os_periodic;
	stNew.sa_flags = 0;
	//stNew.sa_flags = SA_RESTART;

/* Notes on SA_RESTART: (man 7 signal)
	The following interfaces are never restarted after being interrupted by
	a signal handler, regardless of the use of SA_RESTART; they always fail
	with the error EINTR when interrupted by a signal handler:
  select, clock_nanosleep, [some others].

*** From POSIX docs:
 SA_RESTART
    This flag affects the behavior of interruptible functions; that is,
    those specified to fail with errno set to [EINTR]. If set, and a
    function specified as interruptible is interrupted by this signal,
    the function shall restart and shall not fail with [EINTR] unless
    otherwise specified. If an interruptible function which uses a
    timeout is restarted, the duration of the timeout following the
    restart is set to an unspecified value that does not exceed the
    original timeout value. If the flag is not set, interruptible
    functions interrupted by this signal shall fail with errno set to
    [EINTR].
   
*** Interesting synthesis on
  http://unix.stackexchange.com/questions/16455/interruption-of-system-calls-when-a-signal-is-caught
 
*** Conclusion : since we need to handle EINTR for read(), write(), select()
	and some *sleep() syscalls anyway, we don't specify SA_RESTART.

*/

	sigaction(SIGALRM, &stNew, NULL);	//install handler for SIGALRM
	/*
	 * Start repeating timer
	 */
	tv.it_interval.tv_sec = tmo / 1000;	/* Seconds */
	tv.it_interval.tv_usec = (tmo % 1000) * 1000; 	/* ms */

	tv.it_value = tv.it_interval;

	setitimer(ITIMER_REAL, &tv, 0); /* Set timer : it will SIGALRM upon expiration */
#endif // _POSIX_TIMERS

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
+* suspended from execution until the time interval specified by the
+* rqtp argument has elapsed, a signal is delivered to the calling
+* thread and the action of the signal is to invoke a signal-catching
+* function, or to terminate the process [...] But,  except  for  the
+* case of being interrupted by a signal, the suspension time shall
+* not be less than the time specified by  rqtp,  as measured by the
+* system clock CLOCK_REALTIME. "
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
#else	//not POSIX_PRIO_SCHED
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
	//TODO : don't need gettimeofday if _POSIX_TIMERS ?
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

#ifdef _POSIX_TIMERS
static void diag_os_discover(void) {
	struct timespec tmtest;
	//use best clock truly available:
#ifdef CLOCK_MONOTONIC_RAW
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tmtest) == 0) {
		printf("using CLOCK_MONOTONIC_RAW.\n");
		monoton_src = CLOCK_MONOTONIC_RAW;
		return;
	}
#endif // CLOCK_MONOTONIC_RAW
#ifdef CLOCK_MONOTONIC
	if (clock_gettime(CLOCK_MONOTONIC, &tmtest) == 0) {
		printf("using CLOCK_MONOTONIC.\n");
		monoton_src = CLOCK_MONOTONIC;
		return;
	}
#endif // CLOCK_MONOTONIC
#ifdef CLOCK_BOOTTIME
	if (clock_gettime(CLOCK_BOOTTIME, &tmtest) == 0) {
		printf("using unusual CLOCK_BOOTTIME.\n");
		monoton_src = CLOCK_BOOTTIME;
		return;
	}
#endif // CLOCK_BOOTTIME
#ifdef CLOCK_REALTIME
	if (clock_gettime(CLOCK_REALTIME, &tmtest) == 0) {
		printf("using sub-optimal CLOCK_REALTIME.\n");
		monoton_src = CLOCK_REALTIME;
		return;
	}
#endif
	printf("WARNING: your system lied about CLOCK_MONOTONIC !!!\nWARNING: you WILL have problems !\n");
	monoton_src = CLOCK_MONOTONIC;	//won't work anyway...
	return;
}
#endif // _POSIX_TIMERS

//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.

void diag_os_calibrate(void) {
	//TODO: implement linux/unix diag_os_calibrate !
	//For the moment we only check the resolution of diag_os_getms(),
	//diag_os_gethrt() and test diag_os_chronoms().
	#define RESOL_ITERS	5
	static int calibrate_done=0;
	unsigned long t1, t2, t3;
	unsigned long long tl1, tl2, resol, maxres;	//for _gethrt()

	if (calibrate_done)
		return;

	//test _gethrt(). This measures usable resolution (clock_getres() gives raw clock res)
	resol=0;
	maxres=0;
	for (int i=0; i < RESOL_ITERS; i++) {
		unsigned long long tr;
		tl1=diag_os_gethrt();
		while ((tl2=diag_os_gethrt()) == tl1) {}
		tr = (tl2-tl1);
		if (tr > maxres) maxres = tr;
		resol += tr;
	}
	printf("diag_os_gethrt() resolution <= %lluus, avg ~%lluus\n",
			diag_os_hrtus(maxres), diag_os_hrtus(resol / RESOL_ITERS));

	//test _getms()
	resol=0;
	maxres=0;
	for (int i=0; i < RESOL_ITERS; i++) {
		unsigned long tr;
		t1=diag_os_getms();
		while ((t2=diag_os_getms()) == t1) {}
		tr = (t2-t1);
		if (tr > maxres) maxres = tr;
		resol += tr;
	}
	printf("diag_os_getms() resolution <= ~%llums, avg ~%llums\n", maxres, resol / RESOL_ITERS);
	if (t2 > ((unsigned long)(-1) - 1000*30*60)) {
		//unlikely, since 32-bit milliseconds will wrap in 49.7 days
		printf("warning : diag_os_getms() will wrap in <30 minutes ! Consider rebooting...\n");
	}

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


unsigned long diag_os_getms(void) {
	//just use diag_os_gethrt() backend
	return diag_os_hrtus(diag_os_gethrt()) / 1000;
}

//return high res timestamp, monotonic.
// TODO: increase portability... Linux and anything POSIX should definitely
// have clock_gettime(), but what about _POSIX_MONOTONIC_CLOCK ?
unsigned long long diag_os_gethrt(void) {
#ifdef _POSIX_TIMERS
	//units : ns
	struct timespec curtime={0};

	clock_gettime(monoton_src, &curtime);

	return curtime.tv_nsec + (curtime.tv_sec * 1000*1000*1000);

#else
	//but we'll use gettimeofday anyway as a stopgap. This is evil
	//because gettimeofday isn't guaranteed to be monotonic (always increasing)
	//TODO : OSX has a mach_absolute_time() which could be used.
	//units : us
	struct timeval tv;
	unsigned long long rv;
	gettimeofday(&tv, NULL);
	rv= tv.tv_usec + (tv.tv_sec * 1000000);
	return rv;
#endif
}

//convert a delta of diag_os_gethrt() timestamps to microseconds
unsigned long long diag_os_hrtus(unsigned long long hrdelta) {
#ifdef _POSIX_TIMERS	//must match diag_os_gethrt() implementation!
	return hrdelta / 1000;
#else
	return hrdelta;
#endif // _POSIX_MONOTONIC_CLOCK
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
#endif // _POSIX_TIMERS
}
