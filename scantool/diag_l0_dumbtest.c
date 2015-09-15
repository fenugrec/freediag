/*
 *	freediag - Vehicle Diagnostic Utility

 *************************************************************************
 * dumbtest (c) 2014-2015 fenugrec
 * Diag, Layer 0, interface tester. This should *NOT* be used while
 * a vehicle is connected; only for debugging the electrical interface itself.

 * The dumb_flags variable is set according to particular type (VAGtool, SE)
 * to enable certain features (L line on RTS, etc)

 * This is a dummy l0 driver : most functions do nothing except
 * _open which opens the subinterface, runs the specified test, and closes everything
 * before returning.
 */


#include <errno.h>
#include <stdlib.h>
#include <string.h>	//for memcmp

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

struct diag_l0_dt_device
{
	int protocol;	//set in diag_l0_dt_open with specified iProtocol
	struct diag_serial_settings serial;
};


// global flags set according to particular interface type (VAGtool vs SE etc.)
static unsigned int dumb_flags=0;
#define USE_LLINE	0x01		//interface maps L line to RTS : setting RTS normally pulls L down to 0 .
#define CLEAR_DTR 0x02		//have DTR cleared constantly (unusual, disabled by default)
#define SET_RTS 0x04			//have RTS set constantly (also unusual, disabled by default).
#define MAN_BREAK 0x08		//force bitbanged breaks for inits; enabled by default
#define LLINE_INV 0x10		//invert polarity of the L line if set. see doc/dumb_interfaces.txt
#define FAST_BREAK 0x20		//do we use diag_tty_fastbreak for iso14230-style fast init.


extern const struct diag_l0 diag_l0_dumbtest;

static int
diag_l0_dt_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_dt_init(void)
{
	/* Global init flag */
	static int diag_l0_dt_initdone=0;

	if (diag_l0_dt_initdone)
		return 0;


	/* Do required scheduling tweeks */
	diag_os_sched();
	diag_l0_dt_initdone = 1;

	return 0;
}




//dtest_1 : slow pulse TXD with diag_tty_break; 400ms / 200ms cycles.

static void dtest_1(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 1: pulsing TXD=1, 1s, TXD=0, 500ms\n", FL);
	for (i=0; i<=4; i++) {
		diag_os_millisleep(1000);
		diag_tty_break(dl0d, 500);
	}

	return;
}

//dtest_2 : fast pulse TXD by sending 0x55 @ 10.4kps with 5ms interbyte
static void dtest_2(struct diag_l0_device *dl0d) {
	int i;
	uint8_t patternbyte=0x55;
	fprintf(stderr, FLFMT "Starting test 2: sending 0x55 with P4=5ms\n", FL);
	for (i=0; i<=300; i++) {
		diag_tty_write(dl0d, &patternbyte, 1);
		diag_os_millisleep(5);
	}

	return;
}

//dtest_3: slow pulse RTS
static void dtest_3(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 3: pulsing RTS=1, 1s, RTS=0, 500ms\n", FL);

	for (i=0; i<=4; i++) {
		diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), 1);
		diag_os_millisleep(1000);
		diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), 0);
		diag_os_millisleep(500);
	}
	diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS));

	return;
}

//dtest_4: slow pulse DTR
static void dtest_4(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 4: pulsing DTR=1, 1s, DTR=0, 500ms\n", FL);

	for (i=0; i<=4; i++) {
		diag_tty_control(dl0d, 1, (dumb_flags & SET_RTS));
		diag_os_millisleep(1000);
		diag_tty_control(dl0d, 0, (dumb_flags & SET_RTS));
		diag_os_millisleep(500);
	}
	diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS));

	return;
}


//dtest_5 : fast pulse TXD with diag_tty_break;

static void dtest_5(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 5: pulsing TXD=1, 50, TXD=0, 25ms\n", FL);
	for (i=0; i<=40; i++) {
		diag_os_millisleep(50);
		diag_tty_break(dl0d, 25);
	}

	return;
}

//dtest_6 : fast pulse TXD with diag_tty_fastbreak;

static void dtest_6(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 6: pulsing TXD=1, 50ms, TXD=0, 25ms\n", FL);
	for (i=0; i<=50; i++) {
		diag_os_millisleep(25);
		diag_tty_fastbreak(dl0d, 50);
	}

	return;
}

