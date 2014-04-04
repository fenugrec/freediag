#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_

/*
 * Serial port settings.
 * We allow most expected settings, except that we need to
 * support arbitrary speeds for some L0 links.
 */


#include "diag.h"

#define IFLUSH_TIMEOUT 30	//timeout to use when calling diag_tty_read from diag_tty_iflush to purge RX buffer.

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


// Included the correct os-specific diag_tty.h file
#ifdef WIN32
	#include "diag_tty_win.h"
#else
	#include "diag_tty_unix.h"
#endif //WIN32

//TODO: move these 3 functions do diag_l2.c ? there is redundance in tty_win.c and tty_unix.c

// this returns dl0d->dl0_handle; I'm not sure why we need a function to do that ?
void *diag_l0_dl0_handle(struct diag_l0_device *dl0d);

//similar; this returns dl0d->dl2_link.
struct diag_l2_link *
diag_l0_dl2_link(struct diag_l0_device *dl0d);

void
diag_l0_set_dl2_link(struct diag_l0_device *dl0d,
	struct diag_l2_link *dl2_link);

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
int diag_tty_control(struct diag_l0_device *dl0d, unsigned int dtr, unsigned int rts);

/* Flush pending input */
// This probably always takes IFLUSH_TIMEOUT to complete since it calls diag_tty_read.
// ret 0 if ok
int diag_tty_iflush(struct diag_l0_device *dl0d);

/* read with timeout, write */
//These deserve a better description. There are be discrepancies between
// implementations because there is no guideline of how these
// should behave.
//TODO : unify diag_tty_read timeouts between win32 and unix
//TODO : unify tty_read return values between win32 & others.
// diag_tty_read : Currently most l0 drivers expect this: (I think)
//	a) read up to (count) bytes until (timeout) expires; return the # of bytes read.
//	b) if no bytes were read and timeout expired, return DIAG_ERR_TIMEOUT
//		*without* diag_iseterr() : many levels call diag_tty_read in
//		a loop until it returns DIAG_ERR_TIMEOUT. If find this a bit counterproductive
//		but that's how it works now (2014/03)
//	c) if there was a real error, return diag_iseterr(ERR)
//	d) returning 0 is interpreted as "EOF"; I find this redundant with
//		returning DIAG_ERR_TIMEOUT: What's the difference between reading EOF
//		from a serial port and reading 0 bytes after having timed out ?
//	e) diag_ttY_read should be blocking, i.e. return only when it "completes":
//		either got [count] bytes or timed out. I see no reason to
//		make this asynchronous (non-blocking).
//	f) if (timeout) was 0, I'm not sure what should happen. I think
//		it should attempt to read (count) bytes available but return
//		immediately. To my knowledge nobody calls diag_ttY_read with timeout=0.
//The windows implementation never returns 0: only DiAG_ERR_TIMEOUT or #bytesread.
ssize_t diag_tty_read(struct diag_l0_device *dl0d,
	void *buf, size_t count, int timeout);
//diag_tty_write :
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
