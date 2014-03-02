#ifndef _DIAG_TTY_UNIX_H_
#define _DIAG_TTY_UNIX_H_

/* This is totally unix-exclusive and should not be included directly; other files should
 * " #include diag_tty.h " 
 * and diag_tty.h takes care of including the right os-specific diag_ttyXYZ.h file.
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__linux__) && (TRY_POSIX == 0)
	#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
	#include <fcntl.h>
#endif

#include <termios.h>	/* For struct termios*/


/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_if.c)
 */

struct diag_ttystate
{
	/*
	 * For recording state before we mess with the interface:
	 */
#if defined(__linux__) && (TRY_POSIX == 0)
	struct serial_struct dt_osinfo;
#endif
	struct termios dt_otinfo;
	int dt_modemflags;

	/* For recording state after/as we mess with the interface */
#if defined(__linux__) && (TRY_POSIX == 0)
	struct serial_struct dt_sinfo;
#endif
	struct termios dt_tinfo;

};

#define DL0D_INVALIDHANDLE -1
typedef int dl0d_handletype;	//just used for casts

struct diag_l0_device
{
	void *dl0_handle;					/* Handle for the L0 switch */
	const struct diag_l0 *dl0;		/* The L0 switch */
	struct diag_l2_link *dl2_link;	/* The L2 link */

	int fd;						/* File descriptor */
	char *name;					/* device name */
	struct diag_ttystate *ttystate;	/* Holds OS specific tty info */

#if !defined(__linux__) || (TRY_POSIX == 1)
	volatile int expired;		/* Timer expiration */
#if defined(_POSIX_TIMERS)
	/* POSIX timers: */
	timer_t timerid;			/* Posix timer */
#endif
#endif
};



#if defined(__cplusplus)
}
#endif
#endif /*_DIAG_TTY_UNIX_H_ */
