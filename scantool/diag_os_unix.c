/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * 2014-2015 fenugrec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
 *
 *	(1) Using the diag_l0_dumb driver requires very accurate timing,
 *		which may require running the process in real time mode in certain
 *		cases.
 *	(2) Another process needs to be capable of establishing (1)
 *
 * Some notes on syscall interruption :
 * 	(3) The os specific and IO driver code allows "interruptible syscalls"
 *		BSD and Linux defaults is that signals don't interrupt syscalls
 *			(i.e restartable system calls)
 *		SYSV does, so you see lots of code that copes with EINTR
 *	(4)	EINTR handling code belongs inside diag_os* and diag_tty* functions only,
 *		to provide a clean OS-independant API to upper levels.
 *
 * Goals : if _POSIX_TIMERS is defined, we attempt to use:
 *		1- POSIX timer_create() mechanisms for the periodic callbacks
 *		2- POSIX clock_gettime(), using best available clockid, for _getms() and _gethrt()
 *		3- clock_nanosleep(), using best available clockid, for _millisleep()
 *
 * Fallbacks for above:
 *		1- SIGALRM signal handler
 *		2- gettimeofday(), yuck. TODO : OSX specific mach_absolute_time()
 *		3a- (linux): /dev/rtc trick
 *		3b- (other): nanosleep()
 *
 * more info on different OS high-resolution timers:
 *	http://nadeausoftware.com/articles/2012/04/c_c_tip_how_measure_elapsed_real_time_benchmarking
 */


#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag_os.h"
#include "diag_os_unix.h"
#include "diag.h"

#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"

#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

/***
 * In the following #ifdefs, enable/include everything supported.
 * Other #ifdefs in the code will 'choose' the correct implementations
 * when applicable. In other words, the following blocks should not
 * interfere with each other (ex. if both _POSIX_TIMERS && __linux__ )
 */
#ifdef _POSIX_TIMERS
	/* This should be defined on a lot of linux/unix systems.
		Implications : timer_create(), clock_gettime(), clock_nanosleep() are available.
	*/
	//Best clockids auto-selected by diag_os_discover() :
	static clockid_t clkid_pt = CLOCK_MONOTONIC;	//clockid for periodic timer,
	static clockid_t clkid_gt = CLOCK_MONOTONIC;	// for clock_gettime(),
	static clockid_t clkid_ns = CLOCK_MONOTONIC;	// for clock_nanosleep()

	static timer_t ptimer_id;	//periodic timer ID
#endif // _POSIX_TIMERS

#ifdef __linux__
	#include <sys/ioctl.h>	//need these for
	#include <linux/rtc.h>	//diag_os_millisleep fallback
#ifndef _POSIX_TIMERS
	#warning ****** WARNING ! Linux without _POSIX_TIMERS ?? Please report this !
#endif
#endif // __linux__

#ifdef HAVE_GETTIMEOFDAY
	#include <sys/time.h>	//only for gettimeofday(), timeval etc?
#endif
/***/


static int diag_os_init_done=0;
static int discover_done = 0;	//protect diag_os_millisleep() and _gethrt()

static void diag_os_discover(void);

static pthread_mutex_t periodic_lock = PTHREAD_MUTEX_INITIALIZER;

