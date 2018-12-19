/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * (c) fenugrec 2014-2016
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
 * Diag, Layer 0, interface for VAGTool, Silicon Engines generic ISO9141/ISO14230
 * and other compatible "dumb" K-line interfaces, such as Jeff Noxon's opendiag interface.
 * Any RS232 interface with no microcontroller onboard should fall in this category.
 *
 * The dumbopts config option must be set according to particular type (VAGtool, SE)
 * to enable certain features (L line on RTS, etc)
 */


#include <stdlib.h>
#include <assert.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"

struct dumb_device {
	int	protocol;	//set in dumb_open with specified iProtocol
	struct	diag_serial_settings serial;

	/* "dumbopts" flags. */
	/* Using a struct cfgi for each of these really seems like overkill...
	 * current approach is to have a cfgi for an "int dumbopts", that is parsed into
	 * these flags in dumb_open(). Hence, the shortname + descriptions defined here are
	 * unused for the moment.
	 */
	bool	use_L;
		#define DD_USEL	"Use RTS to drive the L line for init. Interface must support this."
		#define DS_USEL	"USEL"
	bool	clr_dtr;
		#define DD_CLRDTR	"Always keep DTR cleared (neg. voltage). Unusual."
		#define DS_CLRDTR	"CLRDTR"
	bool	set_rts;
		#define DD_SETRTS	"Always keep RTS set (pos. voltage). Unusual; do not use with USE_L."
		#define DS_SETRTS	"SETRTS"
	bool	man_break;
		#define DD_MANBRK	"Send manual breaks (essential for USB-serial ICs that don't support 5bps.)"
		#define	DS_MANBRK	"MANBRK"
	bool	lline_inv;
		#define DD_INVL	"Invert polarity of the L line. Unusual."
		#define DS_INVL	"INVL"
	bool	fast_break;
		#define DD_FASTBK	"Try alternate iso14230 fastinit code."
		#define DS_FASTBK	"FASTB"
	bool	blockduplex;
		#define DD_BKDUPX	"Use message-based half duplex removal if P4==0."
		#define	DS_BKDUPX	"BLKDUP"

	struct	cfgi port;
	struct	cfgi dumbopts;
		#define DUMBOPTS_SN	"dumbopts"
		#define DUMBOPTS_DESC "Dumb interface option flags; addition of the desired flags:\n" \
				" 0x01 : USE_LLINE : use if the L line (driven by RTS) is required for init. Interface must support this\n" \
				"\t(VAGTOOL for example).\n" \
				" 0x02 : CLEAR_DTR : use if your interface needs DTR to be always clear (neg. voltage).\n" \
				"\tThis is unusual. By default DTR will always be SET (pos. voltage)\n" \
				" 0x04 : SET_RTS : use if your interface needs RTS to be always set (pos. voltage).\n" \
				"\tThis is unusual. By default RTS will always be CLEAR (neg. voltage)\n" \
				"\tThis option should not be used with USE_LLINE.\n" \
				" 0x08 : MAN_BREAK : essential for USB-serial converters that don't support 5bps\n" \
				"\tsuch as FTDI232*, P230* and other ICs (enabled by default).\n" \
				" 0x10: LLINE_INV : Invert polarity of the L line. see\n" \
				"\tdoc/dumb_interfaces.txt !! This is unusual.\n" \
				" 0x20: FAST_BREAK : use alternate iso14230 fastinit code.\n" \
				" 0x40: BLOCKDUPLEX : use message-based half duplex removal (if P4==0)\n\n" \
				"ex.: \"dumbopts 9\" for MAN_BREAK and USE_LLINE.\n"


	ttyp *tty_int;			/** handle for tty stuff */

};


