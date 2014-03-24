/*
 *	freediag - Vehicle Diagnostic Utility

 *************************************************************************
 * dumbtest
 * Diag, Layer 0, interface tester. This is *NOT* meant to be used while
 * a vehicle is connected; only for debugging the electrical interface itself.

* The dumb_flags variable is set according to particular type (VAGtool, SE)
 * to enable certain features (L line on RTS, etc)

 * This is a non-standard l0 driver : most functions do nothing except
 * _open which opens the subinterface, runs tests, and closes everything
 * before returning.
 */


#include <errno.h>
#include <stdlib.h>

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


static const struct diag_l0 diag_l0_dt;

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
	fprintf(stderr, FLFMT "Starting test 1: pulsing TXD=1, 400ms, TXD=0, 200ms\n", FL);
	//this should run indefinitely until interrupted... not done yet.
	for (i=0; i<=4; i++) {
		diag_os_millisleep(400);
		diag_tty_break(dl0d, 200);
	}

	return;
}

//dtest_2 : fast pulse TXD by sending 0x55 @ 10.4kps
static void dtest_2(UNUSED(struct diag_l0_device *dl0d)) {
	fprintf(stderr, FLFMT "Test 2 not implemented !\n", FL);
	//TODO : write test #2. We'll need to call diag_tty_setup first
	return;
}

//dtest_3: slow pulse RTS : set for 400ms, clear for 200ms
static void dtest_3(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 3: pulsing RTS=1, 400ms, RTS=0, 200ms\n", FL);

	for (i=0; i<=4; i++) {
		diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), 1);
		diag_os_millisleep(400);
		diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), 0);
		diag_os_millisleep(200);
	}
	diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS));

	return;
}

//dtest_4: slow pulse DTR : set for 400ms, clear for 200ms
static void dtest_4(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 4: pulsing DTR=1, 400ms, DTR=0, 200ms\n", FL);

	for (i=0; i<=4; i++) {
		diag_tty_control(dl0d, 1, (dumb_flags & SET_RTS));
		diag_os_millisleep(400);
		diag_tty_control(dl0d, 0, (dumb_flags & SET_RTS));
		diag_os_millisleep(200);
	}
	diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS));

	return;
}


//dtest_5 : fast pulse TXD with diag_tty_break; 100ms / 25ms cycles.

static void dtest_5(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 5: pulsing TXD=1, 100ms, TXD=0, 25ms\n", FL);
	for (i=0; i<=40; i++) {
		diag_os_millisleep(100);
		diag_tty_break(dl0d, 25);
	}

	return;
}

//dtest_6 : fast pulse TXD with diag_tty_fastbreak; 75ms / 25ms cycles.

static void dtest_6(struct diag_l0_device *dl0d) {
	int i;
	fprintf(stderr, FLFMT "Starting test 6: pulsing TXD=1, 75ms, TXD=0, 25ms\n", FL);
	for (i=0; i<=50; i++) {
		diag_os_millisleep(50);
		diag_tty_fastbreak(dl0d, 50);
	}

	return;
}

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

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s test #%d\n",
			FL, subinterface, testnum);
	}

	diag_l0_dt_init();	 //make sure it is initted

	if ((rv=diag_calloc(&dev, 1)))
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_NOMEM);

	dev->protocol = DIAG_L1_RAW;	//cheat !
	if ((rv=diag_tty_open(&dl0d, subinterface, &diag_l0_dt, (void *)dev))) {
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	//set initial DTR and RTS lines before starting tests;
	if (diag_tty_control(dl0d, !(dumb_flags & CLEAR_DTR), (dumb_flags & SET_RTS)) < 0) {
		diag_tty_close(&dl0d);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
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
	default:
		break;
	}

	diag_tty_close(&dl0d);
	free(dev);
	fprintf(stderr, FLFMT "L0 test finished. Ignore the following error.\n", FL);
	return NULL;
}

//diag_l0_dt_close : free the specified diag_l0_device and close TTY handle.
//this shouldn't be needed : diag_l0_dt_open closes diag_l0_device before
//returning !
static int
diag_l0_dt_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_dt_device *dev =
			(struct diag_l0_dt_device *)diag_l0_dl0_handle(dl0d);

		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "l0 link %p closing\n",
				FL, (void *)dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

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

	dev = (struct diag_l0_dt_device *)diag_l0_dl0_handle(dl0d);

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

	dev = (struct diag_l0_dt_device *)diag_l0_dl0_handle(dl0d);

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


static int
diag_l0_dt_getflags(UNUSED(struct diag_l0_device *dl0d))
{
	return DIAG_L1_HALFDUPLEX;
}

static const struct diag_l0 diag_l0_dt = {
 	"Dumb interface test suite",
	"DUMBT",
	-1,		//support "all" L1 protos...
	diag_l0_dt_init,
	diag_l0_dt_open,
	diag_l0_dt_close,
	diag_l0_dt_initbus,
	diag_l0_dt_send,
	diag_l0_dt_recv,
	diag_l0_dt_setspeed,
	diag_l0_dt_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_dt_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_dumbt_add(void) {
	return diag_l1_add_l0dev(&diag_l0_dt);
}
