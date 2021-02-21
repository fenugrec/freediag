/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * (c) 2014-2015 fenugrec
 *
 * OS specific stuff

 * We run the process in high priority.
 *
 * WIN32 will use CreateTimerQueueTimer instead of the SIGALRM handler of unix.
 * Right now there's no self-checking but it should be of OK accuracy for basic stuff ( keepalive messages )
 * NOTE : that means at least WinXP is required.

 * additional timing info: http://www.windowstimestamp.com/description
 */


#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag_tty.h"
#include "diag_os.h"
#include "diag.h"

#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"


#include <process.h>
#include <windows.h>
#include <conio.h>	//for _kbhit, _getch
/** Check the _kbhit function **/
#ifdef HAVE_MS_KBHIT
#define _kbhit kbhit
#endif
/** Check the _getch function **/
#ifdef HAVE_MS_GETCH
#define _getch getch
#endif

#include <inttypes.h> //for PRIu64 formatters

static bool diag_os_init_done=0;
static bool timer_period_changed = 0;	//do we need to reset timeEndPeriod on exit

LARGE_INTEGER perfo_freq = {{0,0}};	//for use with QueryPerformanceFrequency and QueryPerformanceCounter
float pf_conv=0;		//this will be (1E6 / perfo_freq) to convert counts to microseconds, i.e. [us]=[counts]*pf_conv
static int pfconv_valid=0;	//flag after querying perfo_freq; nothing will not work without a performance counter
int shortsleep_reliable=0;	//TODO : auto-detect this on startup. See diag_os_millisleep & diag_os_calibrate
static UINT timer_period = 0; // to store timeBeginPeriod(timer_period) value for future cleanup


static void tweak_timing(bool change_interval);
static void reset_timing(void);

/* periodic callback:
+* the current implementation uses non-async-signal-safe functions
+* in the signal handlers.  Their behavior is undefined if they happen
+* to occur during any other non-async-signal-safe function.
 */
HANDLE hDiagTimer = INVALID_HANDLE_VALUE;

CRITICAL_SECTION periodic_lock;

VOID CALLBACK timercallback(UNUSED(PVOID lpParam), BOOLEAN timedout) {

	if (!TryEnterCriticalSection(&periodic_lock)) return;

	if (!timedout) {
		//this should never happen.
		fprintf(stderr, FLFMT "Problem with OS timer callback! Report this !\n", FL);
	} else {
		diag_l3_timer();	/* Call L3 Timer */
		diag_l2_timer();	/* Call L2 timer */
	}
	LeaveCriticalSection(&periodic_lock);

	return;
}