#define BPS_PERIOD 198		//length of 5bps bits. The idea is to make this configurable eventually by adding a user-settable offset
#define WUPFLUSH 10		//should be between 5 and ~15 ms; this is the time we allow diag_tty_read to purge the break after a fast init
#define WUPOFFSET 2		//shorten fastinit wake-up pattern by WUPOFFSET ms, to fine-tune tWUP. Another ugly bandaid that should be
				//at least runtime-configurable

// dumbopts flags set according to particular interface type (VAGtool vs SE etc.)
#define USE_LLINE	0x01		//interface maps L line to RTS : setting RTS normally pulls L down to 0 .
#define CLEAR_DTR 0x02		//have DTR cleared constantly (unusual, disabled by default)
#define SET_RTS 0x04			//have RTS set constantly (also unusual, disabled by default).
#define MAN_BREAK 0x08		//force bitbanged breaks for inits; enabled by default
#define LLINE_INV 0x10		//invert polarity of the L line if set. see doc/dumb_interfaces.txt
#define FAST_BREAK 0x20		//do we use diag_tty_fastbreak for iso14230-style fast init.
#define BLOCKDUPLEX 0x40	//This allows half duplex removal on a whole message if P4==0 (see diag_l1_send())
#define DUMBDEFAULTS (MAN_BREAK | BLOCKDUPLEX)	//default set of flags



extern const struct diag_l0 diag_l0_dumb;

static void dumb_close(struct diag_l0_device *dl0d);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
dumb_init(void) {
	return 0;
}

/* fill & init new dl0d */
static int
dumb_new(struct diag_l0_device *dl0d) {
	struct dumb_device *dev;
	int rv;

	assert(dl0d);

	rv = diag_calloc(&dev, 1);
	if (rv != 0) {
		return diag_ifwderr(rv);
	}

	dl0d->l0_int = dev;

	rv = diag_cfgn_tty(&dev->port);
	if (rv != 0) {
		free(dev);
		return diag_ifwderr(rv);
	}

	rv = diag_cfgn_int(&dev->dumbopts, DUMBDEFAULTS, DUMBDEFAULTS);
	if (rv != 0) {
		diag_cfg_clear(&dev->port);
		free(dev);
		return diag_ifwderr(rv);
	}

	/* finish filling the dumbopts cfgi */
	dev->dumbopts.shortname = DUMBOPTS_SN;
	dev->dumbopts.descr = DUMBOPTS_DESC;
	dev->port.next = &dev->dumbopts;
	dev->dumbopts.next = NULL;

	printf("Note concerning generic (dumb) interfaces : there are additional\n"
			"options which can be set with \"set dumbopts\". By default\n"
			"\"K-line only\" and \"MAN_BREAK\" are set. \n");

	return 0;
}

static void dumb_del(struct diag_l0_device *dl0d) {
	struct dumb_device *dev;

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

static struct cfgi *dumb_getcfg(struct diag_l0_device *dl0d) {
	struct dumb_device *dev;
	if (dl0d == NULL) {
		return diag_pseterr(DIAG_ERR_BADCFG);
	}

	dev = dl0d->l0_int;
	return &dev->port;
}


static int dumb_open(struct diag_l0_device *dl0d, int iProtocol) {
	struct dumb_device *dev;
	int dumbopts;

	assert(dl0d);
	dev = dl0d->l0_int;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "open port %s L1proto %d\n",
		FL, dev->port.val.str, iProtocol);

	/* try to open TTY */
	dev->tty_int = diag_tty_open(dev->port.val.str);
	if (dev->tty_int == NULL) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dev->protocol = iProtocol;

	/* parse dumbopts to set flags */
	dumbopts = dev->dumbopts.val.i;

	dev->use_L = dumbopts & USE_LLINE;
	dev->clr_dtr = dumbopts & CLEAR_DTR;
	dev->set_rts = dumbopts & SET_RTS;
	dev->man_break = dumbopts & MAN_BREAK;
	dev->lline_inv = dumbopts & LLINE_INV;
	dev->fast_break = dumbopts & FAST_BREAK;
	dev->blockduplex = dumbopts & BLOCKDUPLEX;

	/*
	 * We set RTS to low, and DTR high, because this allows some
	 * interfaces to work than need power from the DTR/RTS lines;
	 * this is altered according to dumbopts.
	 */
	if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts) < 0) {
		dumb_close(dl0d);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	(void)diag_tty_iflush(dev->tty_int);	/* Flush unread input */

	dl0d->opened = 1;

	return 0;
}

