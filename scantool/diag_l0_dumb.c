/*
 *	freediag - Vehicle Diagnostic Utility
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * Diag, Layer 0, interface for VAGTool, Silicon Engines generic ISO9141
 * and other compatible "dumb" interfaces, such as Jeff Noxon's opendiag interface.
 * Any RS232 interface with no microcontroller onboard should fall in this category.
 * This is an attempt at merging diag_l0_se and diag_l0_vw .
 * WIN32 will not work for a while, if ever.
 *
* The dumb_flags variable is set according to particular type (VAGtool, SE)
 * to enable certain features (L line on RTS, etc)
 * Work in progress !
 */


#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <errno.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

struct diag_l0_dumb_device
{
	int protocol;
	struct diag_serial_settings serial;
};

/* Global init flag */
static int diag_l0_dumb_initdone;

// flags set according to particular interface type (VAGtool vs SE etc.)
static int dumb_flags;
#define DUMB_RTS_L	0x01		//interface has maps L line to RTS

extern const struct diag_l0 diag_l0_dumb;

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_dumb_init(void)
{
	if (diag_l0_dumb_initdone)
		return 0;
	diag_l0_dumb_initdone = 1;

	/* Do required scheduling tweeks */
	diag_os_sched();

	return 0;
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_dumb_open(const char *subinterface, int iProtocol)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_dumb_device *dev;

	if (diag_l0_debug & DIAG_DEBUG_OPEN)
	{
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_dumb_init();

	if ((rv=diag_calloc(&dev, 1)))
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_NOMEM);

	dev->protocol = iProtocol;
	if ((rv=diag_tty_open(&dl0d, subinterface, &diag_l0_dumb, (void *)dev)))
	{
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	/*
	 * We set RTS to low, and DTR high, because this allows some
	 * interfaces to work than need power from the DTR/RTS lines;
	 */
	if (diag_tty_control(dl0d, 1, 0) < 0) {
		diag_tty_close(&dl0d);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
	}

	(void)diag_tty_iflush(dl0d);	/* Flush unread input */

	return dl0d;
}

static int
diag_l0_dumb_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_dumb_device *dev =
			(struct diag_l0_dumb_device *)diag_l0_dl0_handle(dl0d);

		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n",
				FL, dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

	return 0;
}

/*
 * Fastinit: XXX missing initbus_fastinit_args ?
 */
static int
diag_l0_dumb_fastinit(struct diag_l0_device *dl0d)
{
	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p fastinit\n",
			FL, dl0d);

	/* Send 25 ms break as initialisation pattern (TiniL) */
	diag_tty_break(dl0d, 25);

	/* Now let the caller send a startCommunications message */
	return 0;
}


/* Do the 5 BAUD L line stuff while the K line is twiddling */
// XXX dumb_Lline : not sure when this is needed (VAGtool only ?)
#define USDELAY 120
static void
diag_l0_dumb_Lline(struct diag_l0_device *dl0d, struct diag_l0_dumb_device *dev,
	uint8_t ecuaddr)
{
	/*
	 * The bus has been high for w0 ms already, now send the
	 * 8 bit ecuaddr at 5 baud LSB first
	 *
	 * NB, most OS delay implementations, other than for highest priority
	 * tasks on a real-time system, only promise to sleep "at least" what
	 * is requested, and only resume at a scheduling quantum, etc.
	 * Since baudrate must be 5baud +/ 5%, we use the -5% value
	 * and let the system extend as needed
	 */
	int i, rv;
	uint8_t cbuf;

	/* Initial state, DTR High, RTS low */

	/*
	 * Set DTR low during this, receive circuitry
	 * will see a break for that time, that we'll clear out later
	 */
	if (diag_tty_control(dl0d, 0, 1) < 0) {
		fprintf(stderr, FLFMT "open: Failed to set modem control lines\n",
			FL);
	}

	/* Set RTS low, for 200ms (Start bit) */
	if (diag_tty_control(dl0d, 0, 0) < 0) {
		fprintf(stderr, FLFMT "open: Failed to set modem control lines\n",
			FL);
		return;
	}
	diag_os_millisleep(USDELAY);		/* 200ms -5% */

	for (i=0; i<8; i++) {
		if (ecuaddr & (1<<i)) {
			/* High */
			rv = diag_tty_control(dl0d, 0, 1);
		} else {
			/* Low */
			rv = diag_tty_control(dl0d, 0, 0);
		}
		
		if (rv < 0) {
			fprintf(stderr, FLFMT "open: Failed to set modem control lines\n",
				FL);
			return;
		}
		diag_os_millisleep(USDELAY);		    /* 200ms -5% */
	}
	/* And set high for the stop bit */
	if (diag_tty_control(dl0d, 0, 1) < 0) {
		fprintf(stderr, FLFMT "open: Failed to set modem control lines\n",
			FL);
		return;
	}
	diag_os_millisleep(USDELAY);		    /* 200ms -5% */

	/* Now put DTR/RTS back correctly so RX side is enabled */
	if (diag_tty_control(dl0d, 1, 0) < 0) {
		fprintf(stderr, FLFMT "open: Failed to set modem control lines\n",
			FL);
	}

	/* And clear out the break */
	(void) diag_tty_read(dl0d, &cbuf, 1, 20);

	return;
}