//dtest_7 : half duplex echo removal 1 : send bytes and remove echo
//one by one; P4=0. Print per-byte time to do this; use _dumb_send() instead
// of diag_tty directly, like l1_send().
static void dtest_7(struct diag_l0_device *dl0d) {
	uint8_t i, echo;
	int rv, badechos=0;
	unsigned long ti, tf;
#define DT7_ITERS 100
	printf("Starting test 7: half duplex single echo removal...\n");

	ti=diag_os_getms();	//get starting time.
	for (i=0; i<=DT7_ITERS; i++) {
		echo=i-1;	//init to bad value
		rv=diag_l0_dt_send(dl0d, NULL, &i, 1);
		if (rv==0) {
			while ( (rv = diag_tty_read(dl0d, &echo, 1, 100)) < 0) {
				if (errno != EINTR)
					break;
				rv = 0; /* Interrupted read, nothing transferred. */
			}
			//check echo
			if (echo != i)
				badechos++;
			else
				rv=0;
		} else {
			break;	//dumb_send failed, this is bad
		}
	}	//for
	tf=diag_os_getms();	//stop time
	tf = (tf-ti)/DT7_ITERS;	//average time per byte
	printf("Average speed : %lu ms/byte. %d bad echos received.\n", tf, badechos);


	return;
}	//dtest_7

//dtest_8 : block half duplex removal; send 10 bytes per message
static void dtest_8(struct diag_l0_device *dl0d) {
#define DT8_MSIZE 10
	uint8_t tx[DT8_MSIZE], echo[DT8_MSIZE];
	int i, rv, badechos=0;
	unsigned long ti, tf;
#define DT8_ITERS 10
	printf("Starting test 8: half duplex block echo removal...\n");
	//fill i[] first
	for (i=0; i<DT8_MSIZE; i++)
		tx[i]=(uint8_t) i;


	ti=diag_os_getms();	//get starting time.
	for (i=0; i<=DT8_ITERS; i++) {

		rv=diag_l0_dt_send(dl0d, NULL, tx, DT8_MSIZE);
		if (rv==0) {
			while ( (rv = diag_tty_read(dl0d, echo, DT8_MSIZE, 100)) < 0) {
				if (errno != EINTR)
					break;
				rv = 0; /* Interrupted read, nothing transferred. */
			}
			//check echo
			if ( (rv>0) && ( memcmp(tx, echo, DT8_MSIZE) == 0))
				rv=0;	//all's well
			else
				badechos++;

		} else {
			break;	//dumb_send failed, this is bad
		}
	}	//for
	if (rv != 0) {
		printf("Error, test did not complete.\n");
	} else {
		tf=diag_os_getms();	//stop time
		tf = (tf-ti)/(DT8_ITERS * DT8_MSIZE);	//average time per byte
		printf("Average speed : %lu ms/byte. %d bad echos received.\n", tf, badechos);
	}

	return;
}	//dtest_8

//dtest_9 : test accuracy of read timeouts.
static void dtest_9(struct diag_l0_device *dl0d) {
	#define DT9_ITERS	4
	int i;
	int iters;
	uint8_t garbage[MAXRBUF];
	unsigned long t0, tf;
	printf("Starting test 9: checking accuracy of read timeouts.\n");
	diag_tty_iflush(dl0d);	//purge before starting

	for (i=10; i<=200; i += 20) {
		printf("Timeout=%d: ", i);
		t0=diag_os_getms();
		for (iters=0; iters < DT9_ITERS; iters++) {
			diag_tty_read(dl0d, garbage, MAXRBUF, i);
		}
		tf = (diag_os_getms() - t0)/(DT9_ITERS);	//average measured timeout
		printf("avg=%lums\n", tf);
	}

	return;
}	//dtest_9

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_dt_open(const char *subinterface, int testnum)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_dt_device *dev;
	struct diag_serial_settings pset;

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s test #%d\n",
			FL, subinterface, testnum);
	}

	diag_l0_dt_init();	 //make sure it is initted

	if ((rv=diag_calloc(&dev, 1)))
		return diag_pseterr(DIAG_ERR_NOMEM);

	dev->protocol = DIAG_L1_RAW;	//cheat !
	if ((rv=diag_tty_open(&dl0d, subinterface, &diag_l0_dumbtest, (void *)dev))) {
		return diag_pseterr(rv);
	}

	pset.speed=10400;
	pset.databits = diag_databits_8;
	pset.stopbits = diag_stopbits_1;
	pset.parflag = diag_par_n;

	if (diag_tty_setup(dl0d, &pset)) {
		diag_tty_close(&dl0d);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	//set initial DTR and RTS lines before starting tests;
	if (diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS)) < 0) {
		diag_tty_close(&dl0d);
		return diag_pseterr(DIAG_ERR_GENERAL);
	}

	(void)diag_tty_iflush(dl0d);	/* Flush unread input */

	//printf("Press <enter> to stop the test.\n");
	//Currently these run for a fixed time...
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
	default:
		break;
	}

	diag_tty_close(&dl0d);
	free(dev);
	fprintf(stderr, FLFMT "L0 test finished. Ignore the following error.\n", FL);
	return NULL;
}


