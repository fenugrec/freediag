/*
 *	freediag - Vehicle Diagnostic Utility

 *************************************************************************
 * dumbtest (c) 2014-2015 fenugrec
 * Diag, Layer 0, interface tester. This should *NOT* be used while
 * a vehicle is connected; only for debugging the electrical interface itself.

 * This is a dummy l0 driver : most functions do nothing except
 * _open which opens the subinterface, runs the specified test, and closes everything
 * before returning.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h> //for memcmp

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"

struct dt_device {
	int protocol; // set in dt_open with specified iProtocol
	struct diag_serial_settings serial;

	/* "dumbopts" flags. Copied from l0_dumb driver */
	/* Using a struct cfgi for each of these really seems like overkill...
	 * current approach is to have a cfgi for an "int dumbopts", that is parsed into
	 * these flags in dumb_open(). Hence, the shortname + descriptions defined here are
	 * unused for the moment.
	 */
	bool use_L;
#define DD_USEL "Use RTS to drive the L line for init. Interface must support this."
#define DS_USEL "USEL"
	bool clr_dtr;
#define DD_CLRDTR "Always keep DTR cleared (neg. voltage). Unusual."
#define DS_CLRDTR "CLRDTR"
	bool set_rts;
#define DD_SETRTS "Always keep RTS set (pos. voltage). Unusual; do not use with USE_L."
#define DS_SETRTS "SETRTS"
	bool man_break;
#define DD_MANBRK                                                                         \
	"Send manual breaks (essential for USB-serial ICs that don't support 5bps.)"
#define DS_MANBRK "MANBRK"
	bool lline_inv;
#define DD_INVL "Invert polarity of the L line. Unusual."
#define DS_INVL "INVL"
	bool fast_break;
#define DD_FASTBK "Try alternate iso14230 fastinit code."
#define DS_FASTBK "FASTB"
	bool blockduplex;
#define DD_BKDUPX "Use message-based half duplex removal if P4==0."
#define DS_BKDUPX "BLKDUP"

	struct cfgi port;
	struct cfgi dumbopts;
#define DUMBOPTS_SN "dumbopts"
#define DUMBOPTS_DESC                                                                     \
	"Dumb interface option flags; addition of the desired flags:\n"                   \
	" 0x01 : USE_LLINE : use if the L line (driven by RTS) is required for init. "    \
	"Interface must support this\n"                                                   \
	"\t(VAGTOOL for example).\n"                                                      \
	" 0x02 : CLEAR_DTR : use if your interface needs DTR to be always clear (neg. "   \
	"voltage).\n"                                                                     \
	"\tThis is unusual. By default DTR will always be SET (pos. voltage)\n"           \
	" 0x04 : SET_RTS : use if your interface needs RTS to be always set (pos. "       \
	"voltage).\n"                                                                     \
	"\tThis is unusual. By default RTS will always be CLEAR (neg. voltage)\n"         \
	"\tThis option should not be used with USE_LLINE.\n"                              \
	" 0x08 : MAN_BREAK : essential for USB-serial converters that don't support "     \
	"5bps\n"                                                                          \
	"\tsuch as FTDI232*, P230* and other ICs (enabled by default).\n"                 \
	" 0x10: LLINE_INV : Invert polarity of the L line. see\n"                         \
	"\tdoc/dumb_interfaces.txt !! This is unusual.\n"                                 \
	" 0x20: FAST_BREAK : use alternate iso14230 fastinit code.\n"                     \
	" 0x40: BLOCKDUPLEX : use message-based half duplex removal (if P4==0)\n\n"       \
	"ex.: \"dumbopts 9\" for MAN_BREAK and USE_LLINE.\n"

	ttyp *tty_int; /** handle for tty stuff */
};

// dumbopts flags set according to particular interface type (VAGtool vs SE etc.)
#define USE_LLINE                                                                         \
	0x01 // interface maps L line to RTS : setting RTS normally pulls L down to 0 .
