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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * Diag, Layer 0, interface for Silicon Engines generic ISO 9141 interface
 *
 * We purposely haven't defined an structure that is used externally by this
 * interface, just a file descriptor because it's not so easy to do for
 * different devices, and potentially different operating systems.
 *
 *
 * This code is written to handle interruptible system calls (which happens
 * on SYSV)
 *
 * This driver is *very* similar to the VAGtool interface driver, all that
 * is different is that the VAGtool device sets RTS low for normal operation
 * and the VAGtool then twiddles RTS for doing 5 baud L line initialisation
 * - this code has a couple of extra bits of checking around that startup
 * code and therefore I've kept it, but it's probably worth discarding this
 * driver at some point and using the VAGtool code for all
 */


#include <unistd.h>

#include <errno.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

CVSID("$Id$");


/*
 * Silicon Engines ISO-9141 'K' Line interface
 * under POSIX-like systems connected to a serial port.
 *
 * We don't use any of the 5 baud init features of the device,
 * just ignore RTS/CTS and then transmit/receive as normal. The device
 * is half duplex so we get an echo (ISO9141 is half duplex)
 *
 * I'd imagine many other K line interfaces will work with this code
 */

struct diag_l0_sileng_device
{
	int protocol;
	struct diag_serial_settings serial;
};

/* Global init flag */
static int diag_l0_sileng_initdone;

extern const struct diag_l0 diag_l0_sileng;

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc 
 */
static int
diag_l0_sileng_init(void)
{
	if (diag_l0_sileng_initdone)
		return (0);
	diag_l0_sileng_initdone = 1;

	/* Do required scheduling tweeks */
	diag_os_sched();

	return (0);
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_sileng_open(const char *subinterface, int iProtocol)
{
	struct diag_l0_device *dl0d;
	struct diag_l0_sileng_device *dev;

	if (diag_l0_debug & DIAG_DEBUG_OPEN)
	{
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_sileng_init();

	if (diag_calloc(&dev, 1))
		return (0);

	dev->protocol = iProtocol;

	if (diag_tty_open(&dl0d, subinterface, &diag_l0_sileng, (void *)dev) < 0)
		return(0);

	/*
	 * We need to ensure that DTR is high, or interface thinks it's in
	 * special 5 baud mode.
	 *
	 * We set RTS to low, because this allows some interfaces to work
	 * than need power from the DTR/RTS lines
	 */
	if (diag_tty_control(dl0d, 1, 0) < 0) {
		diag_tty_close(&dl0d);
		return 0;
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */

	return (dl0d) ;
}

static int
diag_l0_sileng_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_sileng_device *dev = 
			(struct diag_l0_sileng_device *)diag_l0_dl0_handle(dl0d);

		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n",
				FL, dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

	return (0);
}

/*
 * Fastinit:
 */
static int
diag_l0_sileng_fastinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in __attribute__((unused)))
{
	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p fastinit\n",
			FL, dl0d);

	/* Send 25 ms break as initialisation pattern (TiniL) */
	diag_tty_break(dl0d, 25);

	/* Now let the caller send a startCommunications message */
	return (0);
}

/*
 * Slowinit:
 *	We need to send a byte (the address) at 5 baud, then
 *	switch back to 10400 baud
 *	and then wait 25ms. We must have waited Tidle (300ms) first
 *
 */
static int
diag_l0_sileng_slowinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in,
struct diag_l0_sileng_device *dev)
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
			return (diag_iseterr(DIAG_ERR_TIMEOUT));
		}
		if (xferd == 0)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return(-1);
		}
		if (errno != EINTR)
		{
			/* Error, EOF */
			perror("read");
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return(-1);
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
	if (rv < 0)
	{
		if (diag_l0_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "slowinit link %p read timeout\n",
				FL, dl0d);
		return(rv);
	}
	return (0);
}

/*
 * Do wakeup on the bus 
 */
static int
diag_l0_sileng_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct diag_l0_sileng_device *dev;

	dev = (struct diag_l0_sileng_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d\n",
			FL, dl0d, dev, in->type);

	if (!dev)
		return(-1);

	
	/* Wait the idle time (Tidle > 300ms) */
	diag_tty_iflush(dl0d);	/* Flush unread input */
	diag_os_millisleep(300);

	switch (in->type)
	{
	case DIAG_L1_INITBUS_FAST:
		rv = diag_l0_sileng_fastinit(dl0d, in);
		break;

	case DIAG_L1_INITBUS_5BAUD:
		rv = diag_l0_sileng_slowinit(dl0d, in, dev);
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
	
	return(rv);

}



/*
 * Send a load of data
 *
 * Returns 0 on success, -1 on failure
 */
static int
diag_l0_sileng_send(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
const void *data, size_t len)
{
	/*
	 * This will be called byte at a time unless P4 timing parameter is zero
	 * as the L1 code that called this will be adding the P4 gap between
	 * bytes
	 */
	ssize_t xferd;

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, FLFMT "device link %p send %d bytes ",
			FL, dl0d, (int)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA)
		{
			diag_data_dump(stderr, data, len);
		}
	}

	while ((size_t)(xferd = diag_tty_write(dl0d, data, len)) != len)
	{
		/* Partial write */
		if (xferd <  0)
		{
			/* error */
			if (errno != EINTR)
			{
				perror("write");
				fprintf(stderr, FLFMT "write returned error %d !!\n", FL, errno);
				return(-1);
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

	return(0);
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 */
static int
diag_l0_sileng_recv(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
void *data, size_t len, int timeout)
{
	int xferd;

	struct diag_l0_sileng_device *dev;
	dev = (struct diag_l0_sileng_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %d bytes timeout %d",
			FL, dl0d, (int)len, timeout);

	while ( (xferd = diag_tty_read(dl0d, data, len, timeout)) <= 0)
	{
		if (xferd == DIAG_ERR_TIMEOUT)
		{
			if (diag_l0_debug & DIAG_DEBUG_READ)
				fprintf(stderr, "\n");
			return ((DIAG_ERR_TIMEOUT));
		}
		if (xferd == 0)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return(-1);
		}
		if (errno != EINTR)
		{
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return(-1);
		}
	}
	if (diag_l0_debug & DIAG_DEBUG_READ)
	{
		diag_data_dump(stderr, data, (size_t)xferd);
		fprintf(stderr, "\n");
	}
	return(xferd);
}

/*
 * Set speed/parity etc
 */
static int
diag_l0_sileng_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pss)
{
	struct diag_l0_sileng_device *dev;

	dev = (struct diag_l0_sileng_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = *pss;

	return diag_tty_setup(dl0d, pss);
}

static int
diag_l0_sileng_getflags(struct diag_l0_device *dl0d __attribute__((unused)))
{
	/* All interface types here use same flags */
	return(
		DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST |
			DIAG_L1_HALFDUPLEX
		);
}

const struct diag_l0 diag_l0_sileng = {
	"Silicon Engines 9141 Converter", 
	"SE9141",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	diag_l0_sileng_init,
	diag_l0_sileng_open,
	diag_l0_sileng_close,
	diag_l0_sileng_initbus,
	diag_l0_sileng_send,
	diag_l0_sileng_recv,
	diag_l0_sileng_setspeed,
	diag_l0_sileng_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_sileng_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_sileng_add(void) {
	return diag_l1_add_l0dev(&diag_l0_sileng);
}
