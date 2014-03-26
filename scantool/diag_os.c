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
 * WIN32 will use CreateTimerQueueTimer instead of the SIGALRM handler of unix.
 * Right now there's no self-checking but it should be of OK accuracy for basic stuff ( keepalive messages probably?)
 * NOTE : that means at least WinXP is required.
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

#ifdef WIN32
	#include <process.h>
	#include <windows.h>
	#include <inttypes.h> 	//for PRIu64 formatters
#else
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
#endif


CVSID("$Id$");

static int diag_os_init_done=0;

#ifdef WIN32
	LARGE_INTEGER perfo_freq={{0,0}};	//for use with QueryPerformanceFrequency and QueryPerformanceCounter
	float pf_conv=0;		//this will be (1E6 / perfo_freq) to convert counts to microseconds
	int shortsleep_reliable=0;	//TODO : auto-detect this on startup. See diag_os_millisleep
	#define ALARM_TIMEOUT 300	// ms interval timeout for timercallback()
#else
	#define ALARM_TIMEOUT 1		//1ms ? why so short ?
#endif

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
HANDLE hDiagTimer;

VOID CALLBACK timercallback(UNUSED(PVOID lpParam), BOOLEAN timedout) {
	static int timerproblem=0;
	if (!timedout) {
		//this should not happen...
		if (!timerproblem) {
			fprintf(stderr, FLFMT "Problem with OS timer callback! Report this !\n", FL);
			timerproblem=1;	//so we dont flood the screen with errors
		}
		//SetEvent(timerproblem) // probably not needed ?
	} else {
		timerproblem=0;
		diag_l3_timer();	/* Call L3 Timer */
		diag_l2_timer();	/* Call L2 timers, which will call L1 timer */
	}
	return;
}
#else	//not WIN32
void
diag_os_sigalrm(UNUSED(int unused))
{
	diag_l3_timer();	/* Call L3 Timer */
	diag_l2_timer();	/* Call L2 timers, which will call L1 timer */
}
#endif //WIN32

//diag_os_init : a bit of a misnomer. This sets up a periodic callback
//to call diag_l3_timer and diag_l2_timer; that would sound like a job
//for "diag_os_sched". The WIN32 version of diag_os_init also
//calls diag_os_sched to increase thread priority.
//return 0 if ok
int
diag_os_init(void)
{
#ifdef WIN32
//	struct sigaction_t stNew;
	unsigned long tmo=ALARM_TIMEOUT;	//20ms seems reasonable...
#else
	struct sigaction stNew;
	struct itimerval tv;
	long tmo = ALARM_TIMEOUT;	/* 1 ms,  why such a high frequency ? */
#endif

	if (diag_os_init_done)
		return 0;


#ifdef WIN32
	//probably the nearest equivalent to a unix interval timer + associated alarm handler
	//is the timer queue... so that's what we do.
	//we create the timer in the default timerqueue
	diag_os_sched();	//call os_sched to increase thread priority.
	//we do this in the hope that the OS increases the performance counter frequency
	//to its maximum, in case it was previously in a low-power, low-freqency state.
	//I have no evidence of that ever happening, however.

	if (! CreateTimerQueueTimer(&hDiagTimer, NULL,
			(WAITORTIMERCALLBACK) timercallback, NULL, tmo, tmo,
			WT_EXECUTEDEFAULT)) {
		fprintf(stderr, FLFMT "CTQT error.\n", FL);
		return -1;
	}

	//and get the current performance counter frequency.
	if (! QueryPerformanceFrequency(&perfo_freq)) {
		fprintf(stderr, FLFMT "Could not QPF. Please report this !\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (perfo_freq.QuadPart !=0) {
		pf_conv=1.0E6 / perfo_freq.QuadPart;
	}
	if (diag_l0_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "Performance counter frequency : %9"PRIu64"Hz\n", FL, perfo_freq.QuadPart);
	}
	diag_os_calibrate();
	diag_os_init_done = 1;
	return 0;
#else // not WIN32
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
#endif	//WIN32
}	//diag_os_init