#define CLEAR_DTR 0x02 // have DTR cleared constantly (unusual, disabled by default)
#define SET_RTS 0x04   // have RTS set constantly (also unusual, disabled by default).
#define MAN_BREAK 0x08 // force bitbanged breaks for inits; enabled by default
#define LLINE_INV 0x10 // invert polarity of the L line if set. see doc/dumb_interfaces.txt
#define FAST_BREAK 0x20 // do we use diag_tty_fastbreak for iso14230-style fast init.
#define BLOCKDUPLEX                                                                       \
	0x40 // This allows half duplex removal on a whole message if P4==0 (see
	     // diag_l1_send())
#define DUMBDEFAULTS (MAN_BREAK | BLOCKDUPLEX) // default set of flags

extern const struct diag_l0 diag_l0_dumbtest;

static int dt_send(struct diag_l0_device *dl0d, UNUSED(const char *subinterface),
		   const void *data, size_t len);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
dt_init(void) {
	/* Global init flag */
	static int dt_initdone = 0;

	if (dt_initdone) {
		return 0;
	}

	/* Do required scheduling tweeks */
	diag_os_sched();
	dt_initdone = 1;

	return 0;
}

// dtest_1 : slow pulse TXD with diag_tty_break; 400ms / 200ms cycles.

static void
dtest_1(struct diag_l0_device *dl0d) {
	int i;
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr, "Starting test 1: pulsing TXD=1, 1s, TXD=0, 500ms:");
	for (i = 0; i <= 4; i++) {
		diag_os_millisleep(1000);
		if (diag_tty_break(dev->tty_int, 500)) {
			break;
		}
		fprintf(stderr, ".");
	}
	fprintf(stderr, "\n");
	return;
}

// dtest_2 : fast pulse TXD by sending 0x55 @ 10.4kps with 5ms interbyte
static void
dtest_2(struct diag_l0_device *dl0d) {
	int i, pc = 0;
	const int iters = 300;
	uint8_t patternbyte = 0x55;
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr, "Starting test 2: sending 0x55 with P4=5ms:");
	for (i = 0; i <= iters; i++) {
		if (diag_tty_write(dev->tty_int, &patternbyte, 1) != 1) {
			fprintf(stderr, "write error\n");
			break;
		}
		if ((10 * i / iters) != pc) {
			pc += 1;
			fprintf(stderr, ".");
		}
		diag_os_millisleep(5);
	}
	fprintf(stderr, "\n");
	return;
}

// dtest_3: slow pulse RTS
static void
dtest_3(struct diag_l0_device *dl0d) {
	int i;
	struct dt_device *dev = dl0d->l0_int;
	fprintf(stderr, "Starting test 3: pulsing RTS=1, 1s, RTS=0, 500ms:");

	for (i = 0; i <= 4; i++) {
		if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), 1)) {
			break;
		}
		diag_os_millisleep(1000);
		if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), 0)) {
			break;
		}
		diag_os_millisleep(500);
		fprintf(stderr, ".");
	}
	diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts);
	fprintf(stderr, "\n");
	return;
}

// dtest_4: slow pulse DTR
static void
dtest_4(struct diag_l0_device *dl0d) {
	int i;
	struct dt_device *dev = dl0d->l0_int;
	fprintf(stderr, "Starting test 4: pulsing DTR=1, 1s, DTR=0, 500ms:");

	for (i = 0; i <= 4; i++) {
		if (diag_tty_control(dev->tty_int, 1, dev->set_rts)) {
			break;
		}
		diag_os_millisleep(1000);
		if (diag_tty_control(dev->tty_int, 0, dev->set_rts)) {
			break;
		}
		diag_os_millisleep(500);
		fprintf(stderr, ".");
	}
	diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts);
	fprintf(stderr, "\n");
	return;
}

// dtest_5 : fast pulse TXD with diag_tty_break;

static void
dtest_5(struct diag_l0_device *dl0d) {
	int i, pc = 0;
	struct dt_device *dev = dl0d->l0_int;
	const int iters = 40;
	fprintf(stderr, "Starting test 5: pulsing TXD=1, 50, TXD=0, 25ms:");
	for (i = 0; i <= iters; i++) {
		diag_os_millisleep(50);
		if (diag_tty_break(dev->tty_int, 25)) {
			fprintf(stderr, "break error\n");
			break;
		}
		if ((10 * i / iters) != pc) {
			pc += 1;
			fprintf(stderr, ".");
		}
	}
	fprintf(stderr, "\n");
	return;
}

