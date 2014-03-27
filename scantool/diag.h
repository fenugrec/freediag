#ifndef _DIAG_H_
#define _DIAG_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id$
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
 * Library user header file
 */

#ifdef CMAKE_ENABLED
	#include "cconf.h"
#else
	#include "config.h"	//still with autotoools. Both should work...
#endif

#ifdef WIN32
	#define _WIN32_WINNT 0x0500	//use > winXP features...
	#include <time.h>
	#include <basetsd.h>
	#include <windows.h>
#else
	#include <sys/types.h>
	#include <sys/time.h>	/* For timeval */
#endif

#include <stdint.h>		/* For uint8_t, etc. This is a C99 header */
#include <stdio.h>		/* For FILE */

#if defined(__cplusplus)
extern "C" {
#endif

// Nice to have anywhere...
#define MIN(_a_, _b_) (((_a_) < (_b_) ? (_a_) : (_b_)))
#ifdef __GNUC__
	#define UNUSED(X) 	X __attribute__((unused))	//magic !
#else
	#define UNUSED(X)	X	//how can we suppress "unused parameter" warnings on other compilers?
#endif // __GNUC__


#ifdef HAVE_GETTIMEOFDAY
	#include <sys/time.h>	//probably the right place where gettimeofday() would be defined ?
#else	//no HAVE_GETTIMEOFDAY	//like on win32?
	struct timezone {
	  int  tz_minuteswest;
	  int  tz_dsttime;
	};

	//this is a bare implementation with no timezone support. Returns 0. see diag_os.c
	int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif //HAVE_GETTIMEOFDAY

#ifndef HAVE_TIMERSUB
	//bare implementation, in diag_os.c
	void timersub(struct timeval *a, struct timeval *b, struct timeval *res);
#endif	//HAVE_TIMERSUB

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#define DB_FILE "./freediag_carsim_all.db"	//default simfile for CARSIM interface
#define DIAG_NAMELEN	256

/* For diagnostics */

#define FLFMT "%s:%d:  "

#ifndef CMAKE_ENABLED
	#define CURFILE __FILE__
	//with autotools __FILE__ seems OK, so less reason for these warnings :

	//#warning *** Without CMake, the debbuging & error messages will show the
	//#warning *** absolute path of the relevant source file. This is annoying
	//#warning *** but not severe. See diag.h
#endif

//CURFILE will be defined by CMake on a per-file basis !
#ifdef MSVC
	#warning MSVC may not work with the CURFILE macro. See diag.h
	// apparently some (all ?) MSVC compilers don't support per-file defines, so CURFILE would be the
	// same for all source files.
	// The disadvantage of __FILE__ is that it often (always ?) holds the absolute path of the file,
	// not just the filename. For our debugging messages we only care about the filename, hence CURFILE.
	#define CURFILE __FILE__
#endif
#define FL CURFILE, __LINE__


/*
 * Many receive buffers are set to 1024, which seems large. At least
 * now it is defined in one place.
 */
#define MAXRBUF 1024

typedef uint8_t target_type, source_type, databyte_type, command_type;
typedef uint16_t flag_type;	//this is used for L2 type flags (diag_l2.h)
			//only used for diag_l2_proto_startcomms() parameter
			//XXX diag_l2_proto_startcomms specifies "uint32_t type"...

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
#define DIAG_IOCTL_SETSPEED	0x2101	/* Set speed, bits etc */
					/* Struct diag_serial_settings is passed */
#define DIAG_IOCTL_INITBUS	0x2201	/* Initialise the ecu bus, data is diag_l1_init */
#define DIAG_IOCTL_IFLUSH 0x2202	//flush input buffers

/* debug control */
// flag containers : diag_l0_debug, diag_l1_debug diag_l2_debug, diag_l3_debug, diag_cmd_debug

#define DIAG_DEBUG_OPEN		0x01	/* Open events */
#define DIAG_DEBUG_CLOSE	0x02	/* Close events */
#define DIAG_DEBUG_READ		0x04	/* Read events */
#define DIAG_DEBUG_WRITE	0x08	/* Write events */
#define DIAG_DEBUG_IOCTL	0x10	/* Ioctl stuff (setspeed etc) */
#define DIAG_DEBUG_PROTO	0x20	/* Other protocol stuff */
#define DIAG_DEBUG_INIT		0x40	/* Initialisation stuff */
#define DIAG_DEBUG_DATA		0x80	/* Dump data depending on other flags */
#define DIAG_DEBUG_TIMER	0x100	/* Timer stuff */

/*
 * Message handling.
 *
 * These are messages passed to or from the layer 2 and layer 3 code
 * which cause data to be transmitted to layer 1
 *
 * The receiver of the message *must* copy the data if it wants it !!!
 */
struct diag_msg
{
	uint8_t	fmt;			/* Message format: */
	#define DIAG_FMT_ISO_FUNCADDR	0x01	/* ISO Functional addressing */
	#define DIAG_FMT_FRAMED		0x02	/* Rcvd data is framed, ie not raw */
	#define	DIAG_FMT_DATAONLY	0x04	/* Rcvd data had L2/L3 headers removed */
	#define DIAG_FMT_CKSUMMED	0x08	/* L2 checked the checksum */

	uint8_t	type;		/* Type from received frame */
	uint8_t	dest;		/* Destination from received frame */
	uint8_t	src;		/* Source from received frame */
	uint8_t	len;		/* calculated data length */
	uint8_t	*data;		/* The data */

	struct timeval	 rxtime;	/* Processed time */
	struct diag_msg	*next;		/* For linked lists of messages */

	uint8_t	mcnt;		/* Number of elements on this list */

	uint8_t	*idata;		/* For free() of data later */
	uint8_t	iflags;		/* Internal flags */
	#define	DIAG_MSG_IFLAG_MALLOC	1	/* We malloced; we Free */
};

struct diag_msg	*diag_allocmsg(size_t datalen);	/* Alloc a new message */
struct diag_msg	*diag_dupmsg(struct diag_msg *);	/* Duplicate a message */
struct diag_msg	*diag_dupsinglemsg(struct diag_msg *); /* same, but just the 1st bit */
void diag_freemsg(struct diag_msg *);	/* Free a msg that we dup'ed */
uint8_t diag_cks1(uint8_t *data, unsigned int len);	//calculate 8bit checksum on [len] bytes

/*
 * General functions
 */
int diag_init(void);
int diag_end(void);

//diag_data_dump : print (len) uin8_t bytes from data[], to FILE (i.e. stderr, etc.)
void diag_data_dump(FILE *out, const void *data, size_t len);
//smartcat : only verifies if s1 is not too large !
void smartcat(char *p1, const size_t s1, const char *p2 );

/*
 * Error functions.
 * "pflseterr" and "iflseterr" aren't intended to be called directly.
 * Use "diag_pseterr" (returns a NULL pointer) or
 * "diag_iseterr" (returns the passed in error code),
 * which will save where the error took place and optionally log it.
 */

void *diag_pflseterr(const char *name, const int line, const int code);
int diag_iflseterr(const char *name, const int line, const int code);

#define diag_pseterr(C) diag_pflseterr(CURFILE, __LINE__, (C))
#define diag_iseterr(C) diag_iflseterr(CURFILE, __LINE__, (C))

/*
 * diag_geterr returns the last error and clears it.
 */
int diag_geterr(void);

/*
 * Textual description of error.
 */
const char *diag_errlookup(const int code);

/*
 * "calloc" and "malloc" that log errors when they fail.
 * As with the seterr functions, only "diag_calloc" is intended to be
 * called directly.
 *
 * Note that diag_calloc is NOT passed in the size - it gets it directly
 * using sizeof. This makes it a little unusual, but reduces potential errors.
 */

//diag_flcalloc (srcfilename, srcfileline, ptr, num,size) = allocate (num*size) bytes
//do not call directly!
int diag_flcalloc(const char *name, const int line,
	void **p, size_t n, size_t s);

//diag_flmalloc : do not call directly !
int diag_flmalloc(const char *name, const int line, void **p, size_t s);

#define diag_calloc(P, N) diag_flcalloc(CURFILE, __LINE__, \
	((void **)(P)), (N), sizeof(*(*P)))

#define diag_malloc(P, S) diag_flmalloc(CURFILE, __LINE__, \
	((void **)(P)), (S))

/*
 * Auto generated config functions (see genconfig.sh)
 * Return 0 if normal exit; other exit codes are OR'ed from diag_l*_*_add()
 */
int diag_l0_config(void);
int diag_l2_config(void);

#if defined(__cplusplus)
}
#endif

#include "diag_os.h"	/* OS specific definitions. */

#endif /* _DIAG_H_ */
