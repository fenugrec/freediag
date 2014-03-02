#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_

/*
 * Serial port settings.
 * We allow most expected settings, except that we need to
 * support arbitrary speeds for some L0 links.
 */


#include "diag_l1.h"

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


// Included the correct os-specific diag_tty.h file
#ifdef WIN32
	#include "diag_tty_win.h"
#else
	#include "diag_tty_unix.h"
#endif //WIN32


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


#endif /* _DIAG_TTY_H_ */