//dumb_close : close TTY handle.
static void
dumb_close(struct diag_l0_device *dl0d) {
	if (!dl0d) {
		return;
	}

	struct dumb_device *dev = dl0d->l0_int;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		FLFMT "l0 link %p closing\n", FL, (void *)dl0d);

	diag_tty_close(dev->tty_int);
	dev->tty_int = NULL;
	dl0d->opened = 0;

	return;
}

/*
 * Fastinit: ISO14230-2 sec 5.2.4.2.3
 * Caller should have waited W5 (>300ms) before calling this (from _initbus only!)
 * we assume the L line was at the correct state (1) during that time.
 * returns 0 (success), 50ms after starting the wake-up pattern.
 * Exceptionally we dont diag_iseterr on return since _initbus() takes care of that.
 */
static int
dumb_fastinit(struct diag_l0_device *dl0d) {
	struct dumb_device *dev = dl0d->l0_int;
	int rv=0;
	uint8_t cbuf[MAXRBUF];

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
		FLFMT "dl0d=%p fastinit\n", FL, (void *)dl0d);

	//Tidle before break : W5 (>300ms) on poweron; P3 (>55ms) after a StopCommunication; or 0ms after a P3 timeout.
	// We assume the caller took care of this.
	/* Send 25/25 ms break as initialisation pattern (TiniL) */
	//ISO14230-2 says we should send the same sync pattern on both L and K together.
	// we do it almost perfectly; the L \_/ pulse starts before and ends after the K \_/ pulse.

	if (dev->use_L) {
		// do K+L only if the user wants to do both
		if (dev->fast_break) {
			//but we can't use diag_tty_fastbreak while doing the L-line.
			fprintf(stderr, FLFMT "Warning : not using L line for FAST_BREAK.\n", FL);
			rv=diag_tty_fastbreak(dev->tty_int, 50-WUPFLUSH);
		} else {
			unsigned long long tb0,tb1;	//WUP adjustment
			//normal fast break on K and L.
			//note : if LLINE_INV is 1, then we need to clear RTS to pull L down !
			rv = diag_tty_control(dev->tty_int, !(dev->clr_dtr), !(dev->lline_inv));
			if (rv < 0) {
				fprintf(stderr, FLFMT "fastinit: Failed to set L\\_\n", FL);
				return diag_ifwderr(rv);
			}
			tb0 = diag_os_gethrt();
			rv=diag_tty_break(dev->tty_int, 25);	//K line low for 25ms
				/* Now restore DTR/RTS */
			if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts) < 0) {
				fprintf(stderr, FLFMT "fastinit: Failed to restore DTR & RTS!\n",
					FL);
			}
			tb1 = diag_os_gethrt() - tb0;
			tb1 = diag_os_hrtus(tb1)/1000;	//elapsed ms so far within tWUP
			tb1 = (50 - WUPFLUSH) - tb1;	//remaining time in WUP
			if (tb1 > 25) {
				return diag_iseterr(DIAG_ERR_GENERAL); // should never happen
			}
			diag_os_millisleep((unsigned int) tb1);
		}	//if FAST_BREAK
	} else {
		// do K line only
		if (dev->fast_break) {
			rv=diag_tty_fastbreak(dev->tty_int, 50-WUPFLUSH);
		} else {
			//normal break
			unsigned long long tb0,tb1;	//WUP adjustment
			tb0 = diag_os_gethrt();
			rv=diag_tty_break(dev->tty_int, 25);	//K line low for 25ms

			tb1 = diag_os_gethrt() - tb0;
			tb1 = diag_os_hrtus(tb1)/1000;	//elapsed ms so far within tWUP
			tb1 = (50 - WUPFLUSH) - tb1;	//remaining time in WUP
			if (tb1 > 25) {
				return diag_iseterr(DIAG_ERR_GENERAL); // should never happen
			}
			diag_os_millisleep((unsigned int) tb1);
		}
	}	//if USE_LLINE
	// here we have WUPFLUSH ms before tWUP is done; we use this
	// short time to flush RX buffers. (L2 needs to send a StartComm
	// request very soon.)

	diag_tty_read(dev->tty_int, cbuf, sizeof(cbuf), WUPFLUSH);


	//there may have been a problem in diag_tty_break, if so :
	if (rv) {
		fprintf(stderr, FLFMT " L0 fastinit : problem !\n", FL);
		return diag_ifwderr(rv);
	}
	return 0;
}


