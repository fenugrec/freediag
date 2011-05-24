#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_
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
 */

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__linux__) && (TRY_POSIX == 0)
#include <linux/serial.h>	/* For Linux-specific struct serial_struct */
#include <fcntl.h>
#endif
#include <termios.h>	/* For struct termios */

#ifdef WIN32
#include <Basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#include "diag_l1.h"

/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_if.c)
 */

struct diag_l1_initbus_args;

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

struct diag_serial_settings;

struct diag_l0	//XXX Why is this in here ??
{
	const char	*diag_l0_textname;	/* Useful textual name */
	const char	*diag_l0_name;	/* Short, unique text name for user interface */

	int 	diag_l0_type;			/* See type description above */
	
	/* function pointers to L0 code */
	int	(*diag_l0_init)(void);
	struct diag_l0_device *(*diag_l0_open)(const char *subinterface,
		int iProtocol);
	int	(*diag_l0_close)(struct diag_l0_device **);
	int	(*diag_l0_initbus)(struct diag_l0_device *,
		struct diag_l1_initbus_args *in);
	int	(*diag_l0_send)(struct diag_l0_device *,
		const char *subinterface, const void *data, size_t len);
	int	(*diag_l0_recv)(struct diag_l0_device *,
		const char *subinterface, void *data, size_t len, int timeout);
	int	(*diag_l0_setspeed)(struct diag_l0_device *,
		const struct diag_serial_settings *pss);
	int	(*diag_l0_getflags)(struct diag_l0_device *);
};

/*
 * Serial port settings.
 * We allow most expected settings, except that we need to
 * support arbitrary speeds for some L0 links.
 */

/*
 * Parity settings
 */
enum diag_parity {
	diag_par_e = 1,	/* Even parity */
	diag_par_o = 2,	/* Odd parity */
	diag_par_n = 3	/* No parity */
};

enum diag_databits {
	diag_databits_8 = 8,
	diag_databits_7 = 7,
	diag_databits_6 = 6,
	diag_databits_5 = 5
};

enum diag_stopbits {
	diag_stopbits_1 = 1,
	diag_stopbits_2 = 2
};

struct diag_serial_settings {
	int speed;
	enum diag_databits databits;
	enum diag_stopbits stopbits;
	enum diag_parity parflag;
};

void *diag_l0_dl0_handle(struct diag_l0_device *dl0d);

struct diag_l2_link *
diag_l0_dl2_link(struct diag_l0_device *dl0d);

void
diag_l0_set_dl2_link(struct diag_l0_device *dl0d,
	struct diag_l2_link *dl2_link);

const struct diag_l0 *diag_l0_device_dl0(struct diag_l0_device *dl0d);

extern int diag_l0_debug;

/* Open, close device */
int diag_tty_open(struct diag_l0_device **ppdl0d,
	const char *subinterface,
	const struct diag_l0 *,
	void *handle);

int diag_tty_close(struct diag_l0_device **ppdl0d);

/* Set speed/parity etc */
int diag_tty_setup(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pss);

int diag_tty_control(struct diag_l0_device *dl0d, int dtr, int rts);

/* Flush pending input */
int diag_tty_iflush(struct diag_l0_device *dl0d);

/* read with timeout, write */
ssize_t diag_tty_read(struct diag_l0_device *dl0d,
	void *buf, size_t count, int timeout);
ssize_t diag_tty_write(struct diag_l0_device *dl0d,
	const void *buf, const size_t count);
int diag_tty_break(struct diag_l0_device *dl0d, const int);

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_TTY_H_ */
