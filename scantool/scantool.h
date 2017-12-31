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

#if defined(__cplusplus)
extern "C" {
#endif

/* Structure to hold responses */
typedef struct {
	uint8_t type;
	uint8_t len;
	uint8_t data[7];

} response;

#define TYPE_UNTESTED 0 /* unchecked, prob because ECU doesn't support */
#define TYPE_FAILED 1   /* Got failure response */
#define TYPE_GOOD 2     /* Valid info */

/*
 * This structure holds all the data/config info for a given ecu
 * - one request can result in more than one ECU responding, and so
 * the data is stored in this
 */
typedef struct {
	uint8_t valid;    /* Valid flag */
	uint8_t ecu_addr; /* Address */

	uint8_t supress; /* Supress output of data from ECU in monitor mode; not
			    implemented*/

	uint8_t mode1_info[0x100]; /* Pids supported by ECU */
	uint8_t mode2_info[0x100]; /* Freeze frame version */
	uint8_t mode5_info[0x100]; /* Mode 5 info */
	uint8_t mode6_info[0x100]; /* Mode 6 info */
	uint8_t mode8_info[0x100]; /* Mode 8 info */
	uint8_t mode9_info[0x100]; /* Mode 9 info */

	uint8_t data_good; /* Flags for above data */

	uint8_t O2_sensors; /* O2 sensors bit mask */

	response mode1_data[256]; /* Response data for all responses */
	response mode2_data[256]; /* Same, but for freeze frame */

	struct diag_msg *rxmsg; /* Received message */
} ecu_data;

#define ECU_DATA_PIDS 0x01
#define ECU_DATA_MODE2 0x02
#define ECU_DATA_MODE5 0x04
#define ECU_DATA_MODE6 0x08
#define ECU_DATA_MODE8 0x10
#define ECU_DATA_MODE9 0x20

#define MAX_ECU 8 /* Max 8 Ecus responding */
extern ecu_data ecu_info[MAX_ECU];
extern unsigned int ecu_count;

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
int l3_do_send(struct diag_l3_conn *d_conn, void *data, size_t len, void *handle);
int l2_do_send(struct diag_l2_conn *d_conn, void *data, size_t len, void *handle);
int l2_check_pid_bits(uint8_t *data, int pid);
int do_l2_9141_start(int destaddr);   // 9141 init
int do_l2_14230_start(int init_type); // 14230 init
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
#define RQST_HANDLE_NORMAL 0             /* Normal mode */
#define RQST_HANDLE_WATCH 1              /* Watching, add timestamp */
#define RQST_HANDLE_DECODE 2             /* Just decode what arrived */
#define RQST_HANDLE_NCMS 3               /* Non cont. mon. tests */
#define RQST_HANDLE_NCMS2 4              /* Ditto, print fails only */
#define RQST_HANDLE_O2S 5                /* O2 sensor tests */
#define RQST_HANDLE_READINESS 6          /* Readiness Tests */
extern const int _RQST_HANDLE_NORMAL;    // Normal mode
extern const int _RQST_HANDLE_WATCH;     // Watching; add timestamp
extern const int _RQST_HANDLE_DECODE;    // Just decode what arrived
extern const int _RQST_HANDLE_NCMS;      // Non cont. mon. tests
extern const int _RQST_HANDLE_NCMS2;     // Ditto; print fails only
extern const int _RQST_HANDLE_O2S;       // O2 sensor tests
extern const int _RQST_HANDLE_READINESS; // Readiness tests

int do_j1979_getdata(int interruptible_flag);
void do_j1979_basics(void);
void do_j1979_cms(void);
void do_j1979_ncms(int);
void do_j1979_getpids(void);
void do_j1979_O2tests(void);
void do_j1979_getO2tests(int O2sensor);

/*
 * Receive callback routines for various L3/L2 types
 */
void j1979_data_rcv(void *handle, struct diag_msg *msg);
void j1979_watch_rcv(void *handle, struct diag_msg *msg);
void l2raw_data_rcv(void *handle, struct diag_msg *msg);

/** J1979 PID structures + utils **/
struct pid;
/* format <numbytes> bytes of data into buf, up to <maxlen> chars. */
typedef void(formatter)(char *buf, int maxlen, int units, const struct pid *, response *,
			int numbytes);

struct pid {
	int pidID;
	const char *desc;
	formatter *cust_snprintf;
	int bytes;
	const char *fmt1; // SI
	double scale1;
	double offset1;
	const char *fmt2; // English (typically)
	double scale2;
	double offset2;
};

#define DATA_VALID(p, d) (d[p->pidID].type == TYPE_GOOD)
#define DATA_1(p, n, d) (d[p->pidID].data[n]) /* extract 8bit value @offset n */
#define DATA_2(p, n, d)                                                                   \
	(DATA_1(p, n, d) * 256 + DATA_1(p, n + 1, d)) /* extract 16bit value @offset n */
#define DATA_RAW(p, n, d) (p->bytes == 1 ? DATA_1(p, n, d) : DATA_2(p, n, d))

#define DATA_SCALED(p, v) (v * p->scale1 + p->offset1)
#define DATA_ENGLISH(p, v) (v * p->scale2 + p->offset2)

const struct pid *get_pid(unsigned int i);

#if defined(__cplusplus)
}
#	endif
#endif /* _SCANTOOL_H_ */
