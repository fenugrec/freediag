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
 * Diag, Layer 0, Interface for B. Roadman BR-1 Interface
 *
 *	Semi intelligent interface, supports only J1979 properly, and does not
 *	support ISO14230 (KWP2000). In ISO9141-2 mode, only supports the
 *	ISO9141-2 address (0x33h)
 *
  *
 * Thank you to B. Roadman for donation of an interface to the prject
 *
 */

#include <string.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"


extern const struct diag_l0 diag_l0_br;

/*
 * States
 */
enum BR_STATE {BR_STATE_CLOSED, BR_STATE_KWP_SENDKB1,
	BR_STATE_KWP_SENDKB2, BR_STATE_KWP_FASTINIT,
	BR_STATE_OPEN };

struct diag_l0_br_device
{
	int protocol;
	int dev_features;	/* Device features */
	enum BR_STATE dev_state;		/* State for 5 baud startup stuff */
	uint8_t dev_kb1;	/* KB1/KB2 for 5 baud startup stuff */
	uint8_t dev_kb2;


	uint8_t	dev_rxbuf[MAXRBUF];	/* Receive buffer XXX need to be this big? */
	int		dev_rxlen;	/* Length of data in buffer */
	int		dev_rdoffset;	/* Offset to read from to */

	uint8_t	dev_txbuf[16];	/* Copy of last sent frame */
	unsigned int		dev_txlen;	/* And length */

	uint8_t	dev_framenr;	/* Frame nr for vpw/pwm */
};

/*
 * Device features (depends on s/w version on the BR-1 device)
 */
#define BR_FEATURE_2BYTE	0x01	/* 2 byte initialisation responses */
#define BR_FEATURE_SETADDR	0x02	/* User can specifiy ISO address */
#define BR_FEATURE_FASTINIT	0x04	/* ISO14230 fast init supported */


static int diag_l0_br_getmsg(struct diag_l0_device *dl0d,
	uint8_t *dp, unsigned int timeout);

static int diag_l0_br_initialise(struct diag_l0_device *dl0d,
	uint8_t type, uint8_t addr);

static int diag_l0_br_writemsg(struct diag_l0_device *dl0d,
	uint8_t type, const void *dp, size_t txlen);

static void diag_l0_br_close(struct diag_l0_device *dl0d);

/* Types for writemsg - corresponds to top bit values for the control byte */
#define BR_WRTYPE_DATA	0x00
#define BR_WRTYPE_INIT	0x40

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code to initialise its
 * variables, etc.
 */
static int
diag_l0_br_init(void)
{
	/* Global init flag */
	static int diag_l0_br_initdone=0;

	if (diag_l0_br_initdone)
		return 0;

	/* Do required scheduling tweeks */
	diag_os_sched();
	diag_l0_br_initdone = 1;

	return 0;
}

static void
diag_l0_br_close(struct diag_l0_device *dl0d)
{
	if (!dl0d) return;

	struct diag_l0_br_device *dev =
		(struct diag_l0_br_device *)dl0d->l0_int;

	if (diag_l0_debug & DIAG_DEBUG_CLOSE)
		fprintf(stderr, FLFMT "link %p closing\n", FL, (void *)dl0d);

	if (dev)
		free(dev);
	diag_tty_close(dl0d);
	diag_l0_del(dl0d);

	return;
}