/*
 * Slowinit:
 *	We need to send a byte (the address) at 5 baud, then
 *	switch back to 10400 baud
 *	and then wait 25ms. We must have waited Tidle (300ms) first
 *
 * We can use the main chip to do this on the K line but on VAGtool interfaces
 * we also need to do this on the L line which is done by twiddling the RTS
 * line.
 */
static int
diag_l0_dumb_slowinit(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in,
	struct diag_l0_dumb_device *dev)
{
	char cbuf[MAXRBUF];
	int xferd, rv;
	int tout;
	struct diag_serial_settings set;

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
	{
		fprintf(stderr, FLFMT "slowinit link %p address 0x%x\n",
			FL, dl0d, in->addr);
	}

	/* Set to 5 baud, 8 N 1 */
	
	set.speed = 5;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	diag_tty_setup(dl0d, &set);

	/* Wait W0 (2ms or longer) leaving the bus at logic 1 */
	diag_os_millisleep(2);

	/* Send the address as a single byte message */
	diag_tty_write(dl0d, &in->addr, 1);

#if notdef	//XXX SE vs VW
	/* Do the L line stuff */
	diag_l0_dumb_Lline(dl0d, in->addr);
#endif

	/*
	 * And read back the single byte echo, which shows TX completes
	 * - At 5 baud, it takes 2 seconds to send a byte ..
	 */
	
	while ( (xferd = diag_tty_read(dl0d, &cbuf[0], 1, 2750)) <= 0)
	{
		if (xferd == DIAG_ERR_TIMEOUT)
		{
			if (diag_l0_debug & DIAG_DEBUG_PROTO)
				fprintf(stderr, FLFMT "slowinit link %p echo read timeout\n",
					FL, dl0d);
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		}
		if (xferd == 0)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		if (errno != EINTR)
		{
			/* Error, EOF */
			perror("read");
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}

	/*
	 * Ideally we would now measure the length of the received
	 * 0x55 sync character to work out the baud rate.
	 * However, we cant do that yet, so we just set the
	 * baud rate to what the user requested and read the 0x55
	 */
	(void)diag_tty_setup(dl0d, &dev->serial);

	if (dev->protocol == DIAG_L1_ISO9141)
		tout = 750;		/* 2s is too long */
	else
		tout = 300;		/* 300ms according to ISO14230-2 */
	rv = diag_tty_read(dl0d, cbuf, 1, tout);
	if (rv < 0) {
		if (diag_l0_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "slowinit link %p read timeout\n",
				FL, dl0d);
		return diag_iseterr(rv);
	} else {
		if (diag_l0_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "slowinit link %p read 0x%x\n",
				FL, dl0d, cbuf[0]);

	}
	return 0;
}

/*
 * Do wakeup on the bus
 * XXX retval ?
 */
static int
diag_l0_dumb_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct diag_l0_dumb_device *dev;

	dev = (struct diag_l0_dumb_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d\n",
			FL, dl0d, dev, in->type);

