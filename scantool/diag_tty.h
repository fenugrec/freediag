#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_

/*
 * Serial port settings.
 * We allow most expected settings, except that we need to
 * support arbitrary speeds for some L0 links.
 */


#include "diag.h"

#define IFLUSH_TIMEOUT 30	//timeout to use when calling diag_tty_read from diag_tty_iflush to purge RX buffer.
		//must not be too long or diag_l0_dumb:slowinit() will not work

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
	unsigned int speed;	//in bps of course
	enum diag_databits databits;
	enum diag_stopbits stopbits;
	enum diag_parity parflag;
};

/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_if.c)
 * A "diag_l0_device" is a unique association between an l0 driver (diag_l0_dumb for instance)
 * and a given serial port.
 */
struct diag_l0_device
{
	void *dl0_handle;					/* Handle for the L0 switch */
	const struct diag_l0 *dl0;		/* The L0 driver's diag_l0 */
	struct diag_l2_link *dl2_link;	/* The L2 link using this dl0d */
	char *name;					/* device name, like /dev/ttyS0 or \\.\COM3 */
	void *tty_int;			/* generic holder for internal tty stuff */
};

extern int diag_l0_debug;

/* Open, close device */
int diag_tty_open(struct diag_l0_device **ppdl0d,
	const char *subinterface,
	const struct diag_l0 *,
	void *handle);

//diag_tty_close : free & close everything in ppdl0d (including dl0d itself)
void diag_tty_close(struct diag_l0_device **ppdl0d);

/* Set speed/parity etc, return 0 if ok. */
int diag_tty_setup(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pss);

//set DTR and RTS lines :
 //~  terminology : rts=1 or dtr=1  ==> set DTR/RTS ==> set pin at positive voltage !
 //~  (opposite polarity of the TX/RX pins!!)
//Ret 0 if ok
int diag_tty_control(struct diag_l0_device *dl0d, unsigned int dtr, unsigned int rts);

/* Flush pending input */
// This probably always takes IFLUSH_TIMEOUT to complete since it calls diag_tty_read.
// ret 0 if ok
int diag_tty_iflush(struct diag_l0_device *dl0d);

// diag_tty_read : (count >0 && timeout >0)
//	a) read up to (count) bytes until (timeout) expires; return the # of bytes read.
//	b) if no bytes were read and timeout expired: return DIAG_ERR_TIMEOUT
//		*without* diag_iseterr(); L2 code uses this for message splitting.
//	c) if there was a real error, return diag_iseterr(x)
//	d) never return 0
//	TODO : clarify if calling with timeout==0 is useful (probably not, nobody does).
ssize_t diag_tty_read(struct diag_l0_device *dl0d,
	void *buf, size_t count, int timeout);

//diag_tty_write: (count >0)
//	a) attempt to write <count> bytes, block (== do not return) until write has completed.
//		It is unclear whether the different OS mechanisms to flush write buffers actually
//		guarantee that serial data has physically sent,
//		or only that the data was flushed as far "downstream" as possible, for example
//		in a UART / device driver buffer.
//	b) return # of bytes written; <0 if error.
ssize_t diag_tty_write(struct diag_l0_device *dl0d,
	const void *buf, const size_t count);

// diag_tty_break: send a [ms] break on TX, return after clearing break
// ret 0 if ok
int diag_tty_break(struct diag_l0_device *dl0d, const unsigned int ms);

/*
 * diag_tty_fastbreak: fixed 25ms break; return [ms] after starting the break.
 * This is for ISO14230 fast init : typically diag_tty_fastbreak(dl0d, 50)
 * ret 0 if ok
 */
int diag_tty_fastbreak(struct diag_l0_device *dl0d, const unsigned int ms);



#endif /* _DIAG_TTY_H_ */