// dtest_6 : fast pulse TXD with diag_tty_fastbreak;

static void
dtest_6(struct diag_l0_device *dl0d) {
	int i, pc = 0;
	struct dt_device *dev = dl0d->l0_int;
	const int iters = 50;
	fprintf(stderr, "Starting test 6: pulsing TXD=1, 50ms, TXD=0, 25ms:");
	for (i = 0; i <= iters; i++) {
		if (diag_tty_fastbreak(dev->tty_int, 50)) {
			fprintf(stderr, "fastbreak error\n");
			break;
		}
		if ((10 * i / iters) != pc) {
			pc += 1;
			fprintf(stderr, ".");
		}
	}
	fprintf(stderr, "\n");
	return;
}

// dtest_7 : half duplex echo removal 1 : send bytes and remove echo
// one by one; P4=0. Print per-byte time to do this; use _dumb_send() instead
// of diag_tty directly, like l1_send().
static void
dtest_7(struct diag_l0_device *dl0d) {
	uint8_t i, pc = 0, echo;
	int rv, badechos = 0;
	unsigned long long ti, tf = 0; // measure inner time
	struct dt_device *dev = dl0d->l0_int;

#define DT7_ITERS 100
	fprintf(stderr, "Starting test 7: half duplex single echo removal:");

	for (i = 0; i < DT7_ITERS; i++) {
		echo = i - 1;          // init to bad value
		ti = diag_os_gethrt(); // get starting time.
		if (dt_send(dl0d, NULL, &i, 1)) {
			break;
		}

		rv = diag_tty_read(dev->tty_int, &echo, 1, 1000);
		if (rv != 1) {
			fprintf(stderr, "\ndt7: tty_read rets %d.\n", rv);
			break;
		}
		tf = tf + diag_os_gethrt() - ti;
		// check echo
		if (echo != i) {
			badechos++;
		}

		if ((10 * i / DT7_ITERS) != pc) {
			pc += 1;
			fprintf(stderr, ".");
		}
	} // for
	fprintf(stderr, "\n");
	tf = tf / DT7_ITERS; // average time per byte
	printf("Average speed : %d us/byte. %d good; %d bad echos received.\n",
	       (int)diag_os_hrtus(tf), i, badechos);

	return;
} // dtest_7

// dtest_8 : block half duplex removal; send 10 bytes per message
static void
dtest_8(struct diag_l0_device *dl0d) {
#define DT8_MSIZE 10
	uint8_t tx[DT8_MSIZE], echo[DT8_MSIZE];
	int i, rv = -1, badechos = 0;
	unsigned long long ti, tf = 0;
	struct dt_device *dev = dl0d->l0_int;

#define DT8_ITERS 10
	fprintf(stderr, "Starting test 8: half duplex block echo removal:");
	// fill i[] first
	for (i = 0; i < DT8_MSIZE; i++) {
		tx[i] = (uint8_t)i;
	}

	for (i = 0; i <= DT8_ITERS; i++) {
		ti = diag_os_gethrt(); // get starting time.
		if (dt_send(dl0d, NULL, tx, DT8_MSIZE)) {
			break;
		}

		rv = diag_tty_read(dev->tty_int, echo, DT8_MSIZE, 100 + 5 * DT8_MSIZE);
		if (rv != DT8_MSIZE) {
			fprintf(stderr, "\ndt8: tty_read rets %d.\n", rv);
			break;
		}
		tf = tf + diag_os_gethrt() - ti;
		// check echo
		if (memcmp(tx, echo, DT8_MSIZE) == 0) {
			// ok
			rv = 0;
		} else {
			badechos++;
		}
		fprintf(stderr, ".");
	} // for

	tf = tf / (DT8_ITERS * DT8_MSIZE); // average time per byte
	fprintf(stderr, "\n");
	if (rv != 0) {
		printf("Error, test did not complete.\n");
	} else {
		printf("Average speed : %d us/byte. %d bad echos received.\n",
		       (int)diag_os_hrtus(tf), badechos);
	}

	return;
} // dtest_8

