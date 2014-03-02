// diag_tty.h pour win32. win64 = plus tard...

#ifndef _DIAG_TTY_WIN_H_
#define _DIAG_TTY_WIN_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <basetsd.h>
#include <winbase.h> //maybe just <windows.h> ?
typedef SSIZE_T ssize_t;

#include "diag_l1.h"

/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_if.c)
 */

//struct diag_ttystate	//OS-dependant !
//TODO : define struct diag_ttystate to include DCB and COMMTIMEOUTS

#define DL0D_INVALIDHANDLE INVALID_HANDLE_VALUE
typedef HANDLE dl0d_handletype;	//just used for casts

//diag_l0_device : some parts of this are the same for every OS.
// the platform-specific stuff should probably only be used in the associated diag_ttyXXX.c file
// diag_l0_sim.c is an offender of this : it uses "fd" for some nefarious purpose
struct diag_l0_device
{
	void *dl0_handle;					/* Handle for the L0 switch */
	const struct diag_l0 *dl0;		/* The L0 switch */
	struct diag_l2_link *dl2_link;	/* The L2 link */

	HANDLE fd;						/* File handle */
	char *name;					/* device name */
	DCB *ttystate;	/* Holds OS specific tty info; a DCB on windoze */

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
	int speed;	//in bps of course
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

void diag_tty_close(struct diag_l0_device **ppdl0d);

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
#endif /* _DIAG_TTY_WIN_H_ */
