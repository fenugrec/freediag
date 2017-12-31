#ifndef _DIAG_L2_H_
#define _DIAG_L2_H_

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
struct diag_l2_link {
	struct diag_l0_device *l2_dl0d;	/* Link we're using to talk to lower layer */
	int	l1proto;		/* L1 protocol used; see diag_l1.h*/

	uint32_t	l1flags;		/* L1 flags, filled with diag_l1_getflags in diag_l2_open*/
	int	l1type;			/* L1 type (see diag_l1.h): mask of supported L1 protos. */

	struct diag_l2_link *next;		/* linked list of all connections */

};

struct diag_msg;

/*
 * A structure to represent a link to an ECU from here
 * There is one of these per ECU we are talking to - we may be talking to
 * more than one ECU per L1 link
 */
struct diag_l2_conn {
	enum {
		DIAG_L2_STATE_CLOSED,	/* Not in use (but not free for anyones use !!) */
		DIAG_L2_STATE_SENTCONREQ,	/* Sent connection request (waiting for response/reject) */
		DIAG_L2_STATE_OPEN,	/* Up and running; only legal state for sending a keepalive request */
		DIAG_L2_STATE_CLOSING	/* sending close request (possibly), waiting for response/timeout */
	} diag_l2_state;		/* State: mainly used by the timer code for keepalive msgs.*/



	struct diag_l2_link *diag_link;		/* info about L1 connection */

	//The following two members are used for periodic keep-alive messages.
	//tlast is updated when diag_l2_send, _recv,
	//  _request, or _startcomm is called succesfully.
	unsigned long tlast;		// Time of last received || sent data, in ms.
	unsigned long tinterval;	// How long before expiry (usually set by startcomms() once). Set to -1 for "never"

	const struct diag_l2_proto *l2proto;	/* Protocol handler */

	flag_type diag_l2_type;		/* Type info for this L2 connection;  */
							//will contain init type (slow/mon/fast/etc) and anything else passed
							//in the flag_type flags argument of l2_startcomms. See DIAG_L2_TYPE_*
							// defines below.

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
	uint16_t	diag_l2_p3max; // p3 = gap from end of all responses to new request;
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
	 * XXX Doing that would require protocol-specific ioctl handlers
	 * to handle DIAG_IOCTL_GET_L2_DATA . It could stay in here for
	 * the moment
	 */
	uint8_t	diag_l2_kb1;	/* KB 1, (ISO stuff really) */
	uint8_t	diag_l2_kb2;	/* KB 2, (ISO stuff really) */


	/* Main linked list of all connections */
	struct diag_l2_conn *next;

	/* Generic receive buffer */
	uint8_t	rxbuf[MAXRBUF];
	int		rxoffset;

	/* Generic 'msg' holder */
	struct diag_msg	*diag_msg;

};


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
#define DIAG_L2_PROT_D2		9	/* Volvo D2 over K-line (kw D3 B0) */
#define DIAG_L2_PROT_TEST	10 /* Dummy L2 test driver */
#define DIAG_L2_PROT_MAX	11	/* Maximum number of protocols */

/*
 * l2proto_list : static-allocated list of supported L2 protocols.
 * The last item is a NULL ptr to ease iterating.
 * The array indices don't necessarily match the macros above !
 */

extern const struct diag_l2_proto *l2proto_list[];

/* *****
 * Flags for diag_l2_proto_startcomms(...flag_type flags); this is not the same
 * as the L2 handler flags (diag_l2_proto->diag_l2_flags) above.
 * Some flags are iso14230-specific and have no meaning with other
 * L2 protos.

 * The bottom 4 bits are not a bit mask because of how scantool_set.c works, i.e.
 * "5BAUD" has to be ==0, etc. That's also why we need _INITMASK
 * "initmode" macros
 */