/* Do the 5 BAUD L line stuff while the K line is twiddling */
// Only called from slowinit if USE_LLINE && !MAN_BREAK.
// Caller must have waited before calling; DTR and RTS should be set correctly beforehand.
// Returns after stop bit is finished + time for diag_tty_read to flush echo. (max 20ms) (NOT the sync byte!)

static void
dumb_Lline(struct diag_l0_device *dl0d, uint8_t ecuaddr) {
	/*
	 * The bus has been high for w0 ms already, now send the
	 * 8 bit ecuaddr at 5 baud LSB first
	 *
	 */
	int i, rv=0;
	struct dumb_device *dev = dl0d->l0_int;
//	uint8_t cbuf[10];

	// We also toggle DTR to disable RXD (blocking it at logical 1).
	// However, at least one system I tested doesn't react well to
	// DTR-toggling.
	/* Set RTS low, for 200ms (Start bit) */
	if (diag_tty_control(dev->tty_int, (dev->clr_dtr), !(dev->lline_inv)) < 0) {
		fprintf(stderr, FLFMT "_LLine: Failed to set DTR & RTS\n", FL);
		return;
	}
	diag_os_millisleep(BPS_PERIOD);		/* 200ms -5% */

	for (i=0; i<8; i++) {
		if (ecuaddr & (1<<i)) {
			/* High */
			rv |= diag_tty_control(dev->tty_int, (dev->clr_dtr), (dev->lline_inv));
		} else {
			/* Low */
			rv |= diag_tty_control(dev->tty_int, (dev->clr_dtr), !(dev->lline_inv));
		}

		if (rv < 0) {
			fprintf(stderr, FLFMT "_LLine: Failed to set DTR & RTS\n",
				FL);
			return;
		}
		diag_os_millisleep(BPS_PERIOD);		/* 200ms -5% */
	}
	/* And set high for the stop bit */
	if (diag_tty_control(dev->tty_int, (dev->clr_dtr), !(dev->lline_inv)) < 0) {
		fprintf(stderr, FLFMT "_LLine: Failed to set DTR & RTS\n",
			FL);
		return;
	}
	diag_os_millisleep(BPS_PERIOD);		/* 200ms -5% */

	/* Now put DTR/RTS back correctly so RX side is enabled */
	if (diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts) < 0) {
		fprintf(stderr, FLFMT "_LLine: Failed to restore DTR & RTS\n",
			FL);
	}

	/* And clear out the break XXX no, _slowinit will do this for us after calling dumb_Lline*/
//	diag_tty_read(dev->tty_int, cbuf, sizeof(cbuf), 20);

	return;
}

/*
 * Slowinit:
 *	We need to send a byte (the address) at 5 baud, then
 *	switch back to 10400 baud
 *	and then wait W1 (60-300ms) until we get Sync byte 0x55.
 * Caller (in L2 typically) must have waited with bus=idle beforehand.
 * This optionally does the L_line stuff according to dumbopts.
 * Ideally returns 0 (success) immediately after receiving Sync byte.
 * This one, like fastinit, doesnt diag_iseterr when returning errors
 * since _initbus() takes care of that.
 *
 */
