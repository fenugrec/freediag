/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 *
 * OS specific stuff

 * We run the process in high priority.
 *
 * WIN32 will use CreateTimerQueueTimer instead of the SIGALRM handler of unix.
 * Right now there's no self-checking but it should be of OK accuracy for basic stuff ( keepalive messages )
 * NOTE : that means at least WinXP is required.
 */


#include <stdlib.h>
#include <string.h>
#include <time.h>	//XXX do we need this here?


#include "diag_tty.h"
#include "diag.h"

#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"


#include <process.h>
#include <windows.h>
#include <inttypes.h> 	//for PRIu64 formatters


static int diag_os_init_done=0;

LARGE_INTEGER perfo_freq={{0,0}};	//for use with QueryPerformanceFrequency and QueryPerformanceCounter
float pf_conv=0;		//this will be (1E6 / perfo_freq) to convert counts to microseconds
int shortsleep_reliable=0;	//TODO : auto-detect this on startup. See diag_os_millisleep


/* periodic callback:
+* the current implementation uses non-async-signal-safe functions
+* in the signal handlers.  Their behavior is undefined if they happen
+* to occur during any other non-async-signal-safe function.
 */
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

//diag_os_init : a bit of a misnomer. This sets up a periodic callback
//to call diag_l3_timer and diag_l2_timer; that would sound like a job
//for "diag_os_sched". The WIN32 version of diag_os_init also
//calls diag_os_sched to increase thread priority.
//return 0 if ok
int
diag_os_init(void)
{
	unsigned long tmo=ALARM_TIMEOUT;

	if (diag_os_init_done)
		return 0;

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

}	//diag_os_init

//diag_os_close: delete alarm handlers / periodic timers
//return 0 if ok
int diag_os_close() {
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
} 	//diag_os_close


//different os_millisleep implementations
//TODO : add timing verification for *unix

int
diag_os_millisleep(unsigned int ms)
{
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
	SHORT rv=GetAsyncKeyState(0x0D);	//sketchy !
	//if LSB of rv ==1 :  key was pressed since the last call to GAKS.
	return rv & 1;

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

	os_sched_done=1;
	return rv;
}	//of diag_os_sched


#ifndef HAVE_GETTIMEOFDAY	//like on win32
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
#endif	//HAVE_GETTIMEOFDAY

#ifndef HAVE_TIMERSUB	//like on win32
void timersub(struct timeval *a, struct timeval *b, struct timeval *res) {
	//compute res=a-b
	LONGLONG atime=1000000 * (a->tv_sec) + a->tv_usec;	//combine high+low
	LONGLONG btime=1000000 * (b->tv_sec) + b->tv_usec;
	LONGLONG restime=atime-btime;
	res->tv_sec= restime/1000000;
	res->tv_usec= restime % 1000000;
	return;
}
#endif //HAVE_TIMERSUB


//diag_os_geterr : get OS-specific error string.
//Either gets the last error if os_errno==0, or print the
//message associated with the specified os_errno
// XXX this is not async-safe / re-entrant !
//
const char * diag_os_geterr(OS_ERRTYPE os_errno) {
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

}

//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.
//On win32, running diag_os_millisleep repeatedly allows it to
//auto-adjust to a certain degree.

void diag_os_calibrate(void) {
	//TODO : adjust dynamic offsets as well, now we just evaluate the situation
	static int calibrate_done=0;	//do it only once
	const int iters=8;
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

	//now test diag_os_getms
	unsigned long t1, t2, t3;
	t1=diag_os_getms();
	while ( ((t2=diag_os_getms())-t1) ==0) {}
	printf("diag_os_getms() resolution: ~%lums.\n", t2-t1);

	//and diag_os_chronoms
	t3=diag_os_chronoms(0);	//get time with current offset
	t1=diag_os_chronoms(t3);	//reset stopwatch & get start time (~ 0)
	while ( ((t2=diag_os_chronoms(0))-t1) ==0) {}
	//here, t2-t1 is the finest resolution available. But we want to restore
	//the previous offset in case some other function already called _chronoms()
	//before us :
	(void) diag_os_chronoms(-t3);	//this should do it

	printf("diag_os_chronoms() : initial time %lums; resolution: ~%lums\n",
		t3, t2-t1);


	printf("Calibration done.\n");
	calibrate_done=1;
	return;

}	//diag_os_calibrate

//return monotonic clock time, ms precision.
//resolution is not important; GetTickCount() is good enough
unsigned long diag_os_getms(void) {
	return (unsigned long) GetTickCount();
}


//millisecond stopwatch, arbitrarily resettable
unsigned long diag_os_chronoms(unsigned long treset) {
	static unsigned long offset=0;

	offset += treset;

	return (unsigned long) (GetTickCount() - offset);
}