static int
diag_l0_br_write(struct diag_l0_device *dl0d, const void *dp, size_t txlen)
{
	if (txlen <=0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_tty_write(dl0d, dp, txlen) != (int) txlen) {
		fprintf(stderr, FLFMT "br_write error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
}

/*
 * Open the diagnostic device, return a file descriptor,
 * record the original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_br_open(const char *subinterface, int iProtocol)
{
	struct diag_l0_device *dl0d;
	struct diag_l0_br_device *dev;
	int rv;
	uint8_t buf[4];	/* Was MAXRBUF. We only use 1! */
	struct diag_serial_settings set;

	diag_l0_br_init();

	if ((rv=diag_calloc(&dev, 1)))
		return diag_pseterr(rv);

	dev->protocol = iProtocol;
	dev->dev_rdoffset = 0;
	dev->dev_txlen = 0;
	dev->dev_framenr = 0;
	dev->dev_state = BR_STATE_CLOSED;
	dev->dev_features = BR_FEATURE_SETADDR;

	/* Get an L0 link */
	dl0d = diag_l0_new();
	if (!dl0d) {
		free(dev);
		return diag_pseterr(rv);
	}
	dl0d->dl0 = diag_l0_br;
	dl0d->l0_int = dev;

	/* try to open TTY */
	if ((rv=diag_tty_open(dl0d, subinterface))) {
		diag_l0_del(dl0d);
		free(dev);
		return diag_pseterr(rv);
	}

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "features 0x%X\n", FL, dev->dev_features);
	}

	/* Set serial line to 19200 baud , 8N1 */
	set.speed = 19200;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if (diag_tty_setup(dl0d, &set)) {
		fprintf(stderr, FLFMT "open: TTY setup failed\n", FL);
		diag_l0_br_close(dl0d);
		return diag_pseterr(rv);
	}

	diag_tty_iflush(dl0d);	/* Flush unread input data */

	/*
	 * Initialise the BR1 interface by sending the CHIP CONNECT
	 * (0x20h) command, we should get a 0xFF back
	 */
	buf[0] = 0x20;
	if (diag_l0_br_write(dl0d, buf, 1)) {
		if ((diag_l0_debug&DIAG_DEBUG_OPEN)) {
			fprintf(stderr, FLFMT "CHIP CONNECT write failed link %p\n",
				FL, (void *)dl0d);
		}
		diag_l0_br_close(dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}
	/* And expect 0xff as a response */
	if (diag_tty_read(dl0d, buf, 1, 100) != 1) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "CHIP CONNECT read failed link %p\n",
				FL, (void *)dl0d);
		}

		diag_l0_br_close(dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}
	if (buf[0] != 0xff) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "CHIP CONNECT rcvd 0x%X != 0xff, link %p\n",
				FL, buf[0], (void *)dl0d);
		}

		diag_l0_br_close(dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}

	/* If it's J1850, send initialisation string now */
	rv = 0;
	switch (iProtocol)
	{
	case DIAG_L1_J1850_VPW:
		rv = diag_l0_br_initialise(dl0d, 0, 0);
		break;
	case DIAG_L1_J1850_PWM:
		rv = diag_l0_br_initialise(dl0d, 1, 0);
		break;
	case DIAG_L1_ISO9141:
	case DIAG_L1_ISO14230:
		/* This initialisation is done in the SLOWINIT code */
		break;
	}
	if (rv) {
		diag_l0_br_close(dl0d);
		return diag_pseterr(rv);
	}

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open succeeded link %p features 0x%X\n",
			FL, (void *)dl0d, dev->dev_features);
	}
	return dl0d;
}


/*
 * Do BR interface protocol initialisation,
 * returns -1 on error or the keybyte value
 */
static int
diag_l0_br_initialise(struct diag_l0_device *dl0d, uint8_t type, uint8_t addr)
{
	struct diag_l0_br_device *dev =
		(struct diag_l0_br_device *)dl0d->l0_int;

	uint8_t txbuf[3];
	uint8_t rxbuf[MAXRBUF];
	int rv;
	unsigned int timeout;


	/*
	 * Send initialisation message, 42H 0YH
	 * - where Y is iniitialisation type
	 */
	memset(txbuf, 0, sizeof(txbuf));
	txbuf[0] = 0x41;
	txbuf[1] = type;
	txbuf[2] = addr;

	if (type == 0x02) {
		timeout = 6000;		/* 5 baud init is slow */
		if (dev->dev_features & BR_FEATURE_SETADDR) {
			txbuf[0] = 0x42;
			rv = diag_l0_br_write(dl0d, txbuf, 3);
		} else {
			rv = diag_l0_br_write(dl0d, txbuf, 2);
		}
		if (rv)
			return diag_iseterr(rv);
	} else {
		timeout = 100;
		rv = diag_l0_br_write(dl0d, txbuf, 2);
		if (rv)
			return diag_iseterr(rv);
	}

	/*
	 * And get back the fail/success message
	 */
	if ((rv = diag_l0_br_getmsg(dl0d, rxbuf, timeout)) < 0)
		return diag_iseterr(rv);

	/*
	 * Response length tells us whether this is an orginal style
	 * interface or one that supports ISO14230 fast init and ISO9141
	 * 5 baud init address setting. This means that it is vital that
	 * a J1850 initialisation request was done before a 9141 one ...
	 */
	dev->dev_features = 0;
	switch (rv)
	{
	case 1:
		dev->dev_features |= BR_FEATURE_SETADDR; /* All allow this */
		break;
	case 2:
		dev->dev_features |= BR_FEATURE_2BYTE;
		dev->dev_features |= BR_FEATURE_SETADDR;
		dev->dev_features |= BR_FEATURE_FASTINIT;
		break;
	default:
		return diag_iseterr(DIAG_ERR_BADDATA);
	}

	return rxbuf[0];
}

