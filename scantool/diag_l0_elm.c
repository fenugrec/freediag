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
 * Diag, Layer 0, interface for Scantool.net's ELM323 Interface 
 * Ken Bantoft <ken@bantoft.org>
 *
 * This is currently just cut/copy/pasted from the sileng driver, and
 * probably won't work yet.
 *
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
 * Scantool.net's ELM 323 chipset based drive driver
 * under POSIX-like systems connected to a serial port.
 *
 * This device actually works does all of the work for you - you
 * just treat it like a serial device and read/write as required.
 *
 */

struct diag_l0_elm_device
{
	int protocol;
	struct diag_serial_settings serial;
};

/* Global init flag */
static int diag_l0_elm_initdone;

extern const struct diag_l0 diag_l0_elm;

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc 
 */
static int
diag_l0_elm_init(void)
{

	fprintf(stderr,"diag_l0_elm_init() called ");

	if (diag_l0_elm_initdone)
		return (0);
	diag_l0_elm_initdone = 1;

	/* Do required scheduling tweeks */
	diag_os_sched();

	return (0);
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_elm_open(const char *subinterface, int iProtocol)
{
	struct diag_l0_device *dl0d;
	struct diag_l0_elm_device *dev;

	/* If we're doing debugging, print to stderr */
/*	if (diag_l0_debug & DIAG_DEBUG_OPEN)
	{
*/		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
/*	}
*/
	diag_l0_elm_init();

	if (diag_calloc(&dev, 1))
		return (0);

	dev->protocol = iProtocol;

	if (diag_tty_open(&dl0d, subinterface, &diag_l0_elm, (void *)dev) < 0)
		return(0);

	if (diag_tty_control(dl0d, 1, 0) < 0) {
		diag_tty_close(&dl0d);
		return 0;
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */

	return (dl0d) ;
}

static int
diag_l0_elm_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_elm_device *dev = 
			(struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

		/* If debugging, print to strerr */
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
diag_l0_elm_fastinit(struct diag_l0_device *dl0d,
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
diag_l0_elm_slowinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in,
struct diag_l0_elm_device *dev)
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
/*
	set.speed = 5;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

*/

        /* Send 25 ms break as initialisation pattern (TiniL) */
        diag_tty_break(dl0d, 25); 

        /* Now let the caller send a startCommunications message */
        return (0);

}

/*
 * Do wakeup on the bus 
 */
static int
diag_l0_elm_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct diag_l0_elm_device *dev;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

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
		rv = diag_l0_elm_fastinit(dl0d, in);
		break;

	case DIAG_L1_INITBUS_5BAUD:
		rv = diag_l0_elm_slowinit(dl0d, in, dev);
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
diag_l0_elm_send(struct diag_l0_device *dl0d,
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
diag_l0_elm_recv(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
void *data, size_t len, int timeout)
{
	int xferd;

	struct diag_l0_elm_device *dev;
	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

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
diag_l0_elm_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pss)
{
	struct diag_l0_elm_device *dev;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = *pss;

	return diag_tty_setup(dl0d, pss);
}

static int
diag_l0_elm_getflags(struct diag_l0_device *dl0d)
{

        struct diag_l0_elm_device *dev;
        int flags;

        dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

        flags = 0;
        switch (dev->protocol)
        {
        case DIAG_L1_J1850_VPW:
        case DIAG_L1_J1850_PWM:
                        flags = DIAG_L1_DOESL2FRAME;
                        break;
        case DIAG_L1_ISO9141:
                        flags = DIAG_L1_SLOW ;
                        flags |= DIAG_L1_DOESP4WAIT;
                        break;
        case DIAG_L1_ISO14230:
                        flags = DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
                        flags |= DIAG_L1_DOESP4WAIT;
                        break;
        }

        if (diag_l0_debug & DIAG_DEBUG_PROTO)
                fprintf(stderr,
                        FLFMT "getflags link %p proto %d flags 0x%x\n",
                        FL, dl0d, dev->protocol, flags);

        return( flags );

}

const struct diag_l0 diag_l0_elm = {
	"Scantool.net ELM323 Chipset Device", 
	"ELM",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	diag_l0_elm_init,
	diag_l0_elm_open,
	diag_l0_elm_close,
	diag_l0_elm_initbus,
	diag_l0_elm_send,
	diag_l0_elm_recv,
	diag_l0_elm_setspeed,
	diag_l0_elm_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_elm_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_elm_add(void) {
	return diag_l1_add_l0dev(&diag_l0_elm);
}