// dtest_9 : test accuracy of read timeouts.
static void
dtest_9(struct diag_l0_device *dl0d) {
#define DT9_ITERS 4
	unsigned int i;
	int iters;
	uint8_t garbage[MAXRBUF];
	unsigned long long t0, tf;
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr, "Starting test 9: checking accuracy of read timeouts:\n");
	diag_tty_iflush(dev->tty_int); // purge before starting

	for (i = 10; i <= 200; i += 20) {
		t0 = diag_os_gethrt();
		for (iters = 0; iters < DT9_ITERS; iters++) {
			diag_tty_read(dev->tty_int, garbage, MAXRBUF, i);
		}
		tf = (diag_os_gethrt() - t0) / DT9_ITERS; // average measured timeout
		printf("Timeout=%d: avg=%dms\n", i, (int)(diag_os_hrtus(tf) / 1000));
	}

	return;
} // dtest_9

// dtest_10 == dtest_2 with a different speed

// dtest_11 : test incomplete read timeout (needs half-duplex connection)
static void
dtest_11(struct diag_l0_device *dl0d) {
#define DT11_ITERS 4
	unsigned int i;
	int iters, rv;
	uint8_t garbage[MAXRBUF];
	unsigned long long t0, tf;
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr,
		"Starting test 11: half-duplex incomplete read timeout accuracy:\n");
	diag_tty_iflush(dev->tty_int); // purge before starting

	for (i = 10; i <= 180; i += 20) {
		tf = 0;
		for (iters = 0; iters < DT11_ITERS; iters++) {
			uint8_t tc = i;
			if ((rv = dt_send(dl0d, NULL, &tc, 1))) {
				goto failed;
			}
			t0 = diag_os_gethrt();
			if ((rv = diag_tty_read(dev->tty_int, garbage, MAXRBUF, i)) != 1) {
				// failed: purge + try next timeout value
				fprintf(stderr, "failed @ timeout=%d : %s\n", i,
					diag_errlookup(rv));
				diag_tty_iflush(dev->tty_int);
				break;
			}
			tf = tf + diag_os_gethrt() - t0;
		}
		tf = tf / DT11_ITERS;
		printf("Timeout=%d: avg=%dms\n", i, (int)(diag_os_hrtus(tf) / 1000));
	}
	return;
failed:
	fprintf(stderr, "Problem during test! %s\n", diag_errlookup(rv));
	return;
} // dtest_11

// dtest_12 : diag_tty_write() duration
static void
dtest_12(struct diag_l0_device *dl0d) {
#define DT12_ITERS 4
	unsigned int i;
	int iters;
	uint8_t garbage[MAXRBUF];
	unsigned long long t0, tf;   // measure inner time
	unsigned long long ts1, ts2; // measure overall loop
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr, "Starting test 12: diag_tty_write() duration:\n");
	diag_tty_iflush(dev->tty_int); // purge before starting

	for (i = 1; i <= 50; i += 5) {
		tf = 0;
		printf("len=%d:", i);
		ts1 = diag_os_gethrt();
		for (iters = 0; iters < DT12_ITERS; iters++) {
			unsigned long long tt1;
			t0 = diag_os_gethrt();
			if (dt_send(dl0d, NULL, garbage, i)) {
				goto failed;
			}
			tt1 = diag_os_gethrt();
			tf = tf + (tt1 - t0);
			printf("\t%luus", (long unsigned int)(diag_os_hrtus(tt1 - t0)));
			(void)diag_tty_read(dev->tty_int, garbage, MAXRBUF, 5);
		}
		ts2 = (diag_os_gethrt() - ts1) / DT12_ITERS;
		tf = tf / DT12_ITERS;
		printf(" => avg=%dms / %dms\n", (int)(diag_os_hrtus(tf) / 1000),
		       (int)(diag_os_hrtus(ts2) / 1000));
		if (i == 1) {
			i = 0;
		}
	}
	return;
failed:
	fprintf(stderr, "Problem during test!\n");
	return;
}

// dtest_13 : simulate 14230 fastinit : 25ms low, tWUP=50ms, then send 0xAA @ 10.4k; with
// diag_tty_fastbreak

