#ifndef _DIAG_H_
#define _DIAG_H_

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
 * Library user header file
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "cconf.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x0500	//use > winXP features...
	#endif
	#include <time.h>
	#include <basetsd.h>
	#include <windows.h>
#else
	#include <sys/types.h>
#endif

#include <stdbool.h>
#include <stdint.h>		/* For uint8_t, etc. This is a C99 header */

#if defined(_MSC_VER)
	//silence warnings about non-portable "_s" function replacements.
	//this must be set before including <stdio.h>.
	#define _CRT_SECURE_NO_WARNINGS
	#pragma warning(disable:4996)
#endif /* _MSC_VER Visual Studio */
#include <stdio.h>		/* For FILE */

#include "diag_os.h"	//for mutexes...

// Nice to have anywhere...
#define MIN(_a_, _b_) (((_a_) < (_b_) ? (_a_) : (_b_)))
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#define FLFMT "%s:%d:  "		//for debug messages


#ifdef HAVE_STRCASECMP
	#include <strings.h>
#else
	#ifdef _WIN32
        //strcasecmp is POSIX, but kernel32 provides lstrcmpi which should be equivalent.
		#define strcasecmp(a,b) lstrcmpi((LPCTSTR) a, (LPCTSTR) b)
	#else
		#error Your system provides no strcasecmp ! This is a problem !
	#endif 	//WIN32
#endif	//HAVE_STRCASECMP


#define DB_FILE "./freediag_carsim_all.db"	//default simfile for CARSIM interface
#define DIAG_NAMELEN	256


/****** compiler-specific tweaks ******/
#ifdef __GNUC__
	#define UNUSED(X) 	X __attribute__((unused))	//magic !
#else
	#define UNUSED(X)	X	//how can we suppress "unused parameter" warnings on other compilers?
#endif // __GNUC__

//hacks for MS Visual studio / visual C
#if defined(_MSC_VER)
	typedef SSIZE_T ssize_t;	//XXX ssize_t is currently only needed because of diag_tty_unix.c:diag_tty_{read,write}.
								//TODO : rework read/write types to use a combination of size_t and int ?
	#if _MSC_VER < 1910 /* anything older than Visual Studio 2017 */
		#define snprintf _snprintf	//danger : _snprintf doesn't guarantee zero-termination !?
									//as of Visual Studio 2015 the snprintf function is c99 compatible.
									//https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/snprintf-snprintf-snprintf-l-snwprintf-snwprintf-l?view=msvc-160
		#pragma message("Warning: MSVC _sprintf() may be dangerous ! Please ask your compiler vendor to supply a C99-compliant snprintf()...")
		//CURFILE will be defined by CMake on a per-file basis !
		#pragma message("Warning: MSVC may not work with the CURFILE macro. See diag.h")
		// apparently some (all ?) MSVC compilers don't support per-file defines, so CURFILE would be the
		// same for all source files.
		// The disadvantage of __FILE__ is that it often (always ?) holds the absolute path of the file,
		// not just the filename. For our debugging messages we only care about the filename, hence CURFILE.
		#define FL  __FILE__, __LINE__
	#else
		// Using Visual Studio 2017 and higher the CURFILE macro will work.
		#define FL CURFILE, __LINE__
	#endif /* _MSC_VER < 1910  Visual Studio 2017 */
#else
	#define FL CURFILE, __LINE__
#endif /* _MSC_VER Visual Studio */
/****** ******/

/*
 * Many receive buffers are set to this, which is voluntarily larger than
 * any possible valid message to/from an ECU.
 */
#define MAXRBUF 1024

#define DIAG_MAX_MSGLEN 4200	/** limit diag_allocmsg() message size. */

typedef uint8_t target_type, source_type, databyte_type, command_type;
typedef uint16_t flag_type;	//this is used for L2 type flags (see diag_l2.h)
			//only used for diag_l2_startcomms() arg

/*
 * IOCTLs
 *
 * The IOCTL can be done to any layer, and it is passed downward with
 * each layer filling in info as needed (there are currently no clashes)
 *
 * Values for "cmd" parameter to diag_l[12]_ioctl()
 * XXX Are their numeric values chosen on purpose ? i.e. why "2023" etc.
 */

