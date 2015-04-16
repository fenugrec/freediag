#ifndef _DIAG_L2_ISO9141_H_
#define _DIAG_L2_ISO9141_H_
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
 * Diag
 *
 * L2 driver for ISO 9141 and ISO 9141-2 interface
 *
 * NOTE: this is only the startcommunications routine, raw routines are
 * used for read/write
 *
 * ISO9141-2 defines it's message packet as:
 * Header Byte 1 = 0x68 from Tester; 0x48 from ECU;
 * Header Byte 2 = 0x6A from Tester; 0x6B from ECU;
 * Source Addr.  = 0xF1 from Tester; 0x?? from ECU;
 * Data Bytes (1 up to 7);
 * 1 Checksum byte.
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

// Message overhead length (header + checksum):
#define OHLEN_ISO9141 4
// Maximum message length (including overhead):
#define MAXLEN_ISO9141 (OHLEN_ISO9141 + 7)

// Communication Initialization Timings:
#define W0min 2   // w0 = bus high prior to address byte;
#define W1min 60  // w1 = gap from address byte to synch pattern;
#define W1max 300
#define W2min 5   // w2 = gap from synch pattern to keybyte 1;
#define W2max 20
#define W3min 0   // w3 = gap from keybyte 1 to keybyte 2;
#define W3max 20
#define W4min 25  // w4 = gap from keybyte 2 and inversion from tester;
#define W4max 50
#define W5min 300 // w5 = guard time before retransmitting address byte;

/*
 * ISO9141 specific data
 */
struct diag_l2_iso9141
{
	uint8_t srcaddr;	// Src address used, normally 0xF1 (tester)
	uint8_t target;	// Target address used, normally 0x33 (ISO9141)

	// These should be only in specific protocol structs, but
	// someone put them in the generic L2 struct, and there seem to be
	//a lot of protocols that expect them to be there... :(
//	uint8_t kb1;	  // key Byte 1
//	uint8_t kb2;	// key Byte 2

	uint8_t rxbuf[MAXLEN_ISO9141]; // Receive buffer, for building message in.
	uint8_t rxoffset;

	enum {
		STATE_CLOSED=0,
		STATE_CONNECTING=1,
		STATE_ESTABLISHED=2,
	} state;

};


 /* Public Interface */

int diag_l2_iso9141_add(void);



// TODO: why does diag_l2_vag.c call this...
int diag_l2_proto_iso9141_int_recv(struct diag_l2_conn *d_l2_conn, int timeout);


#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_ISO9141_H_ */
