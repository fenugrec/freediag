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
 * Diag, Layer 0, Interface for Multiplex Engineering interface
 *		Supports #T16 interface only. Other interfaces need special
 *		code to read multi-frame messages with > 3 frames (and don't
 *		support all interface types)
 *
 *   http://www.multiplex-engineering.com
 *
 */


#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_iso14230.h"
#include "diag_tty.h"
#include "diag_l1.h"

CVSID("$Id$");

#define INTERFACE_ADDRESS 0x38


static const struct diag_l0 diag_l0_muleng;

/*
 * Baud rate table for converting single byte value from interface to
 * baud rate. Note the single byte value is count in 2.5microseconds for
 * receiving a bit of the 0x55
 */
static const unsigned int me_baud_table[] = { 0, 400000, 200000, 133333, 100000, 80000,
			66666, 57142, 50000, 44444,
	/* 10 */ 40000, 36363, 33333, 30769, 28571, 26666,
			25000, 23529, 22222, 21052,
	/* 20 */ 19200, 19200, 18181, 17391, 16666, 16000,
			15384, 14814, 14285, 13793,
	/* 30 */ 13333, 12903, 12500, 12121, 11764, 11428,
			11111, 10400, 10400, 10400,
	/* 40 */ 10400, 9600, 9600, 9600, 9600, 8888, 8695, 8510, 8333, 8163,
	/* 50 */ 8000, 7843, 7692, 7547, 7407, 7272, 7142, 7017, 6896, 6779,
	/* 60 */ 6666, 6557, 6451, 6349, 0, 6153, 6060, 5970, 5882, 5797,
	/* 70 */ 5714, 5633, 5555, 5479, 5405, 5333, 5263, 5194, 5128, 5063,
	/* 80 */ 5000, 4800, 4800, 4800, 4800, 4800, 4800, 4597, 4545, 4494,
	/* 90 */ 4444, 4395, 4347, 4301, 4255, 4210, 4166, 4123, 4081, 4040,
	/* 100 */ 4000, 3960, 3921, 3883, 3846, 3809, 3600, 3600, 3600, 3600,
	/* 110 */ 3600, 3600, 3600, 3600, 3600, 3478, 3448, 3418, 3389, 3361,
	/* 120 */ 3333, 3305, 3278, 3252, 3225, 3200, 3174, 3149, 3125, 3100,
	/* 130 */ 3076, 3053, 3030, 3007, 2985, 2962, 2941, 2919, 2898, 2877,
	/* 140 */ 2857, 2836, 2816, 2797, 2777, 2758, 2739, 2721, 2702, 2684,
	/* 150 */ 2666, 2649, 2631, 2614, 2597, 2580, 2564, 2547, 2531, 2515,
	/* 160 */ 2500, 2400, 2400, 2400, 2400, 2400, 2400, 2400, 2400, 2400,
	/* 170 */ 2400, 2400, 2400, 2400, 2298, 2285, 2272, 2259, 2247, 2234,
	/* 180 */ 2222, 2209, 2197, 2185, 2173, 2162, 2150, 2139, 2127, 2116,
	/* 190 */ 2105, 2094, 2083, 2072, 2061, 2051, 2040, 2030, 2020, 2010,
	/* 200 */ 2000, 1990, 1980, 1970, 1960, 1951, 1941, 1932, 1923, 1913,
	/* 210 */ 1904, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 220 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 230 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 240 */ 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800, 1800,
	/* 250 */ 1600, 1593, 1587, 1581, 1574, 1568, } ;

struct diag_l0_muleng_device
{
	int protocol;
	int dev_wakeup;		/* Contains wakeup type for next packet */
	int dev_state;		/* State for 5 baud startup stuff */
	uint8_t dev_kb1;	/* KB1/KB2 for 5 baud startup stuff */
	uint8_t dev_kb2;

