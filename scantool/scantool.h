#ifndef _SCANTOOL_H_
#define _SCANTOOL_H_
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
 *
 * J1978 Scan tool
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"

#if defined(__cplusplus)
extern "C" {
#endif

extern int diag_cli_debug;

extern const char progname[];

/**
 * Decimal/Octal/Hex to integer routine
 * formats:
 * [-]0[0-7] : octal
 * [-]0x[0-9,A-F,a-f] : hex
 * [-]$[0-9,A-F,a-f] : hex
 * [-][0-9] : dec
 * Returns 0 if unable to decode.
 */
int htoi(char *buf);

int cmd_up(int argc, char **argv);
int cmd_exit(int argc, char **argv);

/* Structure to hold responses */
typedef struct response
{
	uint8_t	type;
	uint8_t	len;
	uint8_t	data[7];

} response_t;

#define TYPE_UNTESTED	0	/* unchecked, prob because ECU doesn't support */
#define TYPE_FAILED	1	/* Got failure response */
#define TYPE_GOOD	2	/* Valid info */

/*
 * This structure holds all the data/config info for a given ecu
 * - one request can result in more than one ECU responding, and so
 * the data is stored in this
 */
typedef struct ecu_data
{
	uint8_t 	valid;		/* Valid flag */
	uint8_t	ecu_addr;	/* Address */

	uint8_t	supress;	/* Supress output of data from ECU in monitor mode; not implemented*/

	uint8_t	mode1_info[0x100];	/* Pids supported by ECU */
	uint8_t	mode2_info[0x100];	/* Freeze frame version */
	uint8_t	mode5_info[0x100];	/* Mode 5 info */
	uint8_t	mode6_info[0x100];	/* Mode 6 info */
	uint8_t	mode8_info[0x100];	/* Mode 8 info */
	uint8_t	mode9_info[0x100];	/* Mode 9 info */

	uint8_t	data_good;		/* Flags for above data */

	uint8_t	O2_sensors;	/* O2 sensors bit mask */

	response_t	mode1_data[256]; /* Response data for all responses */
	response_t	mode2_data[256]; /* Same, but for freeze frame */

	struct diag_msg	*rxmsg;		/* Received message */
} ecu_data_t;

#define ECU_DATA_PIDS	0x01
#define ECU_DATA_MODE2	0x02
#define ECU_DATA_MODE5	0x04
#define ECU_DATA_MODE6	0x08
#define ECU_DATA_MODE8	0x10
#define ECU_DATA_MODE9	0x20

#define MAX_ECU 8			/* Max 8 Ecus responding */
extern ecu_data_t	ecu_info[MAX_ECU];
extern unsigned int ecu_count;

extern uint8_t	global_O2_sensors;	/* O2 sensors bit mask */

extern struct diag_l0_device *global_dl0d;	/* L2 file descriptor */

struct diag_l2_conn;
struct diag_l3_conn;

/** Send a SAE J1979 request, get a response, and partially process it
 *
 * J1979 messages are 7 data bytes long: mode(SID) byte + max 6 extra bytes
 * (J1979 5.3.2; table 8)
 * @return 0 if ok
*/
int l3_do_j1979_rqst(struct diag_l3_conn *d_conn, uint8_t mode, uint8_t p1, uint8_t p2,
	uint8_t p3, uint8_t p4, uint8_t p5, uint8_t p6, void *handle);

/*
 * Send some data on the connection
 */
int l3_do_send( struct diag_l3_conn *d_conn, void *data, size_t len, void *handle);
int l2_do_send( struct diag_l2_conn *d_conn, void *data, size_t len, void *handle);
int l2_check_pid_bits(uint8_t *data, int pid);
int do_l2_9141_start(int destaddr); // 9141 init
int do_l2_14230_start(int init_type); //14230 init
int do_l2_generic_start(void);// Generic init, using parameters set by user
int do_j1979_getdtcs(void);
int do_j1979_getO2sensors(void);
int diag_cleardtc(void);
int ecu_connect(void);

struct diag_msg *find_ecu_msg(int byte, databyte_type val);

/*
 * Handle values for above (l3_do_j1979_rqst, j1979_data_rcv, etc)
 * XXX This was extremely irregular: they were passed as void pointers.
 * Now they are rather declared as const ints so we can pass
 * pointers to that instead of masquerading ints as void pointers.
 */
#define RQST_HANDLE_NORMAL	0	/* Normal mode */
#define RQST_HANDLE_WATCH	1	/* Watching, add timestamp */
#define RQST_HANDLE_DECODE	2	/* Just decode what arrived */
#define RQST_HANDLE_NCMS	3	/* Non cont. mon. tests */
#define RQST_HANDLE_NCMS2	4	/* Ditto, print fails only */
#define RQST_HANDLE_O2S		5	/* O2 sensor tests */
#define RQST_HANDLE_READINESS	6	/* Readiness Tests */
extern const int _RQST_HANDLE_NORMAL;	//Normal mode
extern const int _RQST_HANDLE_WATCH;	//Watching; add timestamp
extern const int _RQST_HANDLE_DECODE;	//Just decode what arrived
extern const int _RQST_HANDLE_NCMS;	//Non cont. mon. tests
extern const int _RQST_HANDLE_NCMS2;	//Ditto; print fails only
extern const int _RQST_HANDLE_O2S;	//O2 sensor tests
extern const int _RQST_HANDLE_READINESS;	//Readiness tests

int do_j1979_getdata(int interruptible_flag);
void do_j1979_basics(void) ;
void do_j1979_cms(void);
void do_j1979_ncms(int);
void do_j1979_getpids(void);
void do_j1979_O2tests(void);
void do_j1979_getO2tests(int O2sensor);

/*
 * Receive callback routines for various L3/L2 types
 */
void	j1979_data_rcv(void *handle, struct diag_msg *msg);
void	j1979_watch_rcv(void *handle, struct diag_msg *msg);
void	l2raw_data_rcv(void *handle, struct diag_msg *msg);

enum globstate {
	//specify numbers because some code checks (for global_state >= X) etc.
	STATE_IDLE=0,		/* Idle */
	STATE_WATCH=1,		/* Watch mode */
	STATE_CONNECTED=2,	/* Connected to ECU */
	STATE_L3ADDED=3,	/* Layer 3 protocol added on Layer 2 */
	STATE_SCANDONE=4,	/* J1978/9 Scan Done, so got J1979 PID list */
};	//only for global_state !
extern enum globstate global_state;

//XXX The following defs should probably go in an auto-generated l0_list.h file ... and they MUST match the list in scantool_set.c !
enum l0_nameindex {MET16, BR1, ELM, CARSIM, DUMB, DUMBT, LAST};
struct l0_name
{
	char * shortname;
	enum l0_nameindex code;
};

/** Global parameters set by user interface **/

extern enum l0_nameindex set_interface;	/* Physical interface name to use */
int set_interface_idx;	//index into l0_names
extern const struct l0_name l0_names[];	//filled in scantool_set.c

#define SUBINTERFACE_MAX 256
extern char	set_subinterface[SUBINTERFACE_MAX];	/* Sub interface (aka device name) */

/* struct global_cfg contains all global parameters */
extern struct globcfg {
	bool units;	/* English(1) or Metric(0)  display */

	uint8_t	tgt;	/* u8; target address */
	uint8_t	src;	/* u8: source addr / tester ID */
	bool	addrtype;	/* Address type, 1 = functional */
	unsigned int speed;	/* ECU comms speed */

	int	initmode;	/* Type of bus init (ISO9141/14230 only) */
	int	L1proto;	/* L1 (H/W) Protocol type */
	int	L2proto;	/* L2 (S/W) Protocol type; value of ->diag_l2_protocol. */
	int	L2idx;		/* index of that L2 proto in struct l2proto_list[] */

	//struct diag_l0_device *dl0d;	/* L0 device to use */
} global_cfg;

/** J1979 PID structures + utils **/
struct pid ;
/* format <numbytes> bytes of data into buf, up to <maxlen> chars. */
typedef void (formatter)(char *buf, int maxlen, int units, const struct pid *, response_t *, int numbytes);

struct pid
{
	int pidID ;
	const char *desc ;
	formatter *cust_snprintf ;
	int bytes ;
	const char *fmt1 ; // SI
	double scale1 ;
	double offset1 ;
	const char *fmt2 ; // English (typically)
	double scale2 ;
	double offset2 ;
};

#define DATA_VALID(p, d)	(d[p->pidID].type == TYPE_GOOD)
#define DATA_1(p, n, d)	(d[p->pidID].data[n])	/* extract 8bit value @offset n */
#define DATA_2(p, n, d)	(DATA_1(p, n, d) * 256 + DATA_1(p, n+1, d))	/* extract 16bit value @offset n */
#define DATA_RAW(p, n, d)	(p->bytes == 1 ? DATA_1(p, n, d) : DATA_2(p, n, d))

#define DATA_SCALED(p, v)	(v * p->scale1 + p->offset1)
#define DATA_ENGLISH(p, v)	(v * p->scale2 + p->offset2)


const struct pid *get_pid ( unsigned int i ) ;


#if defined(__cplusplus)
}
#endif
#endif /* _SCANTOOL_H_ */