//diag_os_close: delete alarm handlers / periodic timers
//return 0 if ok
int diag_os_close() {
#ifdef WIN32
	DWORD err;

	diag_os_init_done=0;	//diag_os_init will have to be done again past this point.
	if (DeleteTimerQueueTimer(NULL,hDiagTimer,NULL)) {
		//succes
		return 0;
	}
	//From MSDN : if error_io_pending, not necessary to call again.
	err=GetLastError();
	if (err==ERROR_IO_PENDING) {
		//This is OK and the queue will be deleted automagically.
		//No need to pester the user about this
		return 0;
	}
	//Otherwise, try again
	fprintf(stderr, FLFMT "Could not DTQT. Retrying...", FL);
	Sleep(500);	//should be more than enough for IO to complete...
	if (DeleteTimerQueueTimer(NULL,hDiagTimer,NULL)) {
		fprintf(stderr, "OK !\n");
		return 0;
	}
	fprintf(stderr, "Failed. Please report this.\n");
	return DIAG_ERR_GENERAL;
#else // not WIN32
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

#endif //WIN32
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
	return 0;
#else		//so it's WIN32
	//This version self-corrects if Sleep() overshoots;
	//if it undershoots then we run an empty loop for the remaining
	//time. Eventually "correction" should contain the biggest
	//overshoot so far; this means every subsequent calls
	//will almost always run through the NOP loop.
	LARGE_INTEGER qpc1, qpc2;
	long real_t;
	static long correction;	//auto-adjusment (in us)
	LONGLONG tdiff;	//measured (elapsed) time (in counts)

	QueryPerformanceCounter(&qpc1);
	tdiff=0;
	correction=0;
	LONGLONG reqt= (ms * perfo_freq.QuadPart)/1000;	//required # of counts
	if (perfo_freq.QuadPart ==0) {
		Sleep(ms);
		return 0;
	}


	if ( shortsleep_reliable || (((long) ms-(correction/1000)) > 5)) {
		//if reliable, or long sleep : try
		Sleep(ms - (correction/1000));
		QueryPerformanceCounter(&qpc2);
		tdiff= qpc2.QuadPart - qpc1.QuadPart;
		if (tdiff > reqt) {
			//we busted the required time by:
			real_t = (long) (pf_conv * (tdiff-reqt));	//in us
			if (real_t > 1000)
				correction += real_t;
			return 0;
		}
	}

	//do NOP loop for short sleeps for the remainder. This is ugly
	//but could help on some systems
	//if Sleep(ms) was too short this will bring us near
	//the requested value.
	while (tdiff < reqt) {
		QueryPerformanceCounter(&qpc2);
		tdiff= qpc2.QuadPart - qpc1.QuadPart;
	}
	return 0;

#endif	//ifndef win32

#endif	//initial "if linux && !posix"
}	//diag_os_millisleep

/*
 * diag_os_ipending: Is input available on stdin. ret 1 if yes.
 *
 * currently (like 2014), it is only used a few places to break long loops ?
 * the effect is that diag_os_ipending returns immediately, and it returns 1 only if Enter was pressed.
 * the WIN32 version of this is clumsier : it returns 1 if Enter was pressed since the last time diag_os_ipending() was called.
 *
 */
