#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_

/*
 * Serial port settings.
 * We allow most expected settings, except that we need to
 * support arbitrary speeds for some L0 links.
 */


#include "diag.h"
#include "diag_l0.h"	//needed for diag_l0_debug

#define IFLUSH_TIMEOUT 30	//timeout to use when calling diag_tty_read from diag_tty_iflush to purge RX buffer.
		//must not be too long or diag_l0_dumb:slowinit() will not work
#define MAXTIMEOUT	10000	//ms; for diag_tty_read()

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


/*** Public functions ***/
typedef void ttyp;	//used as "(tty_internal_struct *) ttyp" in tty code

/** Get available serial ports
 *
 * @param[out] numports : will hold the # of ports found.
 * @return argv-style list of full port names, that must be free'd with
 * strlist_free()
 *
 */
char **diag_tty_getportlist(int *numports);

/** Open serial port
 * @param portname: serial port device / file / tty name
 * @return new ttyp handle if ok, NULL if failed
 */
ttyp *diag_tty_open(const char *portname);

/** Close serial port
 *
 * Also frees everything allocated in diag_tty_open().
 */
void diag_tty_close(ttyp *tty_int);

/** Set speed/parity.
 *
 * @return 0 if ok.
 */
int diag_tty_setup(ttyp *tty_int,
	const struct diag_serial_settings *pss);

/** Set DTR and RTS lines.
 *
 * Terminology : rts=1 or dtr=1 ==\> set DTR/RTS ==\> set pin at positive voltage !
 * (opposite polarity of the TX/RX pins!!)
 * @return 0 if ok
 */
int diag_tty_control(ttyp *tty_int, unsigned int dtr, unsigned int rts);


/** Flush pending input.
 *
 * This probably always takes IFLUSH_TIMEOUT to complete since it calls diag_tty_read.
 * @return 0 if ok
 */
int diag_tty_iflush(ttyp *tty_int);

// diag_tty_read : (count >0 && timeout >0)
//	a) read up to (count) bytes until (timeout) expires; return the # of bytes read.
//	b) if no bytes were read and timeout expired: return DIAG_ERR_TIMEOUT
//		*without* diag_iseterr(); L2 code uses this for message splitting.
//	c) if there was a real error, return diag_iseterr(x)
//	d) never return 0
//	TODO : clarify if calling with timeout==0 is useful (probably not, nobody does).
ssize_t diag_tty_read(ttyp *tty_int,
	void *buf, size_t count, unsigned int timeout);

/** Write bytes to tty (blocking).
 *
 *	@param count: Attempt to write [count] bytes, block (== do not return) until write has completed.
 *  @return # of bytes written; \<0 if error.
 * @note It is unclear whether the different OS mechanisms to flush write buffers actually
 * guarantee that serial data has physically sent,
 * or only that the data was flushed as far "downstream" as possible, for example
 * to a UART / device driver buffer.
 */
ssize_t diag_tty_write(ttyp *tty_int,
	const void *buf, const size_t count);


/** Send a break on TXD.
 * @param ms: duration (milliseconds)
 * @return 0 if ok, after clearing break
 */
int diag_tty_break(ttyp *tty_int, const unsigned int ms);

/** Send fixed 25ms break pattern on TXD.
 *
 * Sets break for 25ms and returns after requested duration.
 * This is for ISO14230 fast init : typically diag_tty_fastbreak(tty_int, 50)
 * @param ms: Total pattern length (\>25ms)
 * @return 0 if ok; returns [ms] after starting the break.
 */
int diag_tty_fastbreak(ttyp *tty_int, const unsigned int ms);


#endif /* _DIAG_TTY_H_ */