static void
dtest_13(struct diag_l0_device *dl0d) {
	int i, pc = 0;
	const int iters = 50;
	const uint8_t db = 0xAA;
	struct dt_device *dev = dl0d->l0_int;

	fprintf(stderr, "Starting test 6: simulate fastinit:");
	for (i = 0; i <= iters; i++) {
		if (diag_tty_fastbreak(dev->tty_int, 50)) {
			fprintf(stderr, "fastbreak error\n");
			break;
		}
		if (diag_tty_write(dev->tty_int, &db, 1) != 1) {
			fprintf(stderr, "tty_write error\n");
			break;
		}
		diag_tty_iflush(dev->tty_int); // purge echo(s)
		if ((10 * i / iters) != pc) {
			pc += 1;
			fprintf(stderr, ".");
		}
	}
	fprintf(stderr, "\n");
	return;
}

static int
dt_new(struct diag_l0_device *dl0d) {
	struct dt_device *dev;
	int rv;

	assert(dl0d);

	rv = diag_calloc(&dev, 1);
	if (rv != 0) {
		return diag_iseterr(rv);
	}

	dl0d->l0_int = dev;

	rv = diag_cfgn_tty(&dev->port);
	if (rv != 0) {
		free(dev);
		return diag_iseterr(rv);
	}

	rv = diag_cfgn_int(&dev->dumbopts, DUMBDEFAULTS, DUMBDEFAULTS);
	if (rv != 0) {
		diag_cfg_clear(&dev->port);
		free(dev);
		return diag_iseterr(rv);
	}

	/* finish filling the dumbopts cfgi */
	dev->dumbopts.shortname = DUMBOPTS_SN;
	dev->dumbopts.descr = DUMBOPTS_DESC;
	dev->port.next = &dev->dumbopts;
	dev->dumbopts.next = NULL;

	printf("*** Warning ! The DUMBT driver is only for electrical ***\n"
	       "*** testing ! Do NOT use while connected to a vehicle! ***\n"
	       "*** refer to doc/scantool-manual.html ***\n");

	return 0;
}

static void
dt_del(struct diag_l0_device *dl0d) {
	struct dt_device *dev;

	assert(dl0d);

	dev = dl0d->l0_int;
	if (!dev) {
		return;
	}

	diag_cfg_clear(&dev->port);
	diag_cfg_clear(&dev->dumbopts);
	free(dev);
	return;
}

static struct cfgi *
dt_getcfg(struct diag_l0_device *dl0d) {
	struct dt_device *dev;
	if (dl0d == NULL) {
		return diag_pseterr(DIAG_ERR_BADCFG);
	}

	dev = dl0d->l0_int;
	return &dev->port;
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static int
dt_open(struct diag_l0_device *dl0d, int testnum) {
	struct dt_device *dev;
	struct diag_serial_settings pset;
	int dumbopts;

	assert(dl0d);
	dev = dl0d->l0_int;

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open port %s test # %d\n", FL, dev->port.val.str,
			testnum);
	}

	dt_init(); // make sure it is initted

	/* try to open TTY */
	dev->tty_int = diag_tty_open(dev->port.val.str);
	if (dev->tty_int == NULL) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	dev->protocol = DIAG_L1_RAW; // cheat !

	switch (testnum) {
	case 10:
		pset.speed = 15000;
		break;
	case 14:
		pset.speed = 360;
		break;
	default:
		pset.speed = 10400;
	}

	pset.databits = diag_databits_8;
	pset.stopbits = diag_stopbits_1;
	pset.parflag = diag_par_n;

	if (diag_tty_setup(dev->tty_int, &pset)) {
		diag_tty_close(dev->tty_int);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* parse dumbopts to set flags */
	dumbopts = dev->dumbopts.val.i;

	dev->use_L = dumbopts & USE_LLINE;
	dev->clr_dtr = dumbopts & CLEAR_DTR;
	dev->set_rts = dumbopts & SET_RTS;
	dev->man_break = dumbopts & MAN_BREAK;
	dev->lline_inv = dumbopts & LLINE_INV;
	dev->fast_break = dumbopts & FAST_BREAK;
	dev->blockduplex = dumbopts & BLOCKDUPLEX;

	// set initial DTR and RTS lines before starting tests;
	if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts) < 0) {
		diag_tty_close(dev->tty_int);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	(void)diag_tty_iflush(dev->tty_int); /* Flush unread input */

	// printf("Press <enter> to stop the test.\n");
	// Currently these run for a fixed time...
	switch (testnum) {
	case 1:
		dtest_1(dl0d);
		break;
	case 2:
		dtest_2(dl0d);
		break;
	case 3:
		dtest_3(dl0d);
		break;
	case 4:
		dtest_4(dl0d);
		break;
	case 5:
		dtest_5(dl0d);
		break;
	case 6:
		dtest_6(dl0d);
		break;
	case 7:
		dtest_7(dl0d);
		break;
	case 8:
		dtest_8(dl0d);
		break;
	case 9:
		dtest_9(dl0d);
		break;
	case 10:
		dtest_2(dl0d); // same test, different speed
		break;
	case 11:
		dtest_11(dl0d);
		break;
	case 12:
		dtest_12(dl0d);
		break;
	case 13:
		dtest_13(dl0d);
		break;
	case 14:
		dtest_7(dl0d); // same test, different speed
		break;
	default:
		break;
	}

	diag_tty_close(dev->tty_int);

	fprintf(stderr, "L0 test finished. Ignore the following error.\n");
	return DIAG_ERR_GENERAL;
}

