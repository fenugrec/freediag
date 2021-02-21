#ifndef _DIAG_OS_H_
#define _DIAG_OS_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * (c) 2014-2015 fenugrec
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
 * Public functions; wrappers for OS-specific functions
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdbool.h>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
	#include <windows.h>
	typedef DWORD OS_ERRTYPE;
#else
	typedef int OS_ERRTYPE;
#endif

#define ALARM_TIMEOUT 300	// ms interval timeout for timer callbacks (keepalive etc)

/* Common prototypes but note that the source
 * is different and defined in OS specific
 * c files.
 */
//init, close : ret 0 if ok
int diag_os_init(void);
int diag_os_close(void);

/** Millisecond sleep (blocking)
 *
 * @param ms requested delay
 * @note This makes or breaks freediag... Coding this function
 * is usually a nightmare on most OS'es. See doc/sourcetree_notes.txt
 */
void diag_os_millisleep(unsigned int ms);

/** Check if a key was pressed
 *
 * @return 0 if no key was pressed
 *
 * Currently, it is only used in a few places to break long loops, with "press any key to stop" semantics.
 * This returns immediately, to allow polling within a loop.
 * The linux/unix implementation needs an "Enter" keypress since stdin is buffered !
 */
int diag_os_ipending(void);

/** Measure & adjust OS timing performance.
 *
 * @note Should be called only once.
 */
void diag_os_calibrate(void);

/** Return OS-specific error message
 *
 * @return error string or empty string if not found.
 * @note Caller must not free() the string.
 * This should only be used from OS-specific code ! (diag_os*.c and diag_tty_*.c )
 */
const char *diag_os_geterr(OS_ERRTYPE os_errno);

/** Return current "time" in milliseconds.
 *
 * This must use a monotonic (i.e. always increasing) clock source; this
 * means a *lot* of gettimeofday & similar functions are inadequate.
 * This is important because this value is used to
 * calculate time differentials.
 * Time zero can be any reference unrelated to actual
 * wall-clock time (unix EPOCH, system boot time, etc.)
 * This does not need fine resolutions; 15-20ms is good enough.
 *
 * @return Monotonic time, in milliseconds, from an arbitrary 0 reference.
 */
unsigned long diag_os_getms(void);

/** Get highest-resolution monotonic timestamp available.
 *
 * For use as a short duration stopwatch.
 * Use diag_os_hrtus() to convert delta values to microseconds.
 * @return Monotonic timestamp, in arbitrary units. @see diag_os_hrtus
 */
unsigned long long diag_os_gethrt(void);

/** Convert an hrt timestamp delta to microseconds.
 *
 * @return microseconds
 * @see diag_os_gethrt
 */
unsigned long long diag_os_hrtus(unsigned long long hrdelta);

/* mutex wrapper stuff.
 * the backends use pthread, C11, winAPI etc.
 * lowest-common-denominator stuff here; regular mutexes (not necessarily recursive etc)
 */
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
	#include <windows.h>
	// No static mutex initialization on Windows ( CRITICAL_SECTION is an opaque type)
	typedef CRITICAL_SECTION diag_mtx;
#elif defined(__unix__)
	#include <pthread.h>
	typedef pthread_mutex_t diag_mtx;
#else
	#error Weird compilation environment, report this!
#endif

/** initialize mutex.
 * must be deleted with diag_os_delmtx() after use
 */
void diag_os_initmtx(diag_mtx *mtx);

/** initialize mutex statically initialized with LOCK_INITIALIZER.
 * must be deleted with diag_os_delmtx() after use
 */
void diag_os_initstaticmtx(diag_mtx *mtx);

/** delete unused mutex
 */
void diag_os_delmtx(diag_mtx *mtx);

/** lock mutex, wait if busy */
void diag_os_lock(diag_mtx *mtx);

/** try to lock mutex, return 0 immediately if failed */
bool diag_os_trylock(diag_mtx *mtx);

/** unlock mutex */
void diag_os_unlock(diag_mtx *mtx);


#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_OS_H_ */
