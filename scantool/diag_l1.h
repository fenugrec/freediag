#ifndef _DIAG_L1_H_
#define _DIAG_L1_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * 2009-2015 fenugrec
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
 * Diagnostic Software
 *
 * - Layer 1 interface definitions
 */

#include "diag.h"		//we need this for uint8_t
#include "diag_tty.h"	//for structs serial_settings
#include "diag_cfg.h"
#include "diag_l0.h"


#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Layer 1/0 device flags
 *
 * Each potential Layer 0 logical interface has a number of flags that show
 * what it supports
 *
 * Some of these (like SLOW start) will be needed by certain ECUs , and
 * so the flags are also used in the ECU definitions
 *
 * Most L1 drivers will prefer fast start, hopefully support both
 * fast and slow start
 *
 * Can be read by higher layers using ..._ioctl(GET_L1_FLAGS)
 */


#define DIAG_L1_SLOW		0x01	/* Supports SLOW (5 baud) Start */
#define DIAG_L1_FAST		0x02	/* Supports FAST Start */
#define DIAG_L1_PREFSLOW	0x04	/* Prefers SLOW (5 baud) Start */
#define DIAG_L1_PREFFAST	0x08	/* Prefers FAST Start */
#define DIAG_L1_HALFDUPLEX	0x10	/* Physical interface is half duplex, need to remove echos */

/* following flags are for semi-intelligent interfaces */

/*
 *	L1/L0 is intelligent and does L2 stuff, this means
 *	- l1_recv() splits multiple responses and returns a complete L2 frame;
 *		presence of headers / checksum is determined by other flags below
 *	- l1_send() ignores P4 inter-byte spacing when forwarding the data to L0
 */
#define	DIAG_L1_DOESL2FRAME		0x20
/*
 * DOESSLOWINIT
 *	L1/L0 interface does the slowinit stuff, so L2 doesn't need to do complex
 *	handshake. L1 will send the keybytes on the first recv(). (All L1's
 *	read the 0x55 and do the right thing, L2 never sees that) See DIAG_L1_DOESFULLINIT
 */
#define	DIAG_L1_DOESSLOWINIT		0x40
/*
 * DOESL2CKSUM
 *	L1/L0 interface adds the L2 checksum/CRC on send
 */
#define	DIAG_L1_DOESL2CKSUM		0x80
/*
 * STRIPSL2CKSUM
 *	L1 strips/checks L2 checksum before sending frame upward
 */
#define DIAG_L1_STRIPSL2CKSUM		0x100
/*
 * DOESP4WAIT
 *
 * interface is semi-intelligent and does the interbyte delay P4 for ISO
 * (P4 : inter-byte delay for messages from tester (us) to ECU)
 */
#define DIAG_L1_DOESP4WAIT		0x200

//AUTOSPEED
//interface takes care of setting the baudrate; we check this before
//calling diag_l1_setspeed
#define DIAG_L1_AUTOSPEED	0x400

//NOTTY : specifically for carsim interface. Prevents l2_ioctl
//from calling diag_tty_*
#define DIAG_L1_NOTTY	0x800

//BLOCKDUPLEX
//This tells diag_l1_send() to do half-duplex removal on the whole
//block instead of byte per byte ( if P4=0 ; no interbyte spacing)
#define DIAG_L1_BLOCKDUPLEX 0x1000

//NOHDRS
//this indicates that L1 already stripped the headers from the frame (ELM default behavior)
//but the l0_elm init code enables headers so this is not useful at the moment.
//If NOHDRS, DIAG_L1_DOESL2FRAME must also be set! (otherwise, no hope of splitting messages...)
#define DIAG_L1_NOHDRS 0x2000

//DOESFULLINIT
//indicates that L0 does the full init, including keybyte stuff. (like ELMs)
//this implies that the initbus ioctl still has to be used.
#define DIAG_L1_DOESFULLINIT 0x4000

//DATAONLY
//indicates that L0 adds headers + checksums before sending to ECU (like ELMs).
#define DIAG_L1_DATAONLY 0x8000