/*
 * Do 5 Baud initialisation
 *
 * This is simple on the BR1 interface, we send the initialisation
 * string, which is 41H 02H and the interface responds with
 * a 1 byte message (i.e length byte of 01h followed by one of the
 * keybytes
 */
static int
diag_l0_br_slowinit( struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	struct diag_l0_br_device *dev =
		(struct diag_l0_br_device *)dl0d->l0_int;
	/*
	 * Slow init
	 * Build message into send buffer, and calculate checksum
	 */
	uint8_t buf[16];	//limited by diag_l0_br_writemsg
	int rv;

	buf[0] = 0x02;
	buf[1] = in->addr;

	/* Send the initialisation message */
	if (dev->dev_features & BR_FEATURE_SETADDR)
		rv = diag_l0_br_writemsg(dl0d, BR_WRTYPE_INIT, buf, 2);
	else
		rv = diag_l0_br_writemsg(dl0d, BR_WRTYPE_INIT, buf, 1);
	if (rv < 0)
		return rv;

	/* And wait for response */
	if ((rv = diag_l0_br_getmsg(dl0d, buf, 6000)) < 0)
		return rv;

	/*
	 * Now set the keybytes from what weve sent
	 */
	if (rv == 1)	/* 1 byte response, old type interface */
	{
		dev->dev_kb1 = buf[0];
		dev->dev_kb2 = buf[0];
	}
	else
	{
		dev->dev_kb1 = buf[0];
		dev->dev_kb2 = buf[1];
	}

	/*
	 * And tell read code to report the keybytes on first read
	 */
	dev->dev_state = BR_STATE_KWP_SENDKB1;

	return 0;
}

/*
 * Do wakeup on the bus
 *
 * We do this by noting a wakeup needs to be done for the next packet for
 * fastinit, and doing slowinit now
 */
static int
diag_l0_br_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = 0;
	struct diag_l0_br_device *dev;

	dev = (struct diag_l0_br_device *)dl0d->l0_int;

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr,
			FLFMT "device link %p info %p initbus type %d proto %d\n",
			FL, (void *)dl0d, (void *)dev, in->type,
			dev ? dev->protocol : -1);

	if (!dev)
		return diag_iseterr(DIAG_ERR_GENERAL);

	diag_tty_iflush(dl0d); /* Flush unread input */

	switch (in->type)
	{
	case DIAG_L1_INITBUS_5BAUD:
		rv = diag_l0_br_slowinit(dl0d, in);
		break;
	case DIAG_L1_INITBUS_FAST:
		if ((dev->dev_features & BR_FEATURE_FASTINIT) == 0)
		{
			/* Fast init Not supported */
			rv = DIAG_ERR_INIT_NOTSUPP;
		}
		else
		{
			/* Fastinit done on 1st TX */
			dev->dev_state = BR_STATE_KWP_FASTINIT;
			rv = 0;
		}
		break;
	default:
		rv = DIAG_ERR_INIT_NOTSUPP;
		break;
	}
	return rv? diag_iseterr(rv):0 ;
}

/*
 * Set speed/parity etc
 *
 * If called by the user then we ignore what he says
 *
 * The internal routine still exists because it makes the code
 * look similar to the other L0 interfaces
 */
static int
diag_l0_br_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset)
{
	struct diag_serial_settings sset;
	fprintf(stderr, FLFMT "Warning: attempted to over-ride serial settings (%d). 19200;8N1 maintained\n", FL, pset->speed);
	return 0;
	//XXX we could probably remove the rest of this
	sset.speed=9600;
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;

	return diag_tty_setup(dl0d, &sset);
}

/*
 * Routine to read a whole BR1 message
 * length of which depends on the first value received.
 * This also handles "error" messages (top bit of first value set)
 *
 * Returns length of received message, or TIMEOUT error, or BUSERROR
 * if the BR interface tells us theres a congested bus
 */