int
diag_os_ipending(void) {
#if WIN32
	SHORT rv=GetAsyncKeyState(0x0D);	//sketchy !
	//if LSB of rv ==1 :  key was pressed since the last call to GAKS.
	return rv & 1;
#else //so not WIN32
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

#endif	//WIN32
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
#elif defined WIN32		//not POSIX_PRIO_SCHED, but win32
	HANDLE curprocess, curthread;

	//set the current process to high priority.
	//the resultant "base priority" is a combination of process priority and thread priority.
	curprocess=GetCurrentProcess();
	curthread=GetCurrentThread();
	if (! SetPriorityClass(curprocess, HIGH_PRIORITY_CLASS)) {
		fprintf(stderr, FLFMT "Warning: could not increase process priority. Timing may be impaired.\n", FL);
	}

	if (! SetThreadPriority(curthread, THREAD_PRIORITY_HIGHEST)) {
		fprintf(stderr, FLFMT "Warning : could not increase thread priority. Timing may be impaired.\n", FL);
	}
	rv=0;

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


#ifndef HAVE_GETTIMEOFDAY	//like on win32
#ifdef WIN32
//#define DELTA_EPOCH_IN_MICROSECS  11644473600000000 // =  0x48864000, not compiler-friendly
#define DELTA_EPOCH_H 0x4886	//so we'll cheat
#define DELTA_EPOCH_L 0x4000
int gettimeofday(struct timeval *tv, UNUSED(struct timezone *tz)) {
	FILETIME ft;
	LARGE_INTEGER longtime;
	const LONGLONG delta_epoch=((LONGLONG) DELTA_EPOCH_H <<32) + DELTA_EPOCH_L;

	if (NULL != tv) {
		GetSystemTimeAsFileTime(&ft);	//getnb of 100ns intvals since 1601-01-01
		longtime.HighPart=ft.dwHighDateTime;
		longtime.LowPart=ft.dwLowDateTime;	//load 64bit val

		longtime.QuadPart /=10;	// convert to 1E-6s; use 64bit member of union

		longtime.QuadPart -= delta_epoch; 	//convert to unix timeframe
		//maybe useless for freediag's purpose, but it's there in any case.
		tv->tv_sec = (long)(longtime.QuadPart / 1000000);
		tv->tv_usec = (long)(longtime.QuadPart % 1000000);
	}
	return 0;
}
#else	//not WIN32 !
	//I don't have a non-win implementation of gettimeofday.
	#error No implementation of gettimeofday() for your system!
#endif 	//WIN32
#endif	//HAVE_GETTIMEOFDAY

#ifndef HAVE_TIMERSUB	//like on win32
#ifdef WIN32
void timersub(struct timeval *a, struct timeval *b, struct timeval *res) {
	//compute res=a-b
	LONGLONG atime=1000000 * (a->tv_sec) + a->tv_usec;	//combine high+low
	LONGLONG btime=1000000 * (b->tv_sec) + b->tv_usec;
	LONGLONG restime=atime-btime;
	res->tv_sec= restime/1000000;
	res->tv_usec= restime % 1000000;
	return;
}
#else //WIN32
	#error No implementation of timersub() for your system !
#endif	//WIN32
#endif //HAVE_TIMERSUB


//diag_os_geterr : get OS-specific error string.
//Either gets the last error if os_errno==0, or print the
//message associated with the specified os_errno
// XXX this is not async-safe / re-entrant !
//
const char * diag_os_geterr(OS_ERRTYPE os_errno) {
#ifdef WIN32
	//to make this re-entrant, we would need CreateMutex OpenMutex etc.
	static char errbuf[100]="";	//this has to be big enough, or else FormatMessage chokes!

	if (os_errno == 0)
		os_errno=GetLastError();

	if (os_errno !=0 ) {
		if (! FormatMessage(0, NULL, os_errno, 0, errbuf, sizeof(errbuf), NULL)) {
			snprintf(errbuf, sizeof(errbuf), "UNK:%u", (unsigned int) os_errno);
		}
	} else {
		strcpy(errbuf, "NIL");
	}
	return (const char *) errbuf;
#else 	//not WIN32 :
	//we'll suppose strerr is satisfactory.
	return (const char *) strerror(os_errno? os_errno : errno);
//	static char errbuf[30];

//	snprintf(errbuf, sizeof(errbuf), "OS Error %d", os_errno);
//	return (const char *) errbuf;
#endif // WIN32

}

//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.
//On win32, running diag_os_millisleep repeatedly allows it to
//auto-adjust to a certain degree.

void diag_os_calibrate(void) {
#ifdef WIN32
	//TODO : adjust dynamic offsets as well, now we just evaluate the situation
	static int calibrate_done=0;	//do it only once
	const int iters=10;
	int testval;	//timeout to test
	LARGE_INTEGER qpc1, qpc2;
	LONGLONG tsum;

	if (perfo_freq.QuadPart == 0) {
		fprintf(stderr, FLFMT "_calibrate will not work without a performance counter.\n", FL);
		return;
	}
	if (calibrate_done)
		return;

	printf("Calibrating timing, this will take a few seconds...\n");

	for (testval=50; testval > 0; testval -= 2) {
		//Start with the highest timeout to force _millisleep to use Sleep()
		//and therefore start auto-correcting right away.
		int i;
		LONGLONG counts, avgerr, max, min;

		max=0;

		tsum=0;
		counts=(testval*perfo_freq.QuadPart)/1000;	//expected # of counts
		min=counts;

		for (i=0; i< iters; i++) {
			LONGLONG timediff;
			QueryPerformanceCounter(&qpc1);
			diag_os_millisleep(testval);
			QueryPerformanceCounter(&qpc2);
			timediff=(qpc2.QuadPart-qpc1.QuadPart);
			tsum += timediff;
			//update extreme records if required:
			if (timediff < min)
				min = timediff;
			if (timediff > max)
				max = timediff;
		}
		avgerr= (LONGLONG) (((tsum/iters)-counts) * pf_conv);	//average error in us
		if ((min < counts) || (avgerr > 900))
			printf("diag_os_millisleep(%d) off by %+"PRId64"%% (%+"PRId64"us)"
			"; spread=%"PRIu64"%%\n", testval, (avgerr*100/1000)/testval, avgerr, ((max-min)*100)/counts);


		if (testval>=25)
			testval -= 7;
	}	//for testvals
	printf("Calibration done.\n");
	calibrate_done=1;
	return;
#else
	return;
#endif
}	//diag_os_calibrate
