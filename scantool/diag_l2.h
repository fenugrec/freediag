#ifndef _DIAG_L2_H_
#define _DIAG_L2_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id$
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
 * - Layer 2 interface definitions
 */

/*
 * Structure of definitions of L2 types supported
 */

/*
 * Links down to layer 1, there will only be one link per protocol per device,
 * may be many connections (defined in diag_l2.h) per link
 */
#if defined(__cplusplus)
extern "C" {
#endif

//diag_l2_link : elements of the diag_l2_links linked-list.
//An l2 link associates an existing diag_l0_device with
//one L1 proto and L1 flags.
struct diag_l2_link
{
	struct diag_l0_device * 	diag_l2_dl0d;	/* Link we're using to talk to lower layer */
	int	diag_l2_l1protocol;		/* L1 protocol used*/

	char	diag_l2_name[DIAG_NAMELEN];	/* XXX this is set the l0 driver shortname !? */

	int	diag_l2_l1flags;		/* L1 flags, filled with diag_l1_getflags */
	int	diag_l2_l1type;			/* L1 type (see diag_l1.h): filled with diag_l1_gettype*/

	struct diag_l2_link *next;		/* linked list of all connections */
	struct diag_l2_link *l1_next;		/* linked list of all ECUs with same ID on different interfaces */
	struct diag_l2_link *l1_prev;		/* prev to make list removal easy */
};

struct diag_msg;

/*
 * A structure to represent a link to an ECU from here
 * There is one of these per ECU we are talking to - we may be talking to
 * more than one ECU per L1 link
 */
struct diag_l2_conn
{
	uint8_t	diag_l2_state;		/* State of this */

	struct diag_l2_link *diag_link;		/* info about L1 connection */

	struct timeval	diag_l2_lastsend;	/* Time we sent last message */
	struct timeval	diag_l2_expiry;		/* When it expires */

	const struct diag_l2_proto *l2proto;	/* Protocol handlers */

	uint32_t diag_l2_type;		/* Type info for this L2 connection */

	// Message timing values.
	// See SAE-J1979 for general usage;
	// See ISO-14230-2, ISO-9141-2, SAE-J1850 for specific values;
	uint16_t	diag_l2_p1min;
	uint16_t	diag_l2_p1max; // p1 = byte gap from ECU;
	uint16_t	diag_l2_p2min;
	uint16_t	diag_l2_p2max; // p2 = gap from request to response;
	uint16_t	diag_l2_p2emin;
	uint16_t	diag_l2_p2emax; // p2 in extended mode (ISO14230 "rspPending");
	uint16_t	diag_l2_p3min;
	uint16_t	diag_l2_p3max; // p3 = gap from response to new request;
	uint16_t	diag_l2_p4min;
	uint16_t	diag_l2_p4max; // p4 = byte gap from tester.

	/* Protocol independent data */
	void	*diag_l2_proto_data;

	/* Speed we're using (baud) */
	unsigned int		diag_l2_speed;

	/*
	 * Physical ECU address (useful if doing logical addressing
	 * only useful if only one responder, should use addressing
	 * from received messages only - some of this may belong in
	 * protocol specific l2 info not in this general structure.
	 */
	uint8_t	diag_l2_physaddr;

	uint8_t	diag_l2_destaddr;	/* Dest (ECU) */
	uint8_t	diag_l2_srcaddr;	/* Source (US) */
	/*
	 * The following two should be in the protocol specific bit
	 * because they only apply to ISO protocols (move at some time
	 * unless they are useful in other protocols, but I haven't
	 * written any other protocols to find out ;-))
	 */
	uint8_t	diag_l2_kb1;	/* KB 1, (ISO stuff really) */
	uint8_t	diag_l2_kb2;	/* KB 2, (ISO stuff really) */


	/* Main list of all connections */
	struct diag_l2_conn *next;

	/* List of connections per L1 interface */
	struct diag_l2_conn *diag_l2_next;
	struct diag_l2_conn *diag_l2_prev;

	/* Generic receive buffer */
	uint8_t	rxbuf[MAXRBUF];
	int		rxoffset;

	/* Generic 'msg' holder */
	struct diag_msg	*diag_msg;

};

// Special Timeout for so-called "Smart" interfaces;
// Slower than any protocol, give them time to unframe
// and checksum the data:
#define SMART_TIMEOUT 150

/*
 * Default ISO 14230 timing values, ms, used as defaults for L2 timeouts
 */
#define ISO_14230_TIM_MIN_P1	0	/* Inter byte timing in ECU response */
#define ISO_14230_TIM_MAX_P1	20
#define ISO_14230_TIM_MIN_P2	25	/* Time between end of tester request and start of ECU response */
#define ISO_14230_TIM_MAX_P2	50	/* or between ECU responses */
#define ISO_14230_TIM_MIN_P2E	25	/* Extended mode for "rspPending" */
#define ISO_14230_TIM_MAX_P2E	5000	/* Extended mode for "rspPending" */
#define ISO_14230_TIM_MIN_P3	55	/* Time between end of ECU response and start of new tester request */
#define ISO_14230_TIM_MAX_P3	5000	/* or time between end of tester request and start of new request if ECU
						doesn't respond */
#define ISO_14230_TIM_MIN_P4	5	/* Inter byte time in tester request */
#define ISO_14230_TIM_MAX_P4	20


/*
 * Application Interface
 */

/*
 * Operational L2 protocols; for struct diag_l2_proto->diag_l2_protocol
 *
 * NOTE, many of these protocols run on each others physical layer,
 * for instance J1850 runs on J1850/ISO9141/ISO14230 interfaces
  */
#define DIAG_L2_PROT_RAW	0	/* Raw send/receive, ie. L2 pass thru */
#define DIAG_L2_PROT_ISO9141	1	/* Iso 9141, keywords 08 08 */
#define DIAG_L2_PROT_NOTUSED	2	/* NOT USED */
#define DIAG_L2_PROT_ISO14230	3	/* Iso 14230 using appropriate message format */
#define DIAG_L2_PROT_SAEJ1850	4	/* SAEJ1850 */
#define DIAG_L2_PROT_CAN	5	/* CAN L2 (is this defined ??) */
#define DIAG_L2_PROT_VAG	6	/* VAG ISO9141 based protocol */
#define DIAG_L2_PROT_MB1	7	/* MB protocol 1 */
#define DIAG_L2_PROT_MB2	8	/* MB protocol 2 */
#define DIAG_L2_PROT_MAX	9	/* Maximum number of protocols */


/*
 * Flags for diag_l2_proto_startcomms; this is not the same
 * as the L2 handler flags (diag_l2_proto->diag_l2_flags)

 * Bits 0/1/2 used to tell what kind of initialisation to be done on the
 * diagnostic bus.
 * TODO: tidy up #defines for binary flags vs int values?
 */
#define DIAG_L2_TYPE_SLOWINIT	0x00		/* Do 5 Baud init */
#define DIAG_L2_TYPE_FASTINIT	0x01		/* Do fast init */
#define DIAG_L2_TYPE_CARBINIT	0x02		/* Do CARB init (see ISO14230-2 5.2.4) */
#define DIAG_L2_TYPE_MONINIT	0x04		/* Don't do any init, just connect to bus */
#define DIAG_L2_TYPE_INITMASK	0x07		/* Init options mask */
/*
 * Bit 3 shows whether the address supplied is a functional or physical
 * address, used for protocols such as ISO14230
 */
#define DIAG_L2_TYPE_FUNCADDR	0x08

/*
 * Bit 4 is a flag to the ISO14230 code to tell it to always use messages
 * with a length byte, this is primarily for SSF14230 - the Swedish vehicle
 * implementation of ISO14230, if not set, the code will use appropriate
 * message types
 */
#define DIAG_L2_TYPE_LONG	0x10

/*
 * Bit 5 is a bit to tell protocols that support both functional and physical
 * addressing to switch to physical addressing after initial communications
 * are established (such as ISO14230)
 */
#define DIAG_L2_TYPE_PHYSCONN	0x20

/*
 * Bit 6 is used to tell the ISO14230 code to use SAE J1978 idle
 * messages (mode 1 PID 0) instead of the ISO "Tester Present"
 * messages for preventing link timeout
 *
 * SAE J1978 is the ODB II ScanTool specification document
 */
#define DIAG_L2_IDLE_J1978	0x40


/* Used for L2_IOCTL_GETDATA */
struct	diag_l2_data
{
	uint8_t physaddr;	/* Physical address of ECU */
	uint8_t kb1;		/* Keybyte 0 */
	uint8_t kb2;		/* Keybyte 1 */
};


/*
 * L2 flags returned from GET_L2_FLAGS
 * ( for struct diag_l2_proto->diag_l2_flags )
 */

/*
 * Received data is sent upwards in frames (ie L3 doesn't have to try
 * and re-frame the code - this is done by timing windows or by the
 * protocol itself
 */
#define DIAG_L2_FLAG_FRAMED	0x01
/*
 * L2 interface assumes addressing is in header not data, and that
 * it must calculate the checksum
 */
#define DIAG_L2_FLAG_DATA_ONLY	0x02
/*
 * L2 does keep alive to ECU
 */
#define DIAG_L2_FLAG_KEEPALIVE	0x04
/*
 * L2 adds checksum
 */
#define DIAG_L2_FLAG_DOESCKSUM	0x08
/*
 * L2 startcomms() always suceeds, but only way to find out if
 * a connection really exists is to send some data and wait for
 * response. This is useful when connected to car networks such as CAN or J1850
 * networks as oppose to normal diagnostic type interface such as ISO9141
 */
#define DIAG_L2_FLAG_CONNECTS_ALWAYS 0x10



/*
 * Internal interface
 */
/* Add a msg to a L2 connection */
void diag_l2_addmsg(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg);

/*
 * Public interface
 *
 * l2_Init()
 *	use:	call at start time to let layer 2 initialise itself
 *
 * l2_Open()
 *	use:	opens up a layer 1 device for usage, setting speed etc etc
 *		as requested.
	params:	device - device name to open
		id - device sub-ID
 *		L1protocol - Layer 1 protocol to use (Hardware layer)
 * l2_Close()
 *	use:	closes a L1 device -
 *
 * l2_startCommunication()
 *	use: 	starts up a session between here and an ECU
 *	params:	fd - l1 file descriptor, obtained from l2 open()
 *		L2protocol - see above - Type of L2 session this is (ISO14230,
 *				OBDII etc etc)
 *		type - see above
 *		bitrate - bit rate to use (bps) for normal communication
 *		target - target initialisation address
 *		source - source initialisation address
 *		rcv_call_back	L3 Routine to call with L2 messages as received
 * 	returns: struct diag_l2_conn for representing the connection
 *
 * l2_stopCommunications()
 *	use:	stop talking to an ECU
 *
 * AccessTimingParameters()
 *	use:	change access timing parameter defaults
 *	params:	connection - the connection
 *
 *
 * send()
 *	use:	send a message
 *	params: connection  - The connection info, obtained from
 *			diag_l2_StartCommunication() routine
 *		msg	The message to send - NOTE, the source address MAY
 *			be ignored, the one specified to StartCommunications
 *			is used for certain L2 protocols
 *	returns: 0 on success, diag error number (<0) on error
 *
 * recv()
 *	use:	checks if there's anything to receive (and sleeps)
 *		should use select on the fd, and call this when it's ready
 *	params: connection  - The connection info, obtained from
 *			diag_l2_StartCommunication() routine
 *		timeout - approximate timeout in milliseconds
 *		callback Callback routine
 *		handle	Handle passed to callback routine
 *	returns: 0 always  or diag error on error
 *
 * msg()
 *	use:	send a message and wait for a response.
 *		this is important because some L2 protocols such as ISO
 *		will return a "request not yet complete", and this deals
 *		with that .. it also deals with getting the timeouts and
 *		everything else correct.
 *		It is also coded to return the received message, rather than
 *		use callbacks, so it's a different type of API to send/recv
 *	params:	connection	- The L2 connection info
 *		msg		- The message to send
 *		*errval		- Place for error to be stored
 *
 *	returns: msg or NULL
 *
 * ioctl()
 *	use:	allows l3 to manipulate l1 info (speed, flags etc)
 *		mainly for raw interface
 *	params: connection  - The connection info, obtained from
 *			diag_l2_StartCommunication() routine
 *		command	- the thing to do
 *		data	- where to get (or put) the data
 *	returns: 0 if OK, diag error num (<0) on error
 *
 */
int diag_l2_init(void);
int diag_l2_end(void);
struct diag_l0_device * diag_l2_open(const char *device_name, const char *subinterface, int L1protocol);
int diag_l2_close(struct diag_l0_device *);

struct diag_l2_conn * diag_l2_StartCommunications(struct diag_l0_device *, int L2protocol,
	uint32_t type, unsigned int bitrate, target_type target, source_type source );

int diag_l2_StopCommunications(struct diag_l2_conn *);

int diag_l2_send(struct diag_l2_conn *connection, struct diag_msg *msg);
void diag_l2_sendstamp(struct diag_l2_conn *d_l2_conn);

int diag_l2_recv(struct diag_l2_conn *connection, int timeout,
	void (* rcv_call_back)(void *, struct diag_msg *), void *handle );

struct diag_msg *diag_l2_request(struct diag_l2_conn *connection, struct diag_msg *msg,
		int *errval);

int diag_l2_ioctl(struct diag_l2_conn *connection, int cmd, void *data);

void diag_l2_timer(void);	/* Regular timer routine */

extern int diag_l2_debug;
extern struct diag_l2_conn  *global_l2_conn;

/*
 * Interface to individual protocols
 * each diag_l2_???.c handler fills in one of these.
 */
struct diag_l2_proto {
	int diag_l2_protocol;
	int diag_l2_flags;		//see #defines above

	/* Individual L2 routines, see description of interface in diag_l2.h */
	int (*diag_l2_proto_startcomms)(struct diag_l2_conn*,
		flag_type, unsigned int bitrate, target_type, source_type);
	int (*diag_l2_proto_stopcomms)(struct diag_l2_conn*);
	int (*diag_l2_proto_send)(struct diag_l2_conn*, struct diag_msg*);
	int (*diag_l2_proto_recv)(struct diag_l2_conn *d_l2_conn,
		int timeout, void (*callback)(void *handle, struct diag_msg *msg),
		void *handle);
	struct diag_msg * (*diag_l2_proto_request)(struct diag_l2_conn*,
		struct diag_msg*, int*);
	void (*diag_l2_proto_timeout)(struct diag_l2_conn*);
};

int diag_l2_add_protocol(const struct diag_l2_proto *l2proto);


#if defined(__cplusplus)
}
#endif
#endif  /* _DIAG_L2_H_ */