static int
diag_l0_br_getmsg(struct diag_l0_device *dl0d, uint8_t *dp, unsigned int timeout)
{
	uint8_t firstbyte;
	size_t readlen;
	int rv;

	if ( (diag_l0_debug & (DIAG_DEBUG_READ|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_READ|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, FLFMT "link %p getmsg timeout %u\n",
			FL, (void *)dl0d, timeout);
	}

	/*
	 * First read the 1st byte, using the supplied timeout
	 */
	rv = diag_tty_read(dl0d, &firstbyte, 1, timeout);
	if (rv != 1) {
		if ( (diag_l0_debug & (DIAG_DEBUG_READ|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_READ|DIAG_DEBUG_DATA) ) {
			fprintf(stderr, FLFMT "link %p getmsg 1st byte timed out\n",
				FL, (void *)dl0d);
		}
		return diag_iseterr(rv);
	}

	/*
	 * Now read data. Maximum is 15 bytes.
	 */

	readlen = firstbyte & 0x0f;

	/*
	 * Reasonable timeout here as the interface told us how
	 * much data to expect, so it should arrive
	 */
	rv = diag_tty_read(dl0d, dp, readlen, 100);
	if (rv != (int)readlen) {
		fprintf(stderr, FLFMT "br_getmsg error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if ( (diag_l0_debug & (DIAG_DEBUG_READ|DIAG_DEBUG_DATA)) ==
		(DIAG_DEBUG_READ|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, FLFMT "link %p getmsg read ctl 0x%X data:",
			FL, (void *)dl0d, firstbyte & 0xff);
		diag_data_dump(stderr, dp, readlen);
		printf("\n");
	}

	/*
	 * Message read complete, check error flag
	 * Top bit set means error, Bit 6 = VPW/PWM bus
	 * congestion (i.e retry).
	 */
	if (firstbyte & 0x80)	/* Error indicator */
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	if (firstbyte & 0x40)	/* VPW/PWM bus conflict, need to retry */
		return diag_iseterr(DIAG_ERR_BUSERROR);

	if (readlen == 0)	/* Should never happen */
		return diag_iseterr(DIAG_ERR_TIMEOUT);

	return (int) readlen;
}


/*
 * Write Message routine. Adds the length byte to the data before sending,
 * and the frame number for VPW/PWM. The type is used to set the top bits
 * of the control byte
 *
 * Returns 0 on success, <0 on error
 * txlen must be <= 15
 */
static int
diag_l0_br_writemsg(struct diag_l0_device *dl0d, uint8_t type,
		 const void *dp, size_t txlen)
{
	struct diag_l0_br_device *dev =
		(struct diag_l0_br_device *)dl0d->l0_int;
	int rv, j1850mode;
	uint8_t outb;

	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, FLFMT "device %p link %p sending to BR1\n",
			FL, (void *)dev, (void *)dl0d);
	}

	if (txlen > 15) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if ((dev->protocol == DIAG_L1_J1850_VPW) ||
		(dev->protocol == DIAG_L1_J1850_PWM)) {
		j1850mode = 1;
		outb = (uint8_t) txlen + 1; /* We also send a frame number */
	} else {
		j1850mode = 0;
		outb = (uint8_t) txlen;
	}

	outb |= type;	/* Set the type bits on the control byte */

	/* Send the length byte */
	rv = diag_l0_br_write(dl0d, &outb, 1);
	if (rv < 0)
		return diag_iseterr(rv);

	/* And now the data */
	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, FLFMT "device %p writing data: ",
			FL, (void *)dev);
		fprintf(stderr,"0x%X ", (int)outb);	/* Length byte */
		diag_data_dump(stderr, dp, txlen);
		fprintf(stderr, "\n");
	}
	rv = diag_l0_br_write(dl0d, dp, txlen);
	if (rv < 0)
		return diag_iseterr(rv);

	/*
	 * ISO mode is raw pass through. In J1850 we need to send
	 * frame numbers, and keep track of whether we are sending/receiving
	 * in order to receive multiple frames.
	 */
	if (j1850mode) {
		if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) ) {
			fprintf(stderr,
				FLFMT "device %p writing data: 0x%X\n",
				FL, (void *)dev, dev->dev_framenr & 0xff);
		}
		rv = diag_l0_br_write(dl0d, &dev->dev_framenr, 1);
		if (rv < 0)
			return diag_iseterr(rv);
	}
	return 0;
}