	uint8_t	dev_rxbuf[14];	/* Receive buffer */
	int		dev_rxlen;	/* Length of data in buffer */
	int		dev_rdoffset;	/* Offset to read from to */
};

#define MULENG_STATE_CLOSED		0x00

/* 5 baud init was successful, need to report keybytes on first recv() */
#define MULENG_STATE_KWP_SENDKB1		0x01
#define MULENG_STATE_KWP_SENDKB2		0x02

#define MULENG_STATE_RAW		0x10	/* Open and working in Passthru mode */

#define MULENG_STATE_FASTSTART		0x18	/* 1st recv() after fast init */
#define MULENG_STATE_OPEN		0x20	/* Open and working */


static int diag_l0_muleng_getmsg(struct diag_l0_device *dl0d, uint8_t *dp);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_muleng_init(void)
{
/* Global init flag */
	static int diag_l0_muleng_initdone=0;

	if (diag_l0_muleng_initdone)
		return 0;

	/* Do required scheduling tweeks */
	diag_os_sched();
	diag_l0_muleng_initdone = 1;

	return 0;
}

/* Put in the ME checksum at the correct place */
static int
diag_l0_muleng_txcksum(uint8_t *data)
{
	int i, cksum;

	for (i=1, cksum = 0; i < 14; i++)
		cksum += data[i];
	data[14] = (uint8_t)cksum;
	return cksum;
}

/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_muleng_open(const char *subinterface, int iProtocol)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_muleng_device *dev;
	struct diag_serial_settings set;

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_muleng_init();

	if ((rv=diag_calloc(&dev, 1)))
		return (struct diag_l0_device *)diag_pseterr(rv);

	dev->protocol = iProtocol;

	if ((rv=diag_tty_open(&dl0d, subinterface, &diag_l0_muleng, (void *)dev))) {
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	/* And set to 19200 baud , 8N1 */

	set.speed = 19200;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	if ((rv=diag_tty_setup(dl0d, &set))) {
		diag_tty_close(&dl0d);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	/* And set DTR high and RTS low to power the device */
	if ((rv=diag_tty_control(dl0d, 1, 0))) {
		diag_tty_close(&dl0d);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */

	return dl0d ;
}

static int
diag_l0_muleng_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_muleng_device *dev =
			(struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);

		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n",
				FL, (void *)dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

	return 0;
}

/*
 * Safe write routine; return 0 on success
 */
static int
diag_l0_muleng_write(struct diag_l0_device *dl0d, const void *dp, size_t txlen)
{
	ssize_t xferd;

	if (!txlen)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) )
	{
		fprintf(stderr, FLFMT "device link %p sending to ME device: ",
			FL, (void *)dl0d);
		diag_data_dump(stderr, dp, txlen);
		fprintf(stderr, "\n");
	}

	/*
	 * And send it to the interface
	 */
	while ((size_t)(xferd = diag_tty_write(dl0d, dp, txlen)) != txlen)
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
		txlen -= (size_t) xferd;
		dp = (const void *)((const char *)dp + xferd);
	}
	return 0;
}


/*
 * Do 5 Baud initialisation
 *
 * In the case of ISO9141 we operate in the interface's "raw" mode
 * (VAG compatibility mode), in 14230 we do a slow init and send
 * a tester present message
 */
static int
diag_l0_muleng_slowinit( struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in,
		struct diag_l0_muleng_device *dev)
{
	/*
	 * Slow init
	 * Build message into send buffer, and calculate checksum
	 */
	uint8_t txbuf[15];
	uint8_t rxbuf[15];
	int rv;
	unsigned int baud;