#define DIAG_L2_TYPE_SLOWINIT	0		/* Do 5 Baud init */
#define DIAG_L2_TYPE_FASTINIT	1		/* Do fast init */
#define DIAG_L2_TYPE_CARBINIT	2		/* Do CARB init (see ISO14230-2 5.2.4), not implemented. */
#define DIAG_L2_TYPE_MONINIT	3		/* Don't do any init, just connect to bus */
#define DIAG_L2_TYPE_INITMASK	0x0F		/* Init options mask */
/*
 * DIAG_L2_TYPE_FUNCADDR : the address supplied is a functional
 * address (instead of physical), used for protocols such as ISO14230
 */
#define DIAG_L2_TYPE_FUNCADDR	0x10

/*
 * DIAG_L2_TYPE_PHYSCONN: tell protocols that support both functional and physical
 * addressing to switch to physical addressing after initial communications
 * are established (such as ISO14230)
 */
//#define DIAG_L2_TYPE_PHYSCONN	0x10	//XXX unused !!

/*
 * DIAG_L2_IDLE_J1978: tell the ISO14230 code to use SAE J1978 idle
 * messages (mode 1 PID 0) instead of the ISO "Tester Present"
 * messages for preventing link timeout
 *
 * SAE J1978 is the ODB II ScanTool specification document
 */
#define DIAG_L2_IDLE_J1978	0x20
/*****/




// Special Timeout for so-called "Smart" interfaces;
// Slower than any protocol, give them time to unframe
// and checksum the data:
#define SMART_TIMEOUT 150
#define RXTOFFSET 20	//ms to add to some diag_l1_recv calls in L2 code
				//In theory this should be 0... It's a band-aid
				//hack to allow system to system variations but NEEDS
				//to be replaced by something runtime-configurable either
				//by the user or some auto-configured feature of the
				//scantool (not implemented yet)



/* struct diag_l2_data: Used for DIAG_IOCTL_GET_L2_DATA */
//this isn't used frequently but L3_vag will eventually need it,
//and cmd_diag_probe uses it to report found ECUs.

struct	diag_l2_data {
	uint8_t physaddr;	/* Physical address of ECU */
	uint8_t kb1;		/* Keybyte 0 */
	uint8_t kb2;		/* Keybyte 1 */
};


/*
 * L2 flags returned from GET_L2_FLAGS
 * ( for struct diag_l2_proto->diag_l2_flags )
 */

/*FLAG_FRAMED:
 * Received data is sent upwards in frames (ie L3 doesn't have to try
 * and re-frame the data - this is done by timing windows or by the
 * protocol itself
 */
#define DIAG_L2_FLAG_FRAMED	0x01


/*
 * L2 does keep alive to ECU
 */
#define DIAG_L2_FLAG_KEEPALIVE	0x04

/*
 * L2 adds L2 checksum : ALWAYS !! Either it lets L1 handle it, or does it itself.
 */
//#define DIAG_L2_FLAG_DOESCKSUM	0x08

/*
 * L2 startcomms() always suceeds, but only way to find out if
 * a connection really exists is to send some data and wait for
 * response. This is useful when connected to car networks such as CAN or J1850
 * networks as opposed to normal diagnostic type interface such as ISO9141
 */
#define DIAG_L2_FLAG_CONNECTS_ALWAYS 0x10



/*
 * Internal interface
 */
/* Add a msg to a L2 connection */
void diag_l2_addmsg(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg);


/* Public functions */

/** Initialize L2 layer
 * Must be called once before using any L2 function
 */
int diag_l2_init(void);

/** De-initialize L2 layer
 * opposite of diag_l2_init(); call before unloading / exiting
 */
int diag_l2_end(void);

/** Opens a layer 1 device for usage

 * Opens L1 + L0, setting speed etc etc as requested.
 * @param dl0d L0 device to use
 * @return 0 if ok
 */
int diag_l2_open(struct diag_l0_device *dl0d, int L1protocol);

/** Close an L1 device
 * @return 0 if ok
 */
int diag_l2_close(struct diag_l0_device *);

/** Starts up a session with ECU
 * @param L2protocol - see diag_l2.h - Type of L2 session (ISO14230,
 *			SAE J1850, etc)
 *	@param flags flags / type: see diag_l2.h
 *	@param bitrate - bit rate to use (bps)
 *	@param target - target initialisation address
 *	@param source - source (tester) initialisation address
 * 	@return a new struct diag_l2_conn for representing the connection
 */