/*
 * Send a load of data
 *
 * Returns 0 on success, -1 on failure
 *
 * This routine will do a fastinit if needed, but all 5 baud inits
 * will have been done by the slowinit() code
 */
static int
diag_l0_br_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len)
{
	int rv = 0;

	struct diag_l0_br_device *dev;

	dev = (struct diag_l0_br_device *)dl0d->l0_int;

	if (len <= 0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr,
			FLFMT "device link %p send %ld bytes protocol %d state %d: ",
			FL, (void *)dl0d, (long)len,
			dev->protocol, dev->dev_state);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			diag_data_dump(stderr, data, len);
			fprintf(stderr, "\n");
		} else {
			fprintf(stderr,"\n");
		}
	}

	/*
	 * Special handling for fastinit, we need to collect up the
	 * bytes of the StartComms message sent by the upper layer
	 * when we have a whole message we can then send the request
	 * as part of a special initialisation type
	 */
	if (dev->dev_state == BR_STATE_KWP_FASTINIT) {
		uint8_t outbuf[6];
		if (dev->dev_txlen < 5) {
			memcpy(&dev->dev_txbuf[dev->dev_txlen], data, len);
			dev->dev_txlen += len;
			rv = 0;
		}

		if (dev->dev_txlen >= 5) {
			/*
			 * Startcomms request is 5 bytes long - we have
			 * 5 bytes, so now we should send the initialisation
			 */
			outbuf[0] = 0x03;
			memcpy(&outbuf[1], dev->dev_txbuf, 5);
			rv = diag_l0_br_writemsg(dl0d, BR_WRTYPE_INIT,
				outbuf, 6);
			/* Stays in FASTINIT state until first read */
		}
	} else {
		/*
		 * Now, keep a copy of the data, and set the framenr to 1
		 * This means the receive code will resend the request if it
		 * wants to get a frame number 2 or 3 or whatever
		 */
		memcpy(dev->dev_rxbuf, data, len);
		dev->dev_txlen = len;
		dev->dev_framenr = 1;

		/* And now encapsulate and send the data */
		rv = diag_l0_br_writemsg(dl0d, BR_WRTYPE_DATA, data, len);
	}

	return rv;
}

/*
 * Get data (blocking), returns number of bytes read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 *
 * This attempts to read whole message, so if we receive any data, timeout
 * is restarted
 *
 * Messages received from the BR1 are of format
 * <control_byte><data ..>
 * If control byte is < 16, it's a length byte, else it's a error descriptor
 */