static int
dumb_slowinit(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in,
	struct dumb_device *dev) {
	uint8_t cbuf[10];
	int rv;
	unsigned int tout;
	struct diag_serial_settings set;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
		FLFMT "slowinit dl0d=%p address 0x%X\n", FL, (void *)dl0d, in->addr);


	//two methods of sending at 5bps. Most USB-serial converts don't support such a slow bitrate !
	if (dev->man_break) {
		//MAN_BREAK means we bitbang 5bps init on K and optionally L as well.

		//send the byte at in->addr, bit by bit.
		int bitcounter;
		uint8_t tempbyte=in->addr;
		bool curbit = 0;	//startbit
		for (bitcounter=0; bitcounter<=8; bitcounter++) {
			//LSB first.
			if (curbit) {
				if (dev->use_L) {
						//release L
						diag_tty_control(dev->tty_int, !(dev->clr_dtr), (dev->lline_inv));
				}
				diag_os_millisleep(BPS_PERIOD);
			} else {
				unsigned int lowtime=BPS_PERIOD;
				//to prevent spurious breaks if we have a sequence of 0's :
				//this is an RLE of sorts...
				for (; bitcounter <=7; bitcounter++) {
					if (tempbyte &
					    1) { // test bit 1; we just tested
						 // curbit before getting here.
						break;
					}
					lowtime += BPS_PERIOD;
					curbit = tempbyte & 1;
					tempbyte = tempbyte >>1;
				}
				//this way, we know for sure the next bit is 1 (either bit 7==1 or stopbit==1 !)
				if (dev->use_L) {
					//L = 0
					diag_tty_control(dev->tty_int, !(dev->clr_dtr), !(dev->lline_inv));
				}
				diag_tty_break(dev->tty_int, lowtime);
			}
			curbit = tempbyte & 1;
			tempbyte = tempbyte >>1;
		}
		//at this point we just finished the last bit, we'll wait the duration of the stop bit.
		if (dev->use_L) {
			diag_tty_control(dev->tty_int, !(dev->clr_dtr), dev->set_rts);	//release L
		}
		diag_os_millisleep(BPS_PERIOD);	//stop bit

		//at this point the stop bit just finished. We could just purge the input buffer ?
		//Usually the next thing to happen is the ECU will send the sync byte (0x55) within W1
		// (60 to 300ms)
		// so we have 60ms to diag_tty_iflush, the risk is if it takes too long
		// the "sync pattern byte" may be lost !
		// TODO : add before+after timing check to see if diag_tty_iflush takes too long
		diag_tty_iflush(dev->tty_int);	//try it anyway
	} else {	//so it's not a man_break
		/* Set to 5 baud, 8 N 1 */

		set.speed = 5;
		set.databits = diag_databits_8;
		set.stopbits = diag_stopbits_1;
		set.parflag = diag_par_n;

		diag_tty_setup(dev->tty_int, &set);


		/* Send the address as a single byte message */
		diag_tty_write(dev->tty_int, &in->addr, 1);
		//This supposes that diag_tty_write is non-blocking, i.e. returns before write is complete !
		// 8 bits + start bit + stop bit @ 5bps = 2000 ms
		/* Do the L line stuff as required*/
		if (dev->use_L) {
			dumb_Lline(dl0d, in->addr);
			tout=400;	//Shorter timeout to get back echo, as dumb_Lline exits after 5bps stopbit.
		} else {
			// If there's no manual L line to do, timeout needs to be longer to receive echo
			tout=2400;
		}
		//TODO : try with diag_tty_iflush instead of trying to read a single echo byte?
		//I expect the RX buffer might have more than 1 spurious byte in it at this point...
		/*
		 * And read back the single byte echo, which shows TX completes
		 * - At 5 baud, it takes 2 seconds to send a byte ..
		 * - ECU responds within W1 = [60,300]ms after stop bit.
		 * We're not trying to get the sync byte yet, just the address byte
		 * echo
		 */

		if (diag_tty_read(dev->tty_int, cbuf, 1,tout) != 1) {
			fprintf(stderr, FLFMT "_slowinit: address echo error\n", FL);
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		}

		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
			FLFMT "\tgot address echo 0x%X\n", FL, cbuf[0]);

		rv = diag_tty_setup(dev->tty_int, &dev->serial);
		if (rv) {
			//reset original settings
			fprintf(stderr, FLFMT "_slowinit: could not reset serial settings !\n", FL);
			return diag_ifwderr(rv);
		}
	}	//if !man_break
	//Here, we sent 0x33 @ 5bps and read back the echo or purged it.
	diag_os_millisleep(60-IFLUSH_TIMEOUT);		//W1 minimum, less the time we spend in diag_tty_iflush
	//Now the ECU is about to, or already, sending the sync byte 0x55.
	/*
	 * Ideally we would now measure the length of the received
	 * 0x55 sync byte to work out the baud rate.
	 * not implemented, we'll just suppose the current serial settings
	 * are OK and read the 0x55.
	 * This is ok for iso9141, but in 14230 (5.2.4.2.2.2) we must detect
	 * between 1200 and 10400bps. One technique that *could* be tried
	 * (and implemented in diag_tty*.c) would be to set to a high baud
	 * rate (ideally > 10400*10, like 115.2k) and detect the incoming
	 * RXD_BREAK conditions as they come in
	 * (1 bit @ 10400 > complete byte @ 115200), and record + analyse
	 * timestamps for every RXD_BREAK falling edge. Windows has an
	 * EV_BREAK event for this; linux/unix may be more complex.
	 * Let's just forget about
	 * all this for now.
	 */

	if (dev->protocol == DIAG_L1_ISO9141) {
		tout = 241+50;		/* maximum W1 + sync byte@10kbps - elapsed (60ms) + mud factor= 241ms + mud factor*/
	} else if (dev->protocol== DIAG_L1_ISO14230) {
		// probably ISO14230-2 sec 5.2.4.2.2
		tout = 300;		/* It should be the same thing, but let's put 300. */
	} else {
		fprintf(stderr, FLFMT "warning : using Slowinit with a strange L1 protocol !\n", FL);
		tout=300;	//but try anyway
	}

	rv = diag_tty_read(dev->tty_int, cbuf, 1, tout);
	if (rv <= 0) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
			FLFMT "\tdid not get Sync byte !\n", FL);

		return diag_iseterr(DIAG_ERR_TIMEOUT);
	}
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
		FLFMT "\tgot sync byte 0x%X!\n", FL, cbuf[0]);

	//If all's well at this point, we just read the sync pattern byte. L2 will take care
	//of reading + echoing the keybytes
	return 0;
}