//DOESKEEPALIVE
//L0 handles any periodic message required by L2/L3.
#define DIAG_L1_DOESKEEPALIVE 0x10000


/*
 * Layer 0 device types
 *
 * Types of L1 Interface (L1protocol) supported
 * Each "device" has up to 16 interfaces, and many sub-interfaces XXX what ?
 *
 * This is a bitmask of what is supported;
 * used for struct diag_l0 (l1proto_mask)
 */
#define	DIAG_L1_ISO9141		0x01	/* K line */
#define	DIAG_L1_ISO14230	0x02	/* K line, different inits allowed */
#define DIAG_L1_J1850_VPW	0x04	/* J1850 interface, 10400 baud, VPW */
#define DIAG_L1_J1850_PWM	0x08	/* J1850 interface 41600 baud, PWM */
#define	DIAG_L1_CAN		0x10	/* CAN bus */
#define DIAG_L1_RES1 0x20	/* Reserved */
#define DIAG_L1_RES2 0x40	/* Reserved */
#define	DIAG_L1_RAW		0x80	/* Raw data interface */


/*
 * L2 -> L1 interface
 *
 * L1 public interface
 */

/* Argument for DIAG_IOCTL_INITBUS */
struct diag_l1_initbus_args {
	uint8_t	type;	/* Init type */
	uint8_t	addr;	/* ECU (target) address, if iso9141 or 14230 init */
	uint8_t	testerid;	/* tester address, for 14230 init */
	uint8_t	physaddr;	//1:physical addressing, 0: func. iso14230 only.
	uint8_t kb1, kb2;	/* key bytes (return value from L0) */
};
//initbus types:
#define DIAG_L1_INITBUS_NONE	0	/* Not needed */
#define DIAG_L1_INITBUS_FAST	1	/* Fast init (25ms low, 25ms high) */
#define DIAG_L1_INITBUS_5BAUD	2	/* 5 baud init */
#define DIAG_L1_INITBUS_2SLOW	3	/* 2 second low on bus, ISO9141-1989 style ? */

/********** Public L1 interface **********/
/** Parses through the l0dev_list linked list
 * and calls ->init for each of them.
 * @return 0 on success (always succeeds)
 *
 * must not be used to allocate memory or open handles !
 */
int diag_l1_init(void);

/** Opposite of diag_l1_init; does nothing for now */
int diag_l1_end(void);


/** Send IOCTL to L1/L0
 *	@param command : IOCTL #, defined in diag.h
 *	@param data	optional, input/output
 *	@return 0 if OK, diag error num (<0) on error
 */
int diag_l1_ioctl(struct diag_l0_device *, unsigned cmd, void *data);


/** calls l0 ->open with the specified L1 protocol;
* @return 0 if ok
*/
int diag_l1_open(struct diag_l0_device *, int L1protocol);

/** Calls diag_l0_close as required; always succeeds. */
void diag_l1_close(struct diag_l0_device *);

/** Send data.
 * @param p4 : inter-byte spacing (ms), if applicable.
 * @return 0 if ok
 */
int diag_l1_send(struct diag_l0_device *, const void *data, size_t len, unsigned int p4);

/** Receive data.
 *
 * @return # of bytes read, DIAG_ERR_TIMEOUT or \<0 if failed. DIAG_ERR_TIMEOUT is not a hard failure
 * since a lot of L2 code uses this to detect end of responses
 */
int diag_l1_recv(struct diag_l0_device *, void *data, size_t len, unsigned int timeout);

/** Get L0/L1 device flags (defined in diag_l1.h)
 * @return bitmask of L0/L1 flags
 */
uint32_t diag_l1_getflags(struct diag_l0_device *);

/** Get L1 type (supported protocols)
 * @return bitmask of supported L1 protocols
 */
int diag_l1_gettype(struct diag_l0_device *);

/**********/

extern int diag_l1_debug;	//L1 debug flags (see diag.h)

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L1_H_ */