	memset(txbuf, 0, sizeof(txbuf));
	txbuf[0] = INTERFACE_ADDRESS;

	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		txbuf[1] = 0x20;	/* Raw mode 5 baud init */
		txbuf[2] = in->addr;
		break;
	case DIAG_L1_ISO14230:
		txbuf[1] = 0x85;
		txbuf[2] = 0x01;		/* One byte message */
		txbuf[3] = DIAG_KW2K_SI_TP;	/* tester present */
		break;
	}

	/*
	 * Calculate the checksum, and send the request
	 */
	(void)diag_l0_muleng_txcksum(txbuf);
	if ((rv = diag_l0_muleng_write(dl0d, txbuf, 15)))
		return diag_iseterr(rv);

	/*
	 * Get answer
	 */
	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		/*
		 * This is raw mode, we should get a single byte back
		 * with the timing interval, then we need to change speed
		 * to match that speed. Remember it takes 2 seconds to send
		 * the 10 bit (1+8+1) address at 5 baud
		 */
		rv = diag_tty_read(dl0d, rxbuf, 1, 2350);
		if (rv < 1)
			return diag_iseterr(DIAG_ERR_GENERAL);

		if (rxbuf[0] == 0x40) {
			/* Problem ..., got an error message */

			diag_tty_iflush(dl0d); /* Empty the receive buffer */

			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		baud = me_baud_table[rxbuf[0]];

		if (diag_l0_debug & DIAG_DEBUG_PROTO)
			fprintf(stderr, FLFMT "device link %p setting baud to %u\n",
				FL, (void *)dl0d, baud);

		if (baud) {
			struct diag_serial_settings set;
			set.speed = baud;
			set.databits = diag_databits_8;
			set.stopbits = diag_stopbits_1;
			set.parflag = diag_par_n;

			/* And set the baud rate */
			diag_tty_setup(dl0d, &set);
		}

		dev->dev_state = MULENG_STATE_RAW;

		break;
	case DIAG_L1_ISO14230:
		/* XXX
		 * Should get an ack back, rather than an error response
		 */
		if ((rv = diag_l0_muleng_getmsg(dl0d, rxbuf)) < 0)
			return diag_iseterr(rv);

		if (rxbuf[1] == 0x80)
			return diag_iseterr(DIAG_ERR_GENERAL);

		/*
 		 * Now send the "get keybyte" request, and wait for
		 * response
		 */
		memset(txbuf, 0, sizeof(txbuf));
		txbuf[0] = INTERFACE_ADDRESS;
		txbuf[1] = 0x86;
		(void)diag_l0_muleng_txcksum(txbuf);
		rv = diag_l0_muleng_write(dl0d, txbuf, 15);
		if (rv < 0)
			return diag_iseterr(rv);

		if ((rv = diag_l0_muleng_getmsg(dl0d, rxbuf)) < 0)
			return diag_iseterr(rv);

		if (rxbuf[1] == 0x80)	/* Error */
			return diag_iseterr(rv);
		/*
		 * Store the keybytes
		 */
		dev->dev_kb1 = rxbuf[2];
		dev->dev_kb2 = rxbuf[3];
		/*
		 * And tell read code to report the keybytes on first read
		 */
		dev->dev_state = MULENG_STATE_KWP_SENDKB1;
		break;
	}


	return rv;
}

/*
 * Do wakeup on the bus
 *
 * We do this by noting a wakeup needs to be done for the next packet for
 * fastinit, and doing slowinit now
 */
static int
diag_l0_muleng_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = 0;
	struct diag_l0_muleng_device *dev;

	dev = (struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);

	if (!dev)
		return diag_iseterr(DIAG_ERR_GENERAL);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d proto %d\n",
			FL, (void *)dl0d, (void *)dev, in->type, dev->protocol);

	diag_tty_iflush(dl0d); /* Empty the receive buffer, wait for idle bus */

	if (in->type == DIAG_L1_INITBUS_5BAUD)
		rv = diag_l0_muleng_slowinit(dl0d, in, dev);
	else
	{
		/* Do wakeup on first TX */
		dev->dev_wakeup = in->type;
		dev->dev_state = MULENG_STATE_FASTSTART;
	}

	return rv;
}