/*
 * Do wakeup on the bus
 * return 0 on success, after reading of a sync byte, before receiving any keyword.
 * since at the L0 level we have no knowledge of 9141 / 14230, the caller is
 * responsible for waiting W5, W0 or Tidle before calling initbus().
 */
static int
dumb_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in) {
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct dumb_device *dev;

	dev = (struct dumb_device *)dl0d->l0_int;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "device link %p info %p initbus type %d\n", FL,
		(void *)dl0d, (void *)dev, in->type);

	if (!dev) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	(void)diag_tty_iflush(dev->tty_int);	/* Flush unread input */

	switch (in->type) {
		case DIAG_L1_INITBUS_FAST:
			rv = dumb_fastinit(dl0d);
			break;
		case DIAG_L1_INITBUS_5BAUD:
			rv = dumb_slowinit(dl0d, in, dev);
			break;
		case DIAG_L1_INITBUS_2SLOW:
			// iso 9141 - 1989 style init, not implemented.
		default:
			return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);
			break;
	}


	if (rv) {
		fprintf(stderr, FLFMT "L0 initbus failed (%s)\n", FL, diag_errlookup(rv));
		return diag_ifwderr(rv);
	}

	return 0;

}


static int dumb_iflush(struct diag_l0_device *dl0d) {
	struct dumb_device *dev = dl0d->l0_int;

	return diag_tty_iflush(dev->tty_int);
}