//diag_os_init : Sets up a periodic callback
//to call diag_l3_timer and diag_l2_timer.
// Also calls tweak_timing() to increase thread priority.
//return 0 if ok
int
diag_os_init(void) {
	unsigned long tmo=ALARM_TIMEOUT;

	if (diag_os_init_done)
		return 0;

	tweak_timing(1);

	//probably the nearest equivalent to a unix interval timer + associated alarm handler
	//is the timer queue... so that's what we do.
	//we create the timer in the default timerqueue
	InitializeCriticalSection(&periodic_lock);

	if (! CreateTimerQueueTimer(&hDiagTimer, NULL,
			(WAITORTIMERCALLBACK) timercallback, NULL, tmo, tmo,
			WT_EXECUTEDEFAULT)) {
		fprintf(stderr, FLFMT "CTQT error.\n", FL);
		hDiagTimer = INVALID_HANDLE_VALUE;
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//and get the current performance counter frequency.
	//From MSDN docs :	The frequency of the performance counter is fixed at system boot
	//					and is consistent across all processors. Therefore, the frequency
	//					need only be queried upon application initialization, and the result can be cached.
	//
	//					Under what circumstances does QueryPerformanceFrequency return FALSE,
	//					or QueryPerformanceCounter return zero?
	//						->This won't occur on any system that runs Windows XP or later.

	if ( !QueryPerformanceFrequency(&perfo_freq) || (perfo_freq.QuadPart==0)) {
		fprintf(stderr, FLFMT "Fatal: could not QPF. Please report this !\n", FL);
		diag_os_close();
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (perfo_freq.QuadPart ==0) {
		fprintf(stderr, FLFMT "Fatal: QPF reports 0Hz. Please report this !\n", FL);
		diag_os_close();
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	pf_conv=1.0E6 / perfo_freq.QuadPart;
	pfconv_valid =1;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_TIMER, DIAG_DBGLEVEL_V,
		FLFMT "Performance counter frequency : %9"PRIu64"Hz\n", FL, perfo_freq.QuadPart);

	diag_os_calibrate();
	diag_os_init_done = 1;
	return 0;

}	//diag_os_init

//diag_os_close: delete alarm handlers / periodic timers
//return 0 if ok
int diag_os_close() {
	DWORD err;

	diag_os_init_done=0;	//diag_os_init will have to be done again past this point.

	reset_timing();

	if (hDiagTimer != INVALID_HANDLE_VALUE) {
		if (DeleteTimerQueueTimer(NULL,hDiagTimer,NULL)) {
			//success
			goto goodexit;
		}
		//From MSDN : if error_io_pending, not necessary to call again.
		err=GetLastError();
		if (err==ERROR_IO_PENDING) {
			//This is OK and the queue will be deleted automagically.
			//No need to pester the user about this
			goto goodexit;
		}
		//Otherwise, try again
		fprintf(stderr, FLFMT "Could not DTQT. Retrying...", FL);
		Sleep(500);	//should be more than enough for IO to complete...
		if (DeleteTimerQueueTimer(NULL,hDiagTimer,NULL)) {
			fprintf(stderr, "OK !\n");
			goto goodexit;
		}
		fprintf(stderr, "Failed. Please report this.\n");
	}

goodexit:
	hDiagTimer = INVALID_HANDLE_VALUE;
	DeleteCriticalSection(&periodic_lock);
	return 0;
} 	//diag_os_close


//
void
diag_os_millisleep(unsigned int ms) {
	//This version self-corrects if Sleep() overshoots;
	//if it undershoots then we run an empty loop for the remaining
	//time. Eventually "correction" should contain the biggest
	//overshoot so far; this means every subsequent calls
	//will almost always run through the NOP loop.
	LARGE_INTEGER qpc1, qpc2;
	long real_t;
	static long correction=0;	//auto-adjusment (in us)
	LONGLONG tdiff;	//measured (elapsed) time (in counts)

	QueryPerformanceCounter(&qpc1);
	assert(pfconv_valid);
	tdiff=0;

	if (perfo_freq.QuadPart ==0) {
		Sleep(ms);
		return;
	}
	LONGLONG reqt= (ms * perfo_freq.QuadPart)/1000;	//required # of counts

	if ( shortsleep_reliable || (((long) ms-(correction/1000)) > 5)) {
		//if reliable, or long sleep : try
		Sleep(ms - (correction/1000));
		QueryPerformanceCounter(&qpc2);
		tdiff= qpc2.QuadPart - qpc1.QuadPart;
		if (tdiff > reqt) {
			//we busted the required time by:
			real_t = (long) (pf_conv * (tdiff-reqt));	//in us
			if (real_t > 1000) {
				correction += real_t;
				if (correction > 4000) correction = 4000;
			}
			return;
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
	return;

}	//diag_os_millisleep


int
diag_os_ipending(void) {
	if (_kbhit()) {
		(void) _getch();
		return 1;
	}
	return 0;
}

/** Adjust priority and OS time interval
 *
 * @param change_interval : if 1, use timeBeginPeriod
 *
 * call reset_timing() before exiting
 */
static void tweak_timing(bool change_interval) {
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

	if (!change_interval) return;

	// workaround to increase Sleep() routine accuracy by decreasing PerformanceTimer time period to minimum possible
	// not fatal if any of this fails

	TIMECAPS caps;
	MMRESULT res = timeGetDevCaps(&caps, sizeof(caps));

	if (res != TIMERR_NOERROR) {
		printf("Unable to timeGetDevCaps.\n");
	} else {
		if (timeBeginPeriod(caps.wPeriodMin) != TIMERR_NOERROR ) {
			printf ("Error setting OS timer period!\n");
		} else {
			timer_period = caps.wPeriodMin;
			timer_period_changed = 1;
		}
	}

	return;
}

/** reset prio and OS time interval
*/
static void reset_timing(void) {
	HANDLE curprocess, curthread;

	//reset the current process to normal
	curprocess=GetCurrentProcess();
	curthread=GetCurrentThread();
	if (! SetPriorityClass(curprocess, NORMAL_PRIORITY_CLASS)) {
		fprintf(stderr, FLFMT "Warning: could not reset process priority.\n", FL);
	}

	if (! SetThreadPriority(curthread, THREAD_PRIORITY_NORMAL)) {
		fprintf(stderr, FLFMT "Warning : could not reset thread priority.\n", FL);
	}

	if (!timer_period_changed) return;

	// restore multimedia timer
	if (timeEndPeriod(timer_period) != TIMERR_NOERROR) {
		printf("Error restoring OS timer period!\n");
	}

}



//diag_os_geterr : get OS-specific error string.
//Either gets the last error if os_errno==0, or print the
//message associated with the specified os_errno
// XXX this is not async-safe / re-entrant !
//
const char *diag_os_geterr(OS_ERRTYPE os_errno) {
	//to make this re-entrant, we would need CreateMutex OpenMutex etc.
	static char errbuf[160]="";

	LPVOID errstr;

	if (os_errno == 0)
		os_errno=GetLastError();

	if (os_errno !=0 ) {
		DWORD elen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
							NULL,
							os_errno,
							0,
							(LPTSTR) &errstr,
							0,
							NULL);
		if (elen) {
			snprintf(errbuf, sizeof(errbuf), "%s", (const char *) errstr);
			LocalFree(errstr);
		} else {
			//formatmessage failed...
			snprintf(errbuf, sizeof(errbuf), "UNK:%u", (unsigned int) os_errno);
		}
	} else {
		strcpy(errbuf, "No error");
	}
	return (const char *) errbuf;

}

//diag_os_calibrate : run some timing tests to make sure we have
//adequate performances.
//On win32, running diag_os_millisleep repeatedly allows it to
//auto-adjust to a certain degree.

void diag_os_calibrate(void) {
	static int calibrate_done=0;	//do it only once
	int testval;	//timeout to test
	LARGE_INTEGER qpc1, qpc2;
	LONGLONG tsum;
	#define RESOL_ITERS	10
	unsigned long long resol, maxres, tl1, tl2;	//all for _gethrt() test
	unsigned long t1, t2;	//for _getms() test

	assert(pfconv_valid);

	if (calibrate_done)
		return;

	//test _gethrt()
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
	printf("diag_os_gethrt() resolution <= %luus, avg ~%luus\n",
			(unsigned long) diag_os_hrtus(maxres), (unsigned long) diag_os_hrtus(resol / RESOL_ITERS));

	//now test diag_os_getms
	t1=diag_os_getms();
	while ( ((t2=diag_os_getms())-t1) ==0) {}
	printf("diag_os_getms() resolution: ~%lums.\n", t2-t1);

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
#define CAL_ITERS 6
		for (i=0; i< CAL_ITERS; i++) {
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
		avgerr= (LONGLONG) (((tsum/CAL_ITERS)-counts) * pf_conv);	//average error in us
		//a high spread (max-min) indicates initbus with dumb interfaces will be
		//fragile. We just print it out; there's not much we can do to fix this.
		if ((min < counts) || (avgerr > 900))
			printf("diag_os_millisleep(%d) off by %+"PRId64"%% (%+"PRId64"us)"
			"; spread=%"PRId64"%%\n", testval, (avgerr*100/1000)/testval, avgerr, ((max-min)*100)/counts);


		if (testval>=30)
			testval -= 8;
	}	//for testvals

	printf("Calibration done.\n");
	calibrate_done=1;
	return;

}	//diag_os_calibrate

//return monotonic clock time, ms precision.
//resolution and accuracy are not important; GetTickCount() is good enough
unsigned long diag_os_getms(void) {
	return (unsigned long) GetTickCount();
}

//get high resolution timestamp
unsigned long long diag_os_gethrt(void) {
	LARGE_INTEGER qpc1;
	assert(pfconv_valid);
	QueryPerformanceCounter(&qpc1);
	return (unsigned long long) qpc1.QuadPart;
}

//convert a delta of diag_os_gethrt() timestamps to microseconds
unsigned long long diag_os_hrtus(unsigned long long hrdelta) {
	assert(pfconv_valid);
	return (unsigned long long) (hrdelta * (double) pf_conv);
}


void
diag_os_initmtx(diag_mtx *mtx) {
	InitializeCriticalSection((CRITICAL_SECTION *)mtx);
	return;
}

void
diag_os_initstaticmtx(diag_mtx *mtx) {
	// No static initialization of mutexes on Windows.
	diag_os_initmtx(mtx);
	return;
}

void
diag_os_delmtx(diag_mtx *mtx) {
	DeleteCriticalSection((CRITICAL_SECTION *)mtx);
	return;
}

void
diag_os_lock(diag_mtx *mtx) {
	EnterCriticalSection((CRITICAL_SECTION *)mtx);
	return;
}

bool
diag_os_trylock(diag_mtx *mtx) {
	if (!TryEnterCriticalSection((CRITICAL_SECTION *)mtx))
		return 0;
	return 1;
}

void
diag_os_unlock(diag_mtx *mtx) {
	LeaveCriticalSection((CRITICAL_SECTION *)mtx);
	return;
}