struct diag_l2_conn *diag_l2_StartCommunications(struct diag_l0_device *, int L2protocol,
	flag_type flags, unsigned int bitrate, target_type target, source_type source );

/** Stop talking to an ECU;
 *
 *	@return 0 if ok
 *
 *	This will undo anything done by l2_startcommunications()
 */
int diag_l2_StopCommunications(struct diag_l2_conn *);

/** Send a message (blocking)
 *	@param connection self-explanatory
 *	@param msg same. NOTE, the source address MAY
 *			be ignored, the one specified to StartCommunications
 *			is used for certain L2 protocols
 *	@return 0 on success, <0 on error
*/
int diag_l2_send(struct diag_l2_conn *connection, struct diag_msg *msg);

/** Checks if there's anything to receive (blocking).
 *	@param timeout in milliseconds
 *	@param callback Callback routine if read succesful
 *	@param handle : generic handle passed to callback
 *	@return 0 on success
 */
int diag_l2_recv(struct diag_l2_conn *connection, unsigned int timeout,
	void (* rcv_call_back)(void *, struct diag_msg *), void *handle );


/** Send a message, and wait the appropriate time for a response.
 *
 *		This calls the
 *		diag_l2_proto_request() function.
 *		This is important because some L2 protocols such as ISO
 *		will return a "request not yet complete", requiring a retry.
 *		This function deals with that behavior, as well as getting the timeouts and
 *		everything else correct.
 *		It is also coded to return the received message, rather than
 *		use callbacks; this provides a different (but non conflicting)
 *		type of API compared to send / recv calls.
 *
 *	@param errval	Pointer to error result if applicable
 *
 *	@return a new msg if successful, or NULL + sets *errval if failed.
 *
 */
struct diag_msg *diag_l2_request(struct diag_l2_conn *connection, struct diag_msg *msg,
		int *errval);

/** Send IOCTL to L2/L1
 *	@param command : IOCTL #, defined in diag.h
 *	@param data	optional input/output data
 *	@return 0 if OK, diag error num (<0) on error
 */
int diag_l2_ioctl(struct diag_l2_conn *connection, unsigned int cmd, void *data);


void diag_l2_timer(void);	/* Regular timer routine */

extern int diag_l2_debug;
extern struct diag_l2_conn  *global_l2_conn;	//TODO : move in globcfg struct

/** L2 protocol descriptor
 *
 * each diag_l2_???.c handler fills in one of these.
 */
struct diag_l2_proto {
	int diag_l2_protocol;
	const char *shortname;
	int diag_l2_flags;		//see #defines above

	//_StartCommunications: the l2 proto implementation of this should modify
	//the timing parameters in diag_l2_conn if required; by default in
	//diag_l2_startcommunications() iso14230 timings are used.
	int (*diag_l2_proto_startcomms)(struct diag_l2_conn *,
		flag_type, unsigned int bitrate, target_type, source_type);
	int (*diag_l2_proto_stopcomms)(struct diag_l2_conn *);
	//diag_l2_proto_send : returns 0 if ok
	int (*diag_l2_proto_send)(struct diag_l2_conn *, struct diag_msg *);
	//diag_l2_proto_recv: ret 0 if ok
	int (*diag_l2_proto_recv)(struct diag_l2_conn *d_l2_conn,
		unsigned int timeout, void (*callback)(void *handle, struct diag_msg *msg),
		void *handle);
	//diag_l2_proto_request : return a new diag_msg if succesful.
	struct diag_msg *(*diag_l2_proto_request)(struct diag_l2_conn *,
		struct diag_msg *, int *);
	//diag_l2_proto_timeout : this is called periodically (interval
	//defined in struct diag_l2_conn, usually to send keepalive messages.
	void (*diag_l2_proto_timeout)(struct diag_l2_conn *);
};

#if defined(__cplusplus)
}
#endif
#endif  /* _DIAG_L2_H_ */