#define DIAG_IOCTL_GET_L1_TYPE	0x2010	/* Get L1 Type, data is ptr to int */
#define DIAG_IOCTL_GET_L1_FLAGS	0x2011	/* Get L1 Flags, data is ptr to int */
#define DIAG_IOCTL_GET_L2_FLAGS	0x2021	/* Get the L2 flags (see fmt stuff )*/
#define DIAG_IOCTL_GET_L2_DATA	0x2023	/* Get the L2 Keybytes etc into
										 * diag_l2_data passed to us
										 */
#define DIAG_IOCTL_SETSPEED	0x2101	/* Set speed, bits etc. data = (const struct diag_serial_settings *); ret 0 if ok
									 * Ignored if DIAG_L1_AUTOSPEED or DIAG_L1_NOTTY is set */
#define DIAG_IOCTL_INITBUS	0x2201	/* Initialise the ecu bus, data = (struct diag_l1_initbus_args *)
									 *
									 * Caller must have waited the appropriate time before calling this, since any
									 * bus-idle requirements are specified at the L2 level.
									 * Must return as soon as possible,
									 * and restore original port settings (speed, etc).
									 * See diag_l1.h for initbus args
									 * return 0 if ok
									 */
#define DIAG_IOCTL_IFLUSH 0x2202	/* flush input buffers. No data. Ignored if DIAG_L1_NOTTY is set.
									 * Ret 0 if ok / not applicable (non fatal error) */
/** Set wake-up (keepalive) message.
 * Only applicable if DIAG_L1_DOESKEEPALIVE is set
 *
 * data = (struct diag_msg *).
 * The (struct diag_msg)->data member must be a raw message (including headers)
 *
 * data can be freed by caller after the ioctl (L0 will make a copy of the message data as required)
 */
#define DIAG_IOCTL_SETWM 0x2203

/****** debug control ******/
// flag containers : diag_l0_debug, diag_l1_debug diag_l2_debug, diag_l3_debug, diag_cli_debug

#define DIAG_DEBUG_OPEN		0x01	/* Open events */
#define DIAG_DEBUG_CLOSE	0x02	/* Close events */
#define DIAG_DEBUG_READ		0x04	/* Read events */
#define DIAG_DEBUG_WRITE	0x08	/* Write events */
#define DIAG_DEBUG_IOCTL	0x10	/* Ioctl stuff (setspeed etc) */
#define DIAG_DEBUG_PROTO	0x20	/* Other protocol stuff */
#define DIAG_DEBUG_INIT		0x40	/* Initialisation stuff */
#define DIAG_DEBUG_DATA		0x80	/* Dump data depending on other flags */
#define DIAG_DEBUG_TIMER	0x100	/* Timer stuff */


// these are for identifying the debug message prefix to be printed
enum debug_prefix {
	DIAG_DEBUGPF_NONE,
	DIAG_DEBUGPF_OPEN,
	DIAG_DEBUGPF_CLOSE,
	DIAG_DEBUGPF_READ,
	DIAG_DEBUGPF_WRITE,
	DIAG_DEBUGPF_IOCTL,
	DIAG_DEBUGPF_PROTO,
	DIAG_DEBUGPF_INIT,
	DIAG_DEBUGPF_DATA,
	DIAG_DEBUGPF_TIMER
};

/** debug message prefixes : dbg_prefixes[DIAG_DEBUGPF_XYZ] is a const char*
 */
extern const char *dbg_prefixes[];


/**** debug message helpers.
 *
 * These macros will allow changing the backend and destination (stderr, file, etc)
 *
 */

#define DIAG_DBGLEVEL_V	0

/** for diag.h internal use only */
#define DIAG_DBG_BACKEND(...) fprintf(stderr, __VA_ARGS__)


/** print general debug message
 *
 */
#define DIAG_DBGGEN(level, ...) DIAG_DBG_BACKEND(__VA_ARGS__);


/** simple debug message formatter
 *
 * flagvar is the i.e. "diag_l1_debug" that is checked against mask.
 * mask: see DIAG_DEBUG_* defs above
 * level: not used yet
 *
 * Must be used for all non-essential messages. Does not add "\n"
 *
 */