//this should never be called : diag_l0_dt_open never returns a diag_l0_device !
//returning !
static int
diag_l0_dt_close(UNUSED(struct diag_l0_device **pdl0d))
{
	fprintf(stderr, FLFMT "**** we're in diag_l0_dt_close()... how did this happen?\n", FL);

	return 0;
}



/*
 * Do nothing
 * return 0 on success,
 */
static int
diag_l0_dt_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{

	struct diag_l0_dt_device *dev;

	dev = (struct diag_l0_dt_device *)(dl0d->l0_int);

	fprintf(stderr, FLFMT "device link %p info %p initbus type %d, doing nothing.\n",
			FL, (void *)dl0d, (void *)dev, in->type);

	return 0;

}



/*
 * Send a load of data
 * this is "blocking", i.e. returns only when it's finished or it failed.
 *
 * Returns 0 on success, -1 on failure
 */

static int
diag_l0_dt_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len)
{
	/*
	 * This will be called byte at a time unless P4 timing parameter is zero
	 * as the L1 code that called this will be adding the P4 gap between
	 * bytes
	 */
	ssize_t xferd;
	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "device link %p send %ld bytes ",
			FL, (void *)dl0d, (long)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA)
			diag_data_dump(stderr, data, len);
		fprintf(stderr, "\n");
	}

	while ((size_t)(xferd = diag_tty_write(dl0d, data, len)) != len) {
		/* Partial write */
		if (xferd < 0) {
			/* error */
			if (errno != EINTR) {
				perror("write");
				fprintf(stderr, FLFMT "write returned error %d !!\n", FL, errno);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			xferd = 0; /* Interrupted read, nothing transferred. */
		}
		/*
		 * Successfully wrote xferd bytes (or 0 && EINTR),
		 * so inc pointers and continue
		 */
		len -= (size_t) xferd;
		data = (const void *)((const uint8_t *)data + xferd);
	}
	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, "\n");
	}

	return 0;
}

/* dummy diag_l0_dt_recv
 */

static int
diag_l0_dt_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
UNUSED(void *data), size_t len, int timeout)
{
	fprintf(stderr,
		FLFMT "link %p recv upto %ld bytes timeout %d; doing nothing.\n",
		FL, (void *)dl0d, (long)len, timeout);

	return diag_iseterr(DIAG_ERR_TIMEOUT);
}

/*
 * Set speed/parity etc
 */
static int
diag_l0_dt_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset)
{
	struct diag_l0_dt_device *dev;

	dev = (struct diag_l0_dt_device *)(dl0d->l0_int);

	dev->serial = *pset;

	return diag_tty_setup(dl0d, &dev->serial);
}

// Update interface options to customize particular interface type (K-line only or K&L)
// Not related to the "getflags" function which returns the interface capabilities.
void diag_l0_dt_setopts(unsigned int newflags)
{
	dumb_flags=newflags;
}
unsigned int diag_l0_dt_getopts(void) {
	return dumb_flags;
}


static uint32_t
diag_l0_dt_getflags(UNUSED(struct diag_l0_device *dl0d))
{
	return DIAG_L1_HALFDUPLEX;
}

const struct diag_l0 diag_l0_dumbtest = {
 	"Dumb interface test suite",
	"DUMBT",
	-1,		//support "all" L1 protos...
	NULL,
	NULL,
	NULL,
	diag_l0_dt_init,
	diag_l0_dt_open,
	diag_l0_dt_close,
	diag_l0_dt_initbus,
	diag_l0_dt_send,
	diag_l0_dt_recv,
	diag_l0_dt_setspeed,
	diag_l0_dt_getflags
};