/*
 * Set speed/parity etc
 *
 * If called by the user then we ignore what he says and use
 * 19200, 8, 1, none
 */

static int
diag_l0_muleng_setspeed(struct diag_l0_device *dl0d,
		const struct diag_serial_settings *pset)
{
	struct diag_serial_settings set;

	fprintf(stderr, FLFMT "Warning: attempted to override com speed (%d)! Report this !\n", FL,pset->speed);
	return 0;
	// I see no need to force another diag_tty_setup
	set.speed = 19200;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	return diag_tty_setup(dl0d, &set);
}

/*
 * Routine to read a whole ME message
 * which is 14 bytes
 */
static int
diag_l0_muleng_getmsg(struct diag_l0_device *dl0d, uint8_t *dp)
{
	size_t offset = 0;
	ssize_t xferd;
	//struct diag_l0_muleng_device *dev;

	//dev = (struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);	//why do we do this ?

	while (offset != 14)
	{
		xferd = diag_tty_read(dl0d, &dp[offset], 14 - offset, 200);
		if (xferd < 0)
			return xferd;
		offset += (size_t) xferd;
	}
	return (int) offset;
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
diag_l0_muleng_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len)
{
	int rv;
	uint8_t cmd;

	uint8_t txbuf[MAXRBUF];
	struct diag_l0_muleng_device *dev;

	dev = (struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);

	if (len <= 0)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (len > 255) {
		fprintf(stderr, FLFMT "_send : requesting too many bytes !\n", FL);
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
	{
		fprintf(stderr, FLFMT "device link %p send %ld bytes protocol %d ",
			FL, (void *)dl0d, (long)len, dev->protocol);
		if (diag_l0_debug & DIAG_DEBUG_DATA)
		{
			diag_data_dump(stderr, data, len);
			fprintf(stderr, "\n");
		}
	}

	if (dev->dev_state == MULENG_STATE_RAW)
	{
		/* Raw mode, no pretty processing */
		rv = diag_l0_muleng_write(dl0d, data, len);
		return rv;
	}

	/*
	 * Figure out cmd to send depending on the hardware we have been
	 * told to use and whether we need to do fastinit or not
	 */
	switch (dev->protocol)
	{
	case DIAG_L1_ISO9141:
		cmd = 0x10;
		break;

	case DIAG_L1_ISO14230:
		if (dev->dev_wakeup == DIAG_L1_INITBUS_FAST)
			cmd = 0x87;
		else
			cmd = 0x88;
		dev->dev_wakeup = 0;	/* We've done the wakeup now */
		break;

	case DIAG_L1_J1850_VPW:
		cmd = 0x02;
		break;

	case DIAG_L1_J1850_PWM:
		cmd = 0x04;
		break;

	case DIAG_L1_CAN:
		cmd = 0x08;
		break;
	default:
		fprintf(stderr, FLFMT "Command never initialised.\n", FL);
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

	/*
	 * Build message into send buffer, and calculate checksum and
	 * send it
	 */
	memset(txbuf, 0, sizeof(txbuf));

	txbuf[0] = INTERFACE_ADDRESS;
	txbuf[1] = cmd;
	txbuf[2] = (uint8_t) len;
	memcpy(&txbuf[3], data, len);

	(void)diag_l0_muleng_txcksum(txbuf);
	rv = diag_l0_muleng_write(dl0d, txbuf, 15);

	return rv;
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 *
 * This attempts to read whole message, so if we receive any data, timeout
 * is restarted
 *
 * Messages received from the ME device are 14 bytes long, this will
 * always be called with enough "len" to receive the max 11 byte message
 * (there are 2 header and 1 checksum byte)
 */

static int
diag_l0_muleng_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
void *data, size_t len, int timeout)
{
	ssize_t xferd;
	int i;
	uint8_t *pdata = (uint8_t *)data;

	struct diag_l0_muleng_device *dev;
	dev = (struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);

	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %ld bytes timeout %d, rxlen %d offset %d\n",
			FL, (void *)dl0d, (long)len, timeout, dev->dev_rxlen, dev->dev_rdoffset);

	/*
	 * Deal with 5 Baud init states where first two bytes read by
	 * user are the keybytes received from the interface, and where
	 * we are using the interface in pass thru mode on ISO09141 protocols
	 */
	switch (dev->dev_state)
	{
	case MULENG_STATE_KWP_SENDKB1:
		if (len >= 2)
		{
			pdata[0] = dev->dev_kb1;
			pdata[1] = dev->dev_kb2;
			dev->dev_state = MULENG_STATE_OPEN;
			return 2;
		}
		else if (len == 1)
		{
			*pdata = dev->dev_kb1;
			dev->dev_state = MULENG_STATE_KWP_SENDKB2;
			return 1;
		}
		return 0;	/* Strange, user asked for 0 bytes */


	case MULENG_STATE_KWP_SENDKB2:
		if (len >= 1)
		{
			*pdata = dev->dev_kb2;
			dev->dev_state = MULENG_STATE_OPEN;
			return 1;
		}
		return 0;	/* Strange, user asked for 0 bytes */


	case MULENG_STATE_RAW:
		xferd = diag_tty_read(dl0d, data, len, timeout);
		if (diag_l0_debug & DIAG_DEBUG_READ)
			fprintf(stderr, FLFMT "link %p read %ld bytes\n", FL,
				(void *)dl0d, (long)xferd);
		return xferd;

	case MULENG_STATE_FASTSTART:
		/* Extend timeout for 1st recv */
		timeout = 200;
		dev->dev_state = MULENG_STATE_OPEN;
		/* Drop thru */
	default:		/* Some other mode */
		break;
	}

	if (dev->dev_rxlen >= 14)
	{
		/*
		 * There's a full packet been received, but the user
		 * has only asked for a few bytes from it previously
		 * Of the packet, bytes x[2]->x[11] are the network data
		 * others are header from the ME device
		 *
		 * The amount of data remaining to be sent to user is
		 * as below, -1 because the checksum is at the end
		 */
		size_t bufbytes = dev->dev_rxlen - dev->dev_rdoffset - 1;

		if (bufbytes <= len)
		{
			memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], bufbytes);
			dev->dev_rxlen = dev->dev_rdoffset = 0;
			return (int) bufbytes;
		}
		else
		{
			memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], len);
			dev->dev_rdoffset += len;
			return (int) len;
		}
	}

	/*
	 * There's either no data waiting, or only a partial read in the
	 * buffer, read some more
	 */

	while (dev->dev_rxlen < 14)
	{
		while ( (xferd = diag_tty_read(dl0d, &dev->dev_rxbuf[dev->dev_rxlen],
			(size_t)(14 - dev->dev_rxlen), timeout)) <= 0)
		{
			if (xferd == DIAG_ERR_TIMEOUT)
				return DIAG_ERR_TIMEOUT;
			if (xferd == 0)
			{
				/* Error, EOF */
				fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			if (errno != EINTR)
			{
				/* Error, EOF */
				fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}
		dev->dev_rxlen += xferd;
	}

	/* OK, got whole message */
	if (diag_l0_debug & DIAG_DEBUG_READ)
	{
		fprintf(stderr,
			FLFMT "link %p received from ME: ", FL, (void *)dl0d);
		for (i=0; i < dev->dev_rxlen; i++)
				fprintf(stderr, "0x%X ",
					dev->dev_rxbuf[i] & 0xff);
		fprintf(stderr, "\n");
	}
	/*
	 * Check the checksum, 2nd byte onward
	 */
	for (i=1, xferd = 0; i < 13; i++)
		 xferd += dev->dev_rxbuf[i];
	if ((xferd & 0xff) != dev->dev_rxbuf[13])
	{
/* XXX, we should deal with this properly rather than just printing a message */
		fprintf(stderr,"Got bad checksum from ME device 0x%X != 0x%X\n",
			(int) xferd & 0xff, dev->dev_rxbuf[13]);
		fprintf(stderr,"PC Serial port probably out of spec.\n");
		fprintf(stderr,"RX Data: ");
		for (i=0; i < dev->dev_rxlen; i++)
				fprintf(stderr, "0x%X ",
					dev->dev_rxbuf[i] & 0xff);
		fprintf(stderr, "\n");
	}


	/*
	 * Check the type
	 */
	if (dev->dev_rxbuf[1] == 0x80)
	{
		/* It's an error message not a data frame */
		dev->dev_rxlen = 0;

		if (diag_l0_debug & DIAG_DEBUG_READ)
			fprintf(stderr,
				FLFMT "link %p ME returns err 0x%X : s/w v 0x%X i/f cap. 0x%X\n",
				FL, (void *)dl0d, dev->dev_rxbuf[3],
				dev->dev_rxbuf[2], dev->dev_rxbuf[4]);

		switch (dev->dev_rxbuf[3])
		{
		case 0x05:	/* No ISO response to request */
		case 0x07:	/* No J1850 response to request */
		case 0x0c:	/* No KWP response to request */
			return DIAG_ERR_TIMEOUT;
			break;

		default:
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		/* NOTREACHED */
	}

	dev->dev_rdoffset = 2;		/* Skip the ME header */

	/* Copy data to user */
	xferd = (len>11) ? 11 : xferd;
	xferd = (xferd>(13-dev->dev_rdoffset)) ? 13-dev->dev_rdoffset : xferd;

	memcpy(data, &dev->dev_rxbuf[dev->dev_rdoffset], (size_t)xferd);
	dev->dev_rdoffset += xferd;
	if (dev->dev_rdoffset == 13)
	{
		/* End of message, reset pointers */
		dev->dev_rxlen = 0;
		dev->dev_rdoffset = 0;
	}
	return xferd;
}

static int
diag_l0_muleng_getflags(struct diag_l0_device *dl0d)
{
	/*
	 * ISO14230/J1850 protocol does L2 framing, ISO9141 doesn't
	 */
	struct diag_l0_muleng_device *dev;
	int flags;

	dev = (struct diag_l0_muleng_device *)diag_l0_dl0_handle(dl0d);

	flags = 0;
	switch (dev->protocol)
	{
	case DIAG_L1_J1850_VPW:
	case DIAG_L1_J1850_PWM:
			flags = DIAG_L1_DOESL2CKSUM;
			flags |= DIAG_L1_DOESL2FRAME;
			break;
	case DIAG_L1_ISO9141:
			flags = DIAG_L1_SLOW ;
/* XX does it ?		flags |= DIAG_L1_DOESL2CKSUM; */
			break;

 	case DIAG_L1_ISO14230:
			flags = DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
			flags |= DIAG_L1_DOESL2FRAME;
			flags |= DIAG_L1_DOESSLOWINIT;
			flags |= DIAG_L1_DOESL2CKSUM;
			break;

	}

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr,
			FLFMT "getflags link %p proto %d flags 0x%X\n",
			FL, (void *)dl0d, dev->protocol, flags);

	return flags ;
}

static const struct diag_l0 diag_l0_muleng = {
	"Multiplex Engineering T16 interface",
	"MET16",
	DIAG_L1_J1850_VPW | DIAG_L1_J1850_PWM |
		DIAG_L1_ISO9141 | DIAG_L1_ISO14230,
	diag_l0_muleng_init,
	diag_l0_muleng_open,
	diag_l0_muleng_close,
	diag_l0_muleng_initbus,
	diag_l0_muleng_send,
	diag_l0_muleng_recv,
	diag_l0_muleng_setspeed,
	diag_l0_muleng_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_muleng_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_muleng_add(void) {
	return diag_l1_add_l0dev(&diag_l0_muleng);
}