#define DIAG_DBGM(flagvar, mask, level, ...) do { \
	if (((flagvar) & (mask)) == (mask)) { \
		DIAG_DBG_BACKEND(__VA_ARGS__); \
	}} while (0)

/** debug message formatter with data
 *
 * flagvar is the i.e. "diag_l1_debug" that is checked against mask.
 * mask: see DIAG_DEBUG_* defs above. No need to specify DIAG_DEBUG_DATA
 * level: not used yet
 *
 * varargs: always printed. Automatically adds trailing "\n"
 *
 * Must be used for all non-essential messages.
 *
 */
#define DIAG_DBGMDATA(flagvar, mask, level, data, datalen, ...) do { \
	if (((flagvar) & (mask)) == (mask)) { \
		DIAG_DBG_BACKEND(__VA_ARGS__); \
		if ((flagvar) & DIAG_DEBUG_DATA) { \
			diag_data_dump(stderr, data, datalen); \
		} \
		fprintf(stderr, "\n"); \
	}} while (0)


/*
 * Message handling.
 *
 * These are messages passed to or from the layer 2 and layer 3 code
 * which cause data to be transmitted to layer 1
 *
 * The receiver of the message *must* copy the data if it wants it !!!
 * The flags are arranged such as zeroing out the whole structure sets "safe" defaults.
 * There's probably no application for 0-length messages, but currently the various functions
 * don't complain when allocating/duplicating empty data.
 */
struct diag_msg {
	uint8_t	fmt;			/* Message format (doesn't absolutely need to be uint8_t) : */
	#define DIAG_FMT_ISO_FUNCADDR	0x01	/* ISO Functional addressing (default : phys) */
	#define DIAG_FMT_FRAMED		0x02	/* Rcvd data is framed, ie not raw. XXX DEPRECATED ! L0..L2 all do this.*/
//	#define	DIAG_FMT_DATAONLY	0x04	/* Rcvd data had L2/L3 headers removed XXX ALWAYS ! */
	#define DIAG_FMT_CKSUMMED	0x08	/* Someone (L1/L2) checked the checksum */
	#define DIAG_FMT_BADCS	0x10		// message has bad checksum

	uint8_t	type;		/* Type from received frame */
	uint8_t	dest;		/* Destination from received frame */
	uint8_t	src;		/* Source from received frame */

	unsigned	len;		/* calculated data length */
	uint8_t	*data;		/* The data; can be dynamically alloc'ed */

	unsigned long	 rxtime;	/* Processing timestamp, in ms given by diag_os_getms() */
	struct diag_msg	*next;		/* For linked lists of messages */

	uint8_t	*idata;		/* For free() of data later: this is a "backup"
							 * of the initial *data pointer.*/
	uint8_t	iflags;		/* Internal flags */
	#define	DIAG_MSG_IFLAG_MALLOC	1	/* We malloced; we Free -- this is set when the msg
										 * was created by diag_allocmsg()*/
};

/** Allocate a new diag_msg
 *
 * Also allocates diag_msg-\>data if datalen\>0.
 * @param datalen: if \>0, size of data buffer to allocate. Max DIAG_MAX_MSGLEN.
 * @return new struct diag_msg, must be freed with diag_freemsg().
 */
struct diag_msg	*diag_allocmsg(size_t datalen);
/**	Duplicate a diag_msg, including all chained messages and their contents.
 * @return new struct diag_msg, must be freed with diag_freemsg().
 */
struct diag_msg	*diag_dupmsg(struct diag_msg *);

/** Duplicate a single diag_msg, without following the linked-list chain.
 * @return new struct diag_msg, must be freed with diag_freemsg().
 */
struct diag_msg	*diag_dupsinglemsg(struct diag_msg *);

/** Free a diag_msg
 * Safe to call with NULL arg
 */
void diag_freemsg(struct diag_msg *);

/** Calculate 8bit checksum
 * @param len: number of bytes in *data
 * @return 8-bit sum of all bytes
 */
uint8_t diag_cks1(const uint8_t *data, unsigned int len);


void diag_printmsg_header(FILE *fp, struct diag_msg *msg, bool timestamp, int msgnum);
void diag_printmsg(FILE *fp, struct diag_msg *msg, bool timestamp);