static int
diag_l0_br_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
void *data, size_t len, unsigned int timeout)
{
	int xferd, rv, retrycnt;
	uint8_t *pdata = (uint8_t *)data;

	struct diag_l0_br_device *dev;
	dev = (struct diag_l0_br_device *)dl0d->l0_int;

	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %ld bytes timeout %u, rxlen %d offset %d framenr %d protocol %d state %d\n",
			FL, (void *)dl0d, (long)len, timeout, dev->dev_rxlen,
			dev->dev_rdoffset, dev->dev_framenr, dev->protocol,
			dev->dev_state);

	switch (dev->dev_state) {
		case BR_STATE_KWP_FASTINIT:
			/* Extend timeouts */
			timeout = 300;
			dev->dev_state = BR_STATE_OPEN;
			break;
		case BR_STATE_KWP_SENDKB1:
			if (len >= 2) {
				pdata[0] = dev->dev_kb1;
				pdata[1] = dev->dev_kb2;
				dev->dev_state = BR_STATE_OPEN;
				return 2;
			} else if (len == 1) {
				*pdata = dev->dev_kb1;
				dev->dev_state = BR_STATE_KWP_SENDKB2;
				return 1;
			}
			return 0;	/* Strange, user asked for 0 bytes */
			break;
		case BR_STATE_KWP_SENDKB2:
			if (len >= 1) {
				*pdata = dev->dev_kb2;
				dev->dev_state = BR_STATE_OPEN;
				return 1;
			}
			return 0;	/* Strange, user asked for 0 bytes */
			break;
		default:
			//So BR_STATE_CLOSED and BR_STATE_OPEN.
			// I don't know what's supposed to happen here, so
			fprintf(stderr, FLFMT "Warning : landed in a strange place. Report this please !\n", FL);
			return 0;
			break;
	}

	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
	case DIAG_L1_ISO14230:
		/* Raw mode */
		xferd = diag_tty_read(dl0d, data, len, timeout);
		break;
	default:
		/*
		 * PWM/VPW Modes
		 *
		 * If theres stuff on the dev-descriptor, give it back
		 * to the user.
		 * We extend timeouts here because in PWM/VPW
		 * modes the interface tells us if there is a timeout, and
		 * we get out of sync if we dont wait for it.
		 */
		if (timeout < 500)
			timeout = 500;

		if (dev->dev_rxlen == 0) {
			/*
			 * No message available, try getting one
			 *
			 * If this is the 2nd read after a send, then
			 * we need to resend the request with the next
			 * frame number to see if any more data is ready
			 */
			if (dev->dev_framenr > 1) {
				rv = diag_l0_br_writemsg(dl0d,
					BR_WRTYPE_DATA,
					dev->dev_txbuf, (size_t)dev->dev_txlen);
			}
			dev->dev_framenr++;

			retrycnt = 0;
			while (1) {
				dev->dev_rdoffset = 0;
				rv = diag_l0_br_getmsg(dl0d, dev->dev_rxbuf, timeout);
				if (rv >= 0) {
					dev->dev_rxlen = rv;
					break;
				}
				if ((rv != DIAG_ERR_BUSERROR) ||
					(retrycnt >= 30)) {
					dev->dev_rxlen = 0;
					return rv;
				}
				/* Need to resend and try again */
				rv = diag_l0_br_writemsg(dl0d,
					BR_WRTYPE_DATA, dev->dev_txbuf,
					(size_t)dev->dev_txlen);
				if (rv < 0)
					return rv;
				retrycnt++;
			}
		}
		if (dev->dev_rxlen) {
			size_t bufbytes = dev->dev_rxlen - dev->dev_rdoffset;

			if (bufbytes <= len) {
				memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], bufbytes);
				dev->dev_rxlen = dev->dev_rdoffset = 0;
				return (int) bufbytes;
			} else {
				memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], len);
				dev->dev_rdoffset += len;
				return (int) len;
			}
		}
		xferd = 0;
		break;
	}

	/* OK, got whole message */
	if (diag_l0_debug & DIAG_DEBUG_READ) {
		fprintf(stderr,
			FLFMT "link %p received from BR1: ", FL, (void *)dl0d);
		diag_data_dump(stderr, data, (size_t)xferd);
		fprintf(stderr, "\n");
	}

	return xferd;
}


static uint32_t
diag_l0_br_getflags(struct diag_l0_device *dl0d)
{
	/*
	 * ISO14230/J1850 protocol does L2 framing, ISO9141 is just
	 * raw, once initialised
	 */
	struct diag_l0_br_device *dev;
	uint32_t flags;

	dev = (struct diag_l0_br_device *)dl0d->l0_int;

	flags = 0;
	switch (dev->protocol) {
	case DIAG_L1_J1850_VPW:
	case DIAG_L1_J1850_PWM:
			flags = DIAG_L1_DOESL2FRAME;
			break;
	case DIAG_L1_ISO9141:
			flags = DIAG_L1_SLOW;
			flags |= DIAG_L1_DOESP4WAIT;
			break;
	case DIAG_L1_ISO14230:
			flags = DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
			flags |= DIAG_L1_DOESP4WAIT;
			break;
	}

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr,
			FLFMT "getflags link %p proto %d flags 0x%X\n",
			FL, (void *)dl0d, dev->protocol, flags);

	return flags;
}

const struct diag_l0 diag_l0_br = {
	"B. Roadman BR-1 interface",
	"BR1",
	DIAG_L1_J1850_VPW | DIAG_L1_J1850_PWM |
		DIAG_L1_ISO9141 | DIAG_L1_ISO14230,
	NULL,
	NULL,
	NULL,
	diag_l0_br_init,
	diag_l0_br_open,
	diag_l0_br_close,
	diag_l0_br_initbus,
	diag_l0_br_send,
	diag_l0_br_recv,
	diag_l0_br_setspeed,
	diag_l0_br_getflags
};