	if (!dev)
		return diag_iseterr(DIAG_ERR_GENERAL);

	
	(void)diag_tty_iflush(dl0d);	/* Flush unread input */
	/* Wait the idle time (Tidle > 300ms) */
	diag_os_millisleep(300);

	switch (in->type) {
		case DIAG_L1_INITBUS_FAST:
			rv = diag_l0_dumb_fastinit(dl0d);
			break;

		case DIAG_L1_INITBUS_5BAUD:
			rv = diag_l0_dumb_slowinit(dl0d, in, dev);
			break;

		default:
			rv = DIAG_ERR_INIT_NOTSUPP;
			break;
	}

	/*
	 * return the baud rate etc to what the user had set
	 * because the init routines will have messed them up
	 */
	(void)diag_tty_setup(dl0d, &dev->serial);
	
	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "initbus device link %p returning %d\n",
			FL, dl0d, rv);

	
	return diag_iseterr(rv);

}



/*
 * Send a load of data
 *
 * Returns 0 on success, -1 on failure
 */
#ifdef WIN32
static int
diag_l0_dumb_send(struct diag_l0_device *dl0d,
const char *subinterface,
const void *data, size_t len)
#else
static int
diag_l0_dumb_send(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
const void *data, size_t len)
#endif
{
	/*
	 * This will be called byte at a time unless P4 timing parameter is zero
	 * as the L1 code that called this will be adding the P4 gap between
	 * bytes
	 */
	ssize_t xferd;

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, FLFMT "device link %p send %ld bytes ",
			FL, dl0d, (long)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA)
		{
			diag_data_dump(stderr, data, len);
		}
	}

	while ((size_t)(xferd = diag_tty_write(dl0d, data, len)) != len)
	{
		/* Partial write */
		if (xferd < 0)
		{
			/* error */
			if (errno != EINTR)
			{
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
		len -= xferd;
		data = (const void *)((const char *)data + xferd);
	}
	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) )
	{
		fprintf(stderr, "\n");
	}

	return 0;
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 */
#ifdef WIN32
static int
diag_l0_dumb_recv(struct diag_l0_device *dl0d,
const char *subinterface,
void *data, size_t len, int timeout)
#else
static int
diag_l0_dumb_recv(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
void *data, size_t len, int timeout)
#endif
{
	int xferd;

	errno = EINTR;

	struct diag_l0_dumb_device *dev;
	dev = (struct diag_l0_dumb_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %ld bytes timeout %d\n",
			FL, dl0d, (long)len, timeout);

	while ( (xferd = diag_tty_read(dl0d, data, len, timeout)) <= 0) {
		if (xferd == DIAG_ERR_TIMEOUT)
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		
		if (xferd == 0 && len != 0) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		
		if (errno != EINTR) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}
	if (diag_l0_debug & DIAG_DEBUG_READ) {
		diag_data_dump(stderr, data, (size_t)xferd);
		fprintf(stderr, "\n");
	}
	return xferd;
}

/*
 * Set speed/parity etc
 */
static int
diag_l0_dumb_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset)
{
	struct diag_l0_dumb_device *dev;

	dev = (struct diag_l0_dumb_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = *pset;

	return diag_tty_setup(dl0d, &dev->serial);
}

#ifdef WIN32
static int
diag_l0_dumb_getflags(struct diag_l0_device *dl0d)
#else
static int
diag_l0_dumb_getflags(struct diag_l0_device *dl0d __attribute__((unused)))
#endif
{
	/* All interface types here use same flags */
	return(
		DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST |
			DIAG_L1_HALFDUPLEX
		);
}

const struct diag_l0 diag_l0_dumb = {
 	"Generic dumb serial interface",
	"DUMB",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	diag_l0_dumb_init,
	diag_l0_dumb_open,
	diag_l0_dumb_close,
	diag_l0_dumb_initbus,
	diag_l0_dumb_send,
	diag_l0_dumb_recv,
	diag_l0_dumb_setspeed,
	diag_l0_dumb_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_dumb_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_dumb_add(void) {
	return diag_l1_add_l0dev(&diag_l0_dumb);
}