/*
 * General functions
 */
//diag_init : ret 0 if ok;
int diag_init(void);
//diag_end : must be called before exiting. Ret 0 if ok
int diag_end(void);

/** Print bytes in hex format
 * @param out: output stream (file / stderr / etc.)
 * @param len: # of bytes to print
 *
 * Prints bytes with "0x%02X" formatter, e.g. "0x55 0xAA 0x03 "
 */
void diag_data_dump(FILE *out, const void *data, size_t len);

//smartcat : only verifies if s1 is not too large !
void smartcat(char *p1, const size_t s1, const char *p2 );

/*
 * Error functions.
 * diag_p_* aren't intended to be called directly.
 * Use "diag_pseterr" (returns a NULL pointer) or
 * "diag_iseterr" (returns the passed in error code),
 * which will save where the error took place and optionally log it.
 */

void *diag_p_pseterr(const char *name, const int line, const int code);
int diag_p_iseterr(const char *name, const int line, const int code);
void *diag_p_pfwderr(const char *name, const int line, const int code);
int diag_p_ifwderr(const char *name, const int line, const int code);

/** return NULL, set and print error. */
#define diag_pseterr(C) diag_p_pseterr(CURFILE, __LINE__, (C))
/** return (C), set and print error. */
#define diag_iseterr(C) diag_p_iseterr(CURFILE, __LINE__, (C))

/** return NULL, print error as a debug msg */
#define diag_pfwderr(C) diag_p_pfwderr(CURFILE, __LINE__, (C))
/** return (C), print error as a debug msg */
#define diag_ifwderr(C) diag_p_ifwderr(CURFILE, __LINE__, (C))


/** Return the last error and clears it.
 */
int diag_geterr(void);

/** Get textual description of error
 *
 * Note, it returns a pointer to a statically allocated buffer
 * common to all threads and that must not be free'd.
 */
const char *diag_errlookup(const int code);

/*
 * "calloc" and "malloc" that log errors when they fail.
 * As with the seterr functions, only "diag_calloc" and "diag_malloc"
 * are intended to be called directly.
 *
 */

// Do not call directly.
int diag_fl_alloc(const char *fName, const int line,
	void **pp, size_t n, size_t s, bool allocIsCalloc);

/** calloc() with logging (clears data)
 * @param P: *ptr
 * @param N: number of (sizeof) elems to allocate
 * @note there is no size argument - it gets it directly
 * using sizeof. This makes it a little unusual, but reduces potential errors.
 */
#define diag_calloc(P, N) diag_fl_alloc(CURFILE, __LINE__, \
	((void **)(P)), (N), sizeof(**(P)), true)

/** malloc() with logging.
 * Same as diag_calloc, but without clearing.
 */
#define diag_malloc(P, N) diag_fl_alloc(CURFILE, __LINE__, \
	((void **)(P)), (N), sizeof(**(P)), false)

/** Add a string to array-of-strings (argv style)
* @param elems: number of elements already in table
* @return new table ptr, NULL if failed
*/
char **strlist_add(char **list, const char *news, int elems);

/** Free argv-style string list
* @param elems: number of strings in list
*/
void strlist_free(char **slist, int elems);

// Atomics

// Atomically accessed types
typedef struct {
	diag_mtx mtx;
	bool v;
} diag_atomic_bool;
typedef struct {
	diag_mtx mtx;
	int v;
} diag_atomic_int;

#define diag_atomic_init(V) diag_os_initstaticmtx(&((V)->mtx))
#define diag_atomic_del(V) diag_os_delmtx(&((V)->mtx))

void diag_atomic_store_bool(diag_atomic_bool *a, bool d);
void diag_atomic_store_int(diag_atomic_int *a, int d);
bool diag_atomic_load_bool(diag_atomic_bool *a);
int diag_atomic_load_int(diag_atomic_int *a);

/** Check if periodic timers have finished
 *
 * Returns true if the periodic timer is no longer necessary, ie. if the diag_end has been
 * run. Returns false otherwise.
 */
bool periodic_done(void);

#if defined(__cplusplus)
}
#endif

#endif /* _DIAG_H_ */