static void
#if defined(_POSIX_TIMERS) && (SEL_PERIODIC==S_POSIX || SEL_PERIODIC==S_AUTO)
diag_os_periodic(UNUSED(union sigval sv)) {
#else
diag_os_periodic(UNUSED(int unused)) {
#endif
	/* Warning: these indirectly use non-async-signal-safe functions
	 * Their behavior is undefined if they happen
	 * to occur during any other non-async-signal-safe function.
	 * See doc/sourcetree_notes.txt
	 */

	if (periodic_done() || pthread_mutex_trylock(&periodic_lock)) {
		return;
	}

	diag_l3_timer(); /* Call L3 Timers */
	diag_l2_timer(); /* Call L2 timers */
	pthread_mutex_unlock(&periodic_lock);
}

//diag_os_init sets up a periodic callback (diag_os_periodic())
//for keepalive messages, and selects + calibrates timer functions.
//return 0 if ok
int
diag_os_init(void) {
	const long tmo = ALARM_TIMEOUT;

	if (diag_os_init_done) {
		return 0;
	}

	diag_os_discover();	//auto-select clockids or other capabilities
	diag_os_calibrate();	//calibrate before starting periodic timer

#if defined(_POSIX_TIMERS) && (SEL_PERIODIC==S_POSIX || SEL_PERIODIC==S_AUTO)
	struct itimerspec pti;
	struct sigevent pt_sigev;

	pt_sigev.sigev_notify = SIGEV_THREAD;	//"Upon timer expiration, invoke sigev_notify_function "
	pt_sigev.sigev_notify_function = diag_os_periodic;
	pt_sigev.sigev_notify_attributes = NULL;	//not sure what we need here, if anything.
//	pt_sigev.sigev_value.sival_int = 0;	//not used
//	pt_sigev.siev_signo = 0;	//not used

	if (timer_create(clkid_pt, &pt_sigev, &ptimer_id) != 0) {
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

	if (getuid() == 0) {
		printf("\t******** WARNING ********\n"
				"\tRunning as superuser (uid 0) !!\n"
				"\tThis is dangerous, not required, and not recommended !\n");
	}

	diag_os_init_done = 1;
	return 0;
}	//diag_os_init

//diag_os_close: delete alarm handlers / periodic timers
//return 0 if ok (in this case, always)
int diag_os_close() {
#if defined(_POSIX_TIMERS) && (SEL_PERIODIC==S_POSIX || SEL_PERIODIC==S_AUTO)
	//disarm + delete periodic timer
	timer_delete(ptimer_id);
#else
	//stop the interval timer:
	struct itimerval tv = {{0,0},{0, 0}};
	setitimer(ITIMER_REAL, &tv, 0);

	//and  set the SIGALRM handler to default, whatever that is
	struct sigaction disable_tmr;
	memset(&disable_tmr, 0, sizeof(disable_tmr));
	disable_tmr.sa_handler=SIG_DFL;
	sigaction(SIGALRM, &disable_tmr, NULL);
#endif // _POSIX_TIMERS

	diag_os_init_done = 0;
	return 0;

} 	//diag_os_close


//return after (ms) milliseconds.
void
diag_os_millisleep(unsigned int ms) {
	unsigned long long t1,t2;	//for verification
	long int offsetus;

	t1=diag_os_gethrt();

	if (ms == 0 || !discover_done) {
		return;
	}

//3 different compile-time implementations
//TODO : select implem at runtime if possible? + internal feedback loop
#if defined(_POSIX_TIMERS) && (SEL_SLEEP==S_POSIX || SEL_SLEEP==S_AUTO)
	struct timespec rqst, resp;
	int rv;

	rqst.tv_sec = ms / 1000;
	rqst.tv_nsec = (ms % 1000) * 1000*1000;

	errno = 0;
	//clock_nanosleep is interruptible, hence this loop
	while ((rv=clock_nanosleep(clkid_ns, 0, &rqst, &resp)) != 0) {
		if (rv == EINTR) {
			rqst = resp;
			errno = 0;
		} else {
			//unlikely
			fprintf(stderr, "diag_os_millisleep : error %d\n",rv);
			break;
		}
	}
#elif defined(__linux__) && (SEL_SLEEP==S_LINUX || SEL_SLEEP==S_AUTO)
/** ugly /dev/rtc implementation; requires uid=0 or appropriate permissions. **/
	int fd, retval;
	unsigned int i;
	unsigned long tmp,data;

	/* adjust time for 2048 rate */

	ms = (unsigned int)((unsigned long) ms* 2048/1000);	//avoid overflow

	if (ms > 2)	//Bias delay -1ms to avoid overshoot ?
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
#else
#warning ****** WARNING ! Your system needs help. Using nanosleep() third-string backup plan.
#warning ****** Please report this!
	struct timespec rqst, resp;
	int rv;

	rqst.tv_sec = ms / 1000;
	rqst.tv_nsec = (ms % 1000) * 1000*1000;

	errno = 0;
	//clock_nanosleep is interruptible, hence this loop
	while ((rv=nanosleep(&rqst, &resp)) != 0) {
		if (rv == EINTR) {
			rqst = resp;
			errno = 0;
		} else {
			break;	//unlikely
		}
	}
#endif // SEL_SLEEP

	t2 = diag_os_gethrt();
	offsetus = ((long int) diag_os_hrtus(t2-t1)) - ms*1000;
	if ((offsetus > 1500) || (offsetus < -1500)) {
		printf("_millisleep off by %ld\n", offsetus);
	}

	return;

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

//diag_os_geterr : get OS-specific error string.
//Either gets the last error if os_errno==0, or print the
//message associated with the specified os_errno
// XXX this is not async-safe / re-entrant !
const char *diag_os_geterr(OS_ERRTYPE os_errno) {
	//we'll suppose strerr is satisfactory.
	return (const char *) strerror(os_errno? os_errno : errno);
//	static char errbuf[30];

//	snprintf(errbuf, sizeof(errbuf), "OS Error %d", os_errno);
//	return (const char *) errbuf;

}

#ifdef _POSIX_TIMERS
//internal use. ret 0 if ok
static int diag_os_testgt(clockid_t ckid, char *ckname) {
	struct timespec tmtest;
	if (clock_gettime(ckid, &tmtest) == 0) {
		printf("clock_gettime(): using %s\n", ckname);
		clkid_gt = ckid;
		return 0;
	}
	return -1;
}
//internal use. ret 0 if ok
static int diag_os_testns(clockid_t ckid, char *ckname) {
	struct timespec rqtp;
	rqtp.tv_sec = 0;
	rqtp.tv_nsec = 0;	//bogus interval for nanosleep test
	if (clock_nanosleep(ckid, 0, &rqtp, NULL) != ENOTSUP) {
		printf("clock_nanosleep(): using %s\n", ckname);
		clkid_ns = ckid;
		return 0;
	}
	return -1;
}
#endif // _POSIX_TIMERS

//use best clock truly available:
//TODO : add weird clockids for other systems
static void diag_os_discover(void) {
#ifdef _POSIX_TIMERS	//this guarantees clock_gettime and CLOCK_REALTIME are available
	int gtdone=0, nsdone=0;

// ***** 1) set clockid for periodic timers
#ifdef _POSIX_MONOTONIC_CLOCK
	//for some reason we can't use CLOCK_MONOTONIC_RAW, but
	//CLOCK_MONOTONIC will do just fine
	clkid_pt= CLOCK_MONOTONIC;
#else
	//CLOCK_REALTIME is mandatory (_MONOTONIC was optional)
	clkid_pt = CLOCK_REALTIME;
#endif // _POSIX_MONOTONIC_CLOCK

// ***** 2) test clockids for clock_gettime and clock_nanosleep
#define TESTCK(X)	if (!gtdone) if (diag_os_testgt(X, #X)==0) { gtdone=1;} \
					if (!nsdone) if (diag_os_testns(X, #X)==0) { nsdone=1;}

#ifdef CLOCK_MONOTONIC_RAW
	TESTCK(CLOCK_MONOTONIC_RAW)
#endif // CLOCK_MONOTONIC_RAW
#ifdef CLOCK_MONOTONIC
	TESTCK(CLOCK_MONOTONIC)
#endif // CLOCK_MONOTONIC
#ifdef CLOCK_BOOTTIME
	TESTCK(CLOCK_BOOTTIME)
	if (clkid_gt == CLOCK_BOOTTIME) {
		printf("CLOCK_BOOTTIME is unusual...\n");
	}
#endif // CLOCK_BOOTTIME
#ifdef CLOCK_REALTIME
	TESTCK(CLOCK_REALTIME)
	if (clkid_gt == CLOCK_REALTIME) {
		printf("CLOCK_REALTIME is suboptimal !\n");
	}
#endif
// ***** 3) report possible problems
	if (!gtdone) {
		clkid_gt = CLOCK_REALTIME;	//won't work anyway...
		printf("WARNING: no clockid for clock_gettime()!!\n");
	}
	if (!nsdone) {
		clkid_ns = CLOCK_REALTIME;
		printf("WARNING: no clockid for clock_nanosleep()!!\n");
	}
	if (!gtdone || !nsdone) {
		printf("WARNING: your system lied about its clocks;\nWARNING: you WILL have problems !\n");
	}
#else
	//nothing to do (yet) for non-posix
#endif // _POSIX_TIMERS
	discover_done = 1;
	return;
}


//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.
//call after diag_os_discover !
void diag_os_calibrate(void) {
	#define RESOL_ITERS	5
	static int calibrate_done=0;
	unsigned long t1, t2;
	unsigned long long tl1, tl2, resol, maxres;	//for _gethrt()

	if (calibrate_done) {
		return;
	}
	if (!discover_done) {
		diag_os_discover();
	}

	//test _gethrt(). clock_getres() would tell us the resolution, but measuring
	//like this gives a better measure of "usable" res.
	resol=0;
	maxres=0;
	for (int i=0; i < RESOL_ITERS; i++) {
		unsigned long long tr;
		tl1=diag_os_gethrt();
		while ((tl2=diag_os_gethrt()) == tl1) {}
		tr = (tl2-tl1);
		if (tr > maxres) {
			maxres = tr;
		}
		resol += tr;
	}
	printf("diag_os_gethrt() resolution <= %lluus, avg ~%lluus\n",
			diag_os_hrtus(maxres), diag_os_hrtus(resol / RESOL_ITERS));
	if (diag_os_hrtus(maxres) >= 1200) {
		printf("WARNING : your system offers no clock >= 1kHz; this "
		       "WILL be a problem!\n");
	}

	//test _getms()
	resol=0;
	maxres=0;
	for (int i=0; i < RESOL_ITERS; i++) {
		unsigned long tr;
		t1=diag_os_getms();
		while ((t2=diag_os_getms()) == t1) {}
		tr = (t2-t1);
		if (tr > maxres) {
			maxres = tr;
		}
		resol += tr;
	}
	printf("diag_os_getms() resolution <= ~%llums, avg ~%llums\n", maxres, resol / RESOL_ITERS);
	if (t2 > ((unsigned long)(-1) - 1000*30*60)) {
		//unlikely, since 32-bit milliseconds will wrap in 49.7 days
		printf("warning : diag_os_getms() will wrap in <30 minutes ! Consider rebooting...\n");
	}

	//test _millisleep() VS _gethrt()
	printf("testing diag_os_millisleep(), this will take a moment...\n");
	for (int testval=50; testval > 0; testval -= 2) {
		//Start with the highest timeout
		int i;
		const int iters = 5;
		long long avgerr, max, min, tsum;	//in us

		tsum=0;
		max=0;
		min=testval*1000;

		for (i=0; i< iters; i++) {
			long long timediff;
			tl1=diag_os_gethrt();
			diag_os_millisleep(testval);
			tl2=diag_os_gethrt();
			timediff= (long long) diag_os_hrtus(tl2 - tl1);
			tsum += timediff;
			//update extreme records if required:
			if (timediff < min) {
				min = timediff;
			}
			if (timediff > max) {
				max = timediff;
			}
		}
		avgerr= (tsum/iters) - (testval*1000);	//average error in us
		//a high spread (max-min) indicates initbus with dumb interfaces will be
		//fragile. We just print it out; there's not much we can do to fix this.
		if ((min < (testval*1000)) || (avgerr > 900)) {
			printf("diag_os_millisleep(%d) off by %lld%% (+%lldus)"
			"; spread=%lld%%\n", testval, (avgerr*100/1000)/testval, avgerr, ((max-min)*100)/(testval*1000));
		}

		if (testval >= 25) {
			testval -= 7;
		}
	}	//for testvals

	calibrate_done=1;
	return;
}	//diag_os_calibrate


unsigned long diag_os_getms(void) {
	//just use diag_os_gethrt() backend
	return diag_os_hrtus(diag_os_gethrt()) / 1000;
}

//return high res timestamp, monotonic.
unsigned long long diag_os_gethrt(void) {
	assert(discover_done);
#if defined(_POSIX_TIMERS) && (SEL_HRT==S_POSIX || SEL_HRT==S_AUTO)
	//units : ns
	struct timespec curtime = {0};

	clock_gettime(clkid_gt, &curtime);

	return curtime.tv_nsec + (curtime.tv_sec * 1000*1000*1000ULL);

#else
	#warning Using gettimeofday() ! This is evil !

#ifndef HAVE_GETTIMEOFDAY
	#error No implementation of gettimeofday() for your system!
#endif // HAVE_GETTIMEOFDAY

	//Use gettimeofday anyway as a stopgap. This is evil
	//because gettimeofday isn't guaranteed to be monotonic (always increasing)
	//units : us
	struct timeval tv;
	unsigned long long rv;
	gettimeofday(&tv, NULL);
	rv= tv.tv_usec + (tv.tv_sec * 1000000ULL);
	return rv;
#endif
}

//convert a delta of diag_os_gethrt() timestamps to microseconds
//must match diag_os_gethrt() implementation!
unsigned long long diag_os_hrtus(unsigned long long hrdelta) {
#if defined(_POSIX_TIMERS) && (SEL_HRT==S_POSIX || SEL_HRT==S_AUTO)
	return hrdelta / 1000;
#else
	return hrdelta;
#endif // _POSIX_TIMERS
}

void
diag_os_initmtx(diag_mtx *mtx) {
	pthread_mutex_init((pthread_mutex_t *)mtx, NULL);
	return;
}

void
diag_os_initstaticmtx(diag_mtx *mtx) {
	// This could have been statically initialized (in the Pthreads case, specifically)
	// see notes in diag_os.h
	pthread_mutex_init((pthread_mutex_t *)mtx, NULL);
	return;
}

void
diag_os_delmtx(diag_mtx *mtx) {
	pthread_mutex_destroy((pthread_mutex_t *)mtx);
	return;
}

void
diag_os_lock(diag_mtx *mtx) {
	if (pthread_mutex_lock((pthread_mutex_t *)mtx) == EDEADLK) {
		// do we even check for error values...
		fprintf(stderr, "DEADLOCK !\n");
	}
	return;
}

bool
diag_os_trylock(diag_mtx *mtx) {
	if (pthread_mutex_trylock((pthread_mutex_t *)mtx)) {
		return 0;
	}
	return 1;
}

void
diag_os_unlock(diag_mtx *mtx) {
	pthread_mutex_unlock((pthread_mutex_t *)mtx);
	return;
}
