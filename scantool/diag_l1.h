#ifndef _DIAG_L1_H_
#define _DIAG_L1_H_

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
 * Diagnostic Software
 *
 * - Layer 1 interface definitions
 */

#include "diag.h"		//we need this for uint8_t
#include "diag_tty.h"	//for structs serial_settings and diag_l0_device


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
 *	L1 is intelligent and does L2 stuff, this means it will
 *	- Return a complete L3 frame of data as one recv()
 *	- Expect complete L3 data to be sent to it, with the address header
 *	in one write,
 * DOESL2FRAME: interface expects and returns full L2 frames XXX
 */
#define	DIAG_L1_DOESL2FRAME		0x20
/*
 * DOESSLOWINIT
 *	L1 interface does the slowinit stuff, so L2 doesn't need to do complex
 *	handshake. L1 will send the keybytes on the first recv(). (All L1's
 *	read the 0x55 and do the right thing, L2 never sees that) See DIAG_L1_DOESFULLINIT
 */
#define	DIAG_L1_DOESSLOWINIT		0x40
/*
 * DOESL2CKSUM
 *	L1 interface does the L2 checksum/CRC on send
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
 * used for struct diag_l0 (diag_l0_type)
 */
#define	DIAG_L1_ISO9141		0x01	/* K line */
#define	DIAG_L1_ISO14230	0x02	/* K line,XXX not sure of the difference with 9141 at this level */
#define DIAG_L1_J1850_VPW	0x04	/* J1850 interface, 10400 baud, VPW */
#define DIAG_L1_J1850_PWM	0x08	/* J1850 interface 41600 baud, PWM */
#define	DIAG_L1_CAN		0x10	/* CAN bus */
#define DIAG_L1_RES1 0x20	/* Reserved */
#define DIAG_L1_RES2 0x40	/* Reserved */
#define	DIAG_L1_RAW		0x80	/* Raw data interface */

/*
 * Number of concurrently supported logical interfaces
 * remember a single physical interface may be many logical interfaces
 * if it has K, CAN, etc in one device
 *
 * For interfaces with muxes (such as to talk to a MB 33 way diagnostic port)
 * the subinterface flag is used for read and write purposes
 * XXX what's an MB 33 way diagnostic port ?!
 *
 * This makes "un-duplexing" a half duplex interface hard work...
 *  and not yet supported in this code
 */
//#define DIAG_L1_MAXINTF		16


/*
 * L2 -> L1 interface
 *
 * L1 public interface
 */

/* Argument to diag_l1_initbus */
struct diag_l1_initbus_args
{
	uint8_t	type;	/* Init type */
	uint8_t	addr;	/* ECU (target) address, if iso9141 or 14230 init */
	uint8_t	testerid;	/* tester address, for 14230 init */
	uint8_t	physaddr;	//1:physical addressing, 0: func. iso14230 only.
};
//initbus types:
#define DIAG_L1_INITBUS_NONE	0	/* Not needed */
#define DIAG_L1_INITBUS_FAST	1	/* Fast init (25ms low, 25ms high) */
#define DIAG_L1_INITBUS_5BAUD	2	/* 5 baud init */
#define DIAG_L1_INITBUS_2SLOW	3	/* 2 second low on bus, ISO9141-1989 style ? */



// diag_l0 : every diag_l0_???.c "driver" fills in one of these to describe itself.
struct diag_l0
{
	const char	*diag_l0_textname;	/* Useful textual name, unused at the moment */
	const char	*diag_l0_name;	/* Short, unique text name for user interface */

	int 	diag_l0_type;			/* supported L1protocols, defined above*/

	/* function pointers to L0 code */
	int	(*diag_l0_init)(void);
	struct diag_l0_device *(*diag_l0_open)(const char *subinterface,
		int iProtocol);
	int	(*diag_l0_close)(struct diag_l0_device **);
	int	(*diag_l0_initbus)(struct diag_l0_device *,
		struct diag_l1_initbus_args *in);
	//diag_l0_send: return 0 on success
	int	(*diag_l0_send)(struct diag_l0_device *,
		const char *subinterface, const void *data, size_t len);
	//diag_l0_recv: ret # of bytes read
	int	(*diag_l0_recv)(struct diag_l0_device *,
		const char *subinterface, void *data, size_t len, int timeout);
	int	(*diag_l0_setspeed)(struct diag_l0_device *,
		const struct diag_serial_settings *pss);
	uint32_t	(*diag_l0_getflags)(struct diag_l0_device *);
};



//********** Public L1 interface
//diag_l1_init : parse through the l0dev_list linked list
//and call diag_l0_init for each of them. returns 0 on success (always succeeds)
// must not be used to allocate memory or open handles !
int diag_l1_init(void);
//diag_l1_end : opposite of diag_l1_init . does nothing for now
int diag_l1_end(void);

//diag_l1_initbus : calls ->diag_l0_initbus. Must return as soon as possible,
//and restore original port settings (speed, etc). Ret 0 if ok
int diag_l1_initbus(struct diag_l0_device *, struct diag_l1_initbus_args *in);

//diag_l1_open : calls diag_l0_open with the specified L1 protocol; returns a
// *diag_l0_device on success, 0 on failure (pseterr).
//	Often calls diag_tty_open; alloc + fill + return a new diag_l0_device
//	struct if succesful.
struct diag_l0_device *diag_l1_open(const char *name, const char *subinterface, int L1protocol);
//diag_l1_close : calls diag_l0_close as required; always succeeds and returns 0. Close & free everything.
int diag_l1_close(struct diag_l0_device **);

//diag_l1_send : send data, ret 0 if ok, <0 failed
int diag_l1_send(struct diag_l0_device *, const char *subinterface, const void *data, size_t len, unsigned int p4);
//diag_l1_recv : return # of bytes read, DIAG_ERR_TIMEOUT or error if failed. DIAG_ERR_TIMEOUT is not a hard failure
//since a lot of L2 code uses this to detect end of responses
int diag_l1_recv(struct diag_l0_device *, const char *subinterface, void *data, size_t len, int timeout);

//diag_l1_setspeed: returns 0 on success,  speed = speed, bits = data bits (5,6,7,8)
 //	 stopbits (1, 2), parflag as above
int diag_l1_setspeed(struct diag_l0_device *dl0d,
	const struct diag_serial_settings *pset);

//getflags, gettype: get flags and type as defined above
uint32_t diag_l1_getflags(struct diag_l0_device *);
int diag_l1_gettype(struct diag_l0_device *);
//**********

//diag_l1_add_l0dev : only called from l0 drivers to add themselves to the l0 device
//linked-list.
int diag_l1_add_l0dev(const struct diag_l0 *l0dev);

extern int diag_l1_debug;	//L1 debug flags (see diag.h)

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L1_H_ */
