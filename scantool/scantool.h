#ifndef _SCANTOOL_H_
#define _SCANTOOL_H_
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
 *
 * J1978 Scan tool
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"

#if defined(__cplusplus)
extern "C" {
#endif

extern int diag_cmd_debug;

extern char *progname;

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

	uint8_t	supress;	/* Supress output of data from ECU */

	uint8_t	pids[0x100];	/* Pids supported by ECU */
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

/* XXX end of stuff to move */

extern int		global_conmode;
extern int		global_protocol;

extern struct diag_l0_device *global_l2_dl0d;	/* L2 file descriptor */

#define	PROTOCOL_NOTFOUND	0
#define	PROTOCOL_ISO9141	1
#define	PROTOCOL_ISO14230	2
#define	PROTOCOL_SAEJ1850	3

//XXX The following defs should probably go in an auto-generated l0_list.h file
enum l0_nameindex {MET16, SE9141, VAGTOOL, BR1, ELM, CARSIM, DUMB};
struct l0_name
{
	char * longname;
	enum l0_nameindex code;
};

struct diag_l2_conn;
struct diag_l3_conn;

/*
 * Do a J1979 request
 */
int l3_do_j1979_rqst(struct diag_l3_conn *d_conn, int mode, uint8_t p1, uint8_t p2,
	uint8_t p3, uint8_t p4, uint8_t p5, uint8_t p6, uint8_t p7, void *handle);

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
 * Handle values for above
 */
#define RQST_HANDLE_NORMAL	0	/* Normal mode */
#define RQST_HANDLE_WATCH	1	/* Watching, add timestamp */
#define RQST_HANDLE_DECODE	2	/* Just decode what arrived */
#define RQST_HANDLE_NCMS	3	/* Non cont. mon. tests */
#define RQST_HANDLE_NCMS2	4	/* Ditto, print fails only */
#define RQST_HANDLE_O2S		5	/* O2 sensor tests */
#define RQST_HANDLE_READINESS	6	/* Readiness Tests */

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

extern int		global_state;

#define STATE_IDLE	0	/* Idle */
#define STATE_WATCH	1	/* Watch mode */
#define	STATE_CONNECTED	2	/* Connected to ECU */
#define	STATE_L3ADDED	3	/* Layer 3 protocol added on Layer 2 */
#define	STATE_SCANDONE	4	/* J1978/9 Scan Done, so got J1979 PID list */


/* Parameters set by user interface (and their defaults) */
extern int 	set_speed ;	/* Comms speed */
extern unsigned char	set_testerid ;	/* Our tester ID */
extern int	set_addrtype ;	/* Address type, 1 = functional */
extern unsigned char	set_destaddr ;	/* Dest ECU address */
extern int	set_L1protocol ;	/* L1 (H/W) Protocol type */
extern int	set_L2protocol ;	/* L2 (S/W) Protocol type */
extern int	set_initmode ;
extern int 	set_display ;	/* English (1) or Metric (0) display */

extern const char*	set_vehicle;	/* Vehicle name */
extern const char*	set_ecu;	/* ECU name */

extern enum l0_nameindex set_interface;	/* Physical interface name to use */
int set_interface_idx;	//index into l0_names
extern const struct l0_name l0_names[];

#define SUBINTERFACE_MAX 256
extern char	set_subinterface[SUBINTERFACE_MAX];	/* Sub interface ID */

struct pid ;
typedef void (formatter)(char *, int, const struct pid *, response_t *, int);

struct pid
{
  int         pidID   ;
  const char *desc    ;
  formatter  *sprintf ;
  int         bytes   ;
  const char *fmt1    ;   // SI
  double      scale1  ;
  double      offset1 ;
  const char *fmt2    ;   // English (typically)
  double      scale2  ;
  double      offset2 ;
};

#define DATA_VALID(p, d)        (d[p->pidID].type == TYPE_GOOD)
#define DATA_1(p, n, d)         (d[p->pidID].data[n])
#define DATA_2(p, n, d)         (DATA_1(p, n, d) * 256 + DATA_1(p, n+1, d))
#define DATA_RAW(p, n, d)       (p->bytes == 1 ? DATA_1(p, n, d) : \
                                                 DATA_2(p, n, d))
#define DATA_SCALED(p, v)       (v * p->scale1 + p->offset1)
#define DATA_ENGLISH(p, v)      (v * p->scale2 + p->offset2)


const struct pid *get_pid ( int i ) ;


#if defined(__cplusplus)
}
#endif
#endif /* _SCANTOOL_H_ */