static void
dt_close(UNUSED(struct diag_l0_device *dl0d)) {
	return;
}

/*
 * Send a load of data
 * this is "blocking", i.e. returns only when it's finished or it failed.
 *
 * Returns 0 on success, -1 on failure
 */

static int
dt_send(struct diag_l0_device *dl0d, UNUSED(const char *subinterface), const void *data,
	size_t len) {
	struct dt_device *dev = dl0d->l0_int;

	/*
	 * This will be called byte at a time unless P4 timing parameter is zero
	 * as the L1 code that called this will be adding the P4 gap between
	 * bytes
	 */
	if (len <= 0) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "dt_send dl0d=%p , len=%ld. ", FL, (void *)dl0d,
			(long)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			diag_data_dump(stderr, data, len);
		}
		fprintf(stderr, "\n");
	}

	if (diag_tty_write(dev->tty_int, data, len) != (int)len) {
		fprintf(stderr, FLFMT "dt_send: write error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if ((diag_l0_debug & (DIAG_DEBUG_WRITE | DIAG_DEBUG_DATA)) ==
	    (DIAG_DEBUG_WRITE | DIAG_DEBUG_DATA)) {
		fprintf(stderr, "\n");
	}

	return 0;
}

/* dummy dt_recv
 */

static int
dt_recv(struct diag_l0_device *dl0d, UNUSED(const char *subinterface), UNUSED(void *data),
	size_t len, unsigned int timeout) {
	fprintf(stderr, FLFMT "link %p recv upto %ld bytes timeout %u; doing nothing.\n",
		FL, (void *)dl0d, (long)len, timeout);

	return diag_iseterr(DIAG_ERR_TIMEOUT);
}

/*
 * Set speed/parity etc
 */
static int
dt_setspeed(struct diag_l0_device *dl0d, const struct diag_serial_settings *pset) {
	struct dt_device *dev;

	dev = (struct dt_device *)(dl0d->l0_int);

	dev->serial = *pset;

	return diag_tty_setup(dev->tty_int, &dev->serial);
}

static uint32_t
dt_getflags(UNUSED(struct diag_l0_device *dl0d)) {
	return DIAG_L1_HALFDUPLEX;
}

static int
dt_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	int rv = 0;

	switch (cmd) {
	case DIAG_IOCTL_IFLUSH:
		// do nothing
		rv = 0;
		break;
	case DIAG_IOCTL_SETSPEED:
		rv = dt_setspeed(dl0d, (const struct diag_serial_settings *)data);
		break;
	default:
		rv = DIAG_ERR_IOCTL_NOTSUPP;
		break;
	}

	return rv;
}

const struct diag_l0 diag_l0_dumbtest = {"Dumb interface test suite",
					 "DUMBT",
					 -1, // support "all" L1 protos...
					 dt_init,
					 dt_new,
					 dt_getcfg,
					 dt_del,
					 dt_open,
					 dt_close,
					 dt_getflags,
					 dt_recv,
					 dt_send,
					 dt_ioctl};