/*
 * Send a load of data
 * this is "blocking", i.e. returns only when it's finished or it failed.
 *
 * Returns 0 on success
 */

static int
dumb_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len) {
	/*
	 * This will be called byte at a time unless P4 timing parameter is zero
	 * as the L1 code that called this will be adding the P4 gap between
	 * bytes
	 */
	int rv;
	struct dumb_device *dev = dl0d->l0_int;

	if (len <= 0) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V, data, len,
		FLFMT "l0_send dl0d=%p len=%ld; ", FL, (void *)dl0d, (long)len);

	if ((rv = diag_tty_write(dev->tty_int, data, len)) != (int) len) {
		fprintf(stderr, FLFMT "dumb_send: write error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;
}

/*
 * Get data (blocking), returns number of bytes read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 * returns # of bytes read if succesful
 */

static int
dumb_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
void *data, size_t len, unsigned int timeout) {
	int rv;
	struct dumb_device *dev = dl0d->l0_int;

	if (len <= 0) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "_recv dl0d=%p req=%ld bytes timeout=%u\n", FL,
		(void *)dl0d, (long)len, timeout);

	if ((rv=diag_tty_read(dev->tty_int, data, len, timeout)) <= 0) {
		if (rv == DIAG_ERR_TIMEOUT) {
			return DIAG_ERR_TIMEOUT;
		}
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		data, (size_t) rv, FLFMT "Got ", FL);

	return rv;
}

/*
 * Set speed/parity etc
 * ret 0 if ok
 */
static int
dumb_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pset) {
	struct dumb_device *dev;

	dev = (struct dumb_device *)dl0d->l0_int;

	dev->serial = *pset;

	return diag_tty_setup(dev->tty_int, &dev->serial);
}


static uint32_t
dumb_getflags(struct diag_l0_device *dl0d) {
	struct dumb_device *dev;
	int flags=0;

	dev = (struct dumb_device *)dl0d->l0_int;

	if (dev->blockduplex) {
		flags |= DIAG_L1_BLOCKDUPLEX;
	}

	switch (dev->protocol) {
	case DIAG_L1_ISO14230:
		flags |= DIAG_L1_FAST | DIAG_L1_PREFFAST | DIAG_L1_SLOW | DIAG_L1_HALFDUPLEX;
		break;
	case DIAG_L1_ISO9141:
		flags |= DIAG_L1_SLOW | DIAG_L1_HALFDUPLEX;
		break;
	default:
		break;
	}

	return flags;
}

static int dumb_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	int rv = 0;

	switch (cmd) {
	case DIAG_IOCTL_SETSPEED:
		rv = dumb_setspeed(dl0d, (const struct diag_serial_settings *) data);
		break;
	case DIAG_IOCTL_INITBUS:
		rv = dumb_initbus(dl0d, (struct diag_l1_initbus_args *)data);
		break;
	case DIAG_IOCTL_IFLUSH:
		rv = dumb_iflush(dl0d);
		break;
	default:
		rv = DIAG_ERR_IOCTL_NOTSUPP;
		break;
	}

	return rv;
}


const struct diag_l0 diag_l0_dumb = {
 	"Generic dumb serial interface",
	"DUMB",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	dumb_init,
	dumb_new,
	dumb_getcfg,
	dumb_del,
	dumb_open,
	dumb_close,
	dumb_getflags,
	dumb_recv,
	dumb_send,
	dumb_ioctl,
};
