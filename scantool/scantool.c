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
 *
 * Mostly ODBII Compliant Scanner Program (SAE J1978)
 *
 * ODBII Scanners are defined in SAE J1978. References in this document
 * are to SAE J1978 Revised Feb1998. This document is available from
 * www.sae.org
 *
 * From Section 5. Required functions & support - the following are the basic
 * functions that the scan tool is required to support or provide
 *
 * a. Automatic hands-off determination of the communication interface user
 * b. Obtaining and displaying the status and results of vehicle on-board
 *	diagnostic evaluations
 * c. Obtaining & Displaying ODB II emissions related DTCs
 * d. Obtaining & Displaying ODB II emissions related current data
 * e. Obtaining & Displaying ODB II emissions related freeze frame data
 * f. Clearing the storage of (c) to (e)
 * g. Obtaining and displaying ODB II emissions related test params and
 *	results as described in SAE J1979
 * h. Provide a user manual and/or help facility
 *
 * Section 6 - Vehicle interface
 *	Communication Data Link and Physical Layers
 *		SAE J1850 interface
 *		ISO 9141-2 interface
 *		ISO 14230-4
 *
 * Section 7.3 - the scan tool must be capable of interfacing with a
 *	vehicle in which multiple modules may be used to support ODBII
 *	requirements
 *	The ODBII Scan tool must alert the user when multiple modules
 *	respond to the same request
 *	Ditto if different values
 *	The tool must provide the user with the ability to select for
 *	display as separate display items the responses received from
 *	multiple modules for the same data item
 *
 *
 * THIS DOESN'T SUPPORT Section 6 as we only have one interface
 * - It "copes" with 7.3 but doesn't tell user or allow user to select
 * which module to see responses from
 *
 *
 *************************************************************************
 *
 * This file contains the workhorse routines, ie all that execute the
 * J1979 (ODBII) protocol
 */

#include <assert.h>
#include <stdbool.h>

#include "diag.h"
#include "diag_os.h"
#include "diag_dtc.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "scantool.h"
#include "scantool_cli.h"
#include "scantool_aif.h"
#include "utlist.h"


//ugly, global data. Could be struct-ed together eventually
struct diag_l2_conn *global_l2_conn;
struct diag_l3_conn *global_l3_conn;
enum globstate global_state = STATE_IDLE;
uint8_t	global_O2_sensors;	/* O2 sensors bit mask */
struct diag_l0_device *global_dl0d;


/*
 * Data received from each ecu
 */
ecu_data_t	ecu_info[MAX_ECU];
unsigned int ecu_count;		/* How many ecus are active */


/* Merge of all the suported mode1 pids by all the ECUs */
uint8_t	merged_mode1_info[0x100];
uint8_t	merged_mode5_info[0x100];


/* Prototypes */
int print_single_dtc(databyte_type d0, databyte_type d1) ;
void do_j1979_getmodeinfo(uint8_t mode, int response_offset) ;

struct diag_l2_conn *do_common_start(int L1protocol, int L2protocol,
	uint32_t type, unsigned int bitrate, target_type target, source_type source );

void initialse_ecu_data(void);

//these are mostly dummy variables only used for some j1979 features.
//see scantool.h
const int _RQST_HANDLE_NORMAL = RQST_HANDLE_NORMAL; 	//Normal mode
const int _RQST_HANDLE_WATCH = RQST_HANDLE_WATCH;  //Watching: add timestamp
const int _RQST_HANDLE_DECODE = RQST_HANDLE_DECODE; 	//Just decode what arrived
const int _RQST_HANDLE_NCMS = RQST_HANDLE_NCMS; 	//Non cont. mon. tests
const int _RQST_HANDLE_NCMS2 = RQST_HANDLE_NCMS2;  //Same: print fails only
const int _RQST_HANDLE_O2S = RQST_HANDLE_O2S; 	//O2 sensor tests
const int _RQST_HANDLE_READINESS = RQST_HANDLE_READINESS; 	//Readiness tests


struct diag_msg *
find_ecu_msg(int byte, databyte_type val)
{
	ecu_data_t *ep;
	struct diag_msg *rxmsg = NULL;
	unsigned int i;

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if (ep->rxmsg) {
			/* Some data arrived from this ecu */
			if (ep->rxmsg->data[byte] == val) {
				rxmsg = ep->rxmsg;
				break;
			}
		}
	}
	return rxmsg;
}


/*
 * Message print out / debug routines
 */
static void
print_msg_header(FILE *fp, struct diag_msg *msg, bool timestamp, int msgnum)
{
	if (timestamp)
		fprintf(fp, "%lu.%03lu: ",
			msg->rxtime / 1000, msg->rxtime % 1000);
	fprintf(fp, "msg %02d src=0x%02X dest=0x%02X\n", msgnum, msg->src, msg->dest);
	fprintf(fp, "msg %02d data: ", msgnum);
}

static void
print_msg(FILE *fp, struct diag_msg *msg, bool timestamp)
{
	struct diag_msg *tmsg;
	int i=0;

	LL_FOREACH(msg, tmsg) {
		print_msg_header(fp, tmsg, timestamp, i);
		diag_data_dump(fp, tmsg->data, tmsg->len);
		if (tmsg->fmt & DIAG_FMT_BADCS)
			fprintf(fp, " [BAD CKS]\n");
		else
			fprintf(fp, "\n");
		i++;
	}
}

/*
 * ************
 * Basic routines to connect/interrogate an ECU
 * ************
 */

/*
 * Receive callback routines. If handle is RQST_HANDLE_WATCH then we're in "watch"
 * mode (set by caller to recv()), else in normal data mode
 *
 * We get called by L3/L2 with all the messages received within the
 * window, i.e we can get responses from many ECUs that all relate to
 * a single request [ISO9141/14230 uses timing windows to decide when
 * no more responses will arrive]
 *
 * We can [and do] get more than one ecu responding with different bits
 * of data on certain vehicles
 */
void
j1979_data_rcv(void *handle, struct diag_msg *msg)
{
	assert(msg != NULL);
	uint8_t *data;
	struct diag_msg *tmsg;
	unsigned int i;
	int ihandle;
	ecu_data_t	*ep;

	if (handle !=NULL)
		ihandle= * (int *)handle;
	else
		ihandle= RQST_HANDLE_NORMAL;

	const char *O2_strings[] = {
		"Test 0",
		"Rich to lean sensor threshold voltage",
		"Lean to rich sensor threshold voltage",
		"Low sensor voltage for switch time calc.",
		"High sensor voltage for switch time calc.",
		"Rich to lean sensor switch time",
		"Lean to rich sensor switch time",
		"Minimum sensor voltage for test cycle",
		"Maximum sensor voltage for test cycle",
		"Time between sensor transitions"
		};

	if (diag_cli_debug & DIAG_DEBUG_DATA) {
		fprintf(stderr, "scantool: Got handle %p; %d bytes of data, src=0x%X, dest=0x%X\n",
			(void *)handle, msg->len, msg->src, msg->dest);
		print_msg(stdout, msg, 0);
	}

	/* Deal with the diag type responses (send/recv/watch) */
	switch (ihandle) {
	/* There is no difference between watch and decode ... */
		case RQST_HANDLE_WATCH:
		case RQST_HANDLE_DECODE:
			if (!(diag_cli_debug & DIAG_DEBUG_DATA)) {
				/* Print data (unless done already) */
					print_msg(stdout, msg, 0);
			}
			return;
			break;
		default:
			break;
	}


	/* All other responses are J1979 response messages */

	/* Clear out old messages */
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if (ep->rxmsg != NULL) {
			/* Old msg. release it */
			diag_freemsg(ep->rxmsg);
			ep->rxmsg = NULL;
		}
	}

	/*
	 * We may get more than one msg here, as more than one
	 * ECU may respond.
	 */
	LL_FOREACH(msg, tmsg) {
		uint8_t src = tmsg->src;
		unsigned int ecu_idx;
		struct diag_msg *rmsg;
		bool found=0;

		for (ecu_idx=0, ep=ecu_info; ecu_idx<MAX_ECU; ecu_idx++, ep++) {
			if (ep->valid) {
				if (ep->ecu_addr == src) {
					found = 1;
					break;
				}
			} else {
				ecu_count++;
				ep->valid = 1;
				ep->ecu_addr = src;
				found = 1;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, "ERROR: Too many ECUs responded\n");
			fprintf(stderr, "ERROR: Info from ECU addr 0x%02X ignored\n", src);
			return;
		}

		/* Ok, we now have the ecu_info for this message fragment */

		/* Attach the fragment to the ecu_info */
		rmsg = diag_dupsinglemsg(tmsg);
		if (rmsg == NULL)
			return;
		LL_CONCAT(ep->rxmsg, rmsg);

		/*
		 * Deal with readiness tests, ncms and O2 sensor tests
		 * Note that ecu_count gets to the correct value from
		 * the first response from the ECU which is the mode1pid0
		 * response
		 */
		data = msg->data;
		switch (ihandle) {
			case RQST_HANDLE_READINESS:
				/* Handled in cmd_test_readiness() */
				break;
			case RQST_HANDLE_NCMS:
			case RQST_HANDLE_NCMS2:
				/*
				 * Non Continuously Monitored System result
				 * NCMS2 prints everything, NCMS prints just failed
				 * tests
				 */
				if (data[0] != 0x46) {
					fprintf(stderr, "Test 0x%02X failed %d\n",
						data[1], data[2]);
					return;
				}
				if ((data[1] & 0x1f) == 0) {
					/* no Test support */
					return;
				}
				LL_FOREACH(msg, tmsg) {
					int val, lim;
					data = tmsg->data;

					val = (data[3]*255) + data[4];
					lim = (data[5]*255) + data[6];

					if ((data[2] & 0x80) == 0) {
						if (ihandle == RQST_HANDLE_NCMS2) {
							/* Only print fails */
							if (val > lim) {
								fprintf(stderr, "Test 0x%X Component 0x%X FAILED ",
								data[1], data[2] & 0x7f);
								fprintf(stderr, "Max val %d Current Val %d\n",
									lim, val);

							}
						} else {
							/* Max value test */
							fprintf(stderr, "Test 0x%X Component 0x%X ",
								data[1], data[2] & 0x7f);

							if (val > lim)
								fprintf(stderr, "FAILED ");
							else
								fprintf(stderr, "Passed ");

							fprintf(stderr, "Max val %d Current Val %d\n",
								lim, val);
						}
					} else {
						if (ihandle == RQST_HANDLE_NCMS2) {
							if (val < lim) {
								fprintf(stderr, "Test 0x%X Component 0x%X FAILED ",
									data[1], data[2] & 0x7f);
								fprintf(stderr, "Min val %d Current Val %d\n",
									lim, val);
							}
						} else {
							/* Min value test */
							fprintf(stderr, "Test 0x%X Component 0x%X ",
								data[1], data[2] & 0x7f);
							if (val < lim)
								fprintf(stderr, "FAILED ");
							else
								fprintf(stderr, "Passed ");

							fprintf(stderr, "Min val %d Current Val %d\n",
								lim, val);
						}
					}
				}
				return;
			case RQST_HANDLE_O2S:
				if (ecu_count>1)
					fprintf(stderr, "ECU %d ", ecu_idx);

				/* O2 Sensor test results */
				if (msg->data[0] != 0x45) {
					fprintf(stderr, "Test 0x%02X failed %d\n",
						msg->data[1], msg->data[2]);
					return;
				}
				if ((data[1] & 0x1f) == 0) {
					/* No Test support */
				} else {
					int val = data[4];
					int min = data[5];
					int max = data[6];
					int failed ;

					if ((val < min) || (val > max))
						failed = 1;
					else
						failed = 0;

					switch (data[1]) {
						case 1:	/* Constant values voltages */
						case 2:
						case 3:
						case 4:
							fprintf(stderr, "%s: %f\n", O2_strings[data[1]],
									data[4]/200.0);
							break;
						case 5:
						case 6:
						case 9:
							fprintf(stderr, "%s: actual %2.2f min %2.2f max %2.2f %s\n",
								O2_strings[data[1]], data[4]/250.0,
								data[5]/250.0, data[6]/250.0,
								failed?"FAILED":"Passed" );
							break;
						case 7:
						case 8:
							fprintf(stderr, "%s: %f %f %f %s\n", O2_strings[data[1]],
								data[4]/200.,
								data[5]/200.,
								data[6]/200.,
								failed?"FAILED":"Passed" );
							break;
						default:
							fprintf(stderr, "Test %d: actual 0x%X min 0x%X max 0x%X %s\n",
								data[1], data[4],
								data[5], data[6],
								failed?"FAILED":"Passed" );
							break;
					}
				}
				return;
		}
	}
	return;
}


/*
 * Receive callback routines, for watching mode, call
 * L3 (in this case SAE J1979) decode routine, if handle is NULL
 * just print the data
 */
void
j1979_watch_rcv(void *handle, struct diag_msg *msg)
{
	struct diag_msg *tmsg;
	int i=0;

	LL_FOREACH(msg, tmsg) {
		print_msg_header(stderr, tmsg, 1, i);

		if (handle != NULL) {
			char buf[256];	/* XXX Can we switch to stdargs for decoders? */
			fprintf(stderr, "%s\n",
				diag_l3_decode((struct diag_l3_conn *)handle, tmsg,
				buf, sizeof(buf)));
		} else {
			diag_data_dump(stderr, tmsg->data, tmsg->len);
			fprintf(stderr, "\n");
		}
		i++;
	}
}


void
l2raw_data_rcv(UNUSED(void *handle), struct diag_msg *msg)
{
	/*
	 * Layer 2 call back, just print the data, this is used if we
	 * do a "read" and we haven't yet added a L3 protocol
	 */
	print_msg(stderr, msg, 0);
	return;
}

/*
 * Routine to check the bitmasks of PIDS received in response
 * to a mode 1 PID 0/0x20/0x40 request
 */
int
l2_check_pid_bits(uint8_t *data, int pid)
{
	int offset;
	int bit;

	pid--;		/* (bits start at 0, pids at 1) */
	/*
	 * Bits 1-8 are in byte 1, 9-16 in byte 2 etc
	 * Same code is for PID requests for 0x40 and 0x60
	 */
	while (pid > 0x20)
		pid -= 0x20;
	offset = pid/8;

	bit = pid - (offset * 8);
	bit = 7 - bit;

	if (data[offset] & (1<<bit))
		return 1;

	return 0;
}

int
l3_do_j1979_rqst(struct diag_l3_conn *d_conn, uint8_t mode, uint8_t p1, uint8_t p2,
	uint8_t p3, uint8_t p4, uint8_t p5, uint8_t p6, void *handle)
{
	assert(d_conn != NULL);
	struct diag_msg msg={0};
	uint8_t data[7];
	int ihandle;
	int rv;
	ecu_data_t *ep;
	unsigned int i;

	uint8_t *rxdata;
	struct diag_msg *rxmsg;

	if (handle !=NULL)
		ihandle= * (int *) handle;
	else
		ihandle= RQST_HANDLE_NORMAL;
	/* Lengths of msg for each mode, 0 = this routine doesn't support */
	// excludes headers and checksum.
	uint8_t mode_lengths[] = { 0, 2, 3, 1, 1, 3, 2, 1, 7, 2 };
#define J1979_MODE_MAX 9

	if (diag_cli_debug & DIAG_DEBUG_DATA) {
		fprintf(stderr, "j1979_rqst: handle %p conn %p mode %#02X\n",
			(void *)handle, (void *)d_conn, mode);

	}

	/* Put in src/dest etc, L3 or L2 may override/ignore them */
	msg.src = global_cfg.src;
	msg.dest = global_cfg.tgt;

	/* XXX add funcmode flags */

	if (mode > J1979_MODE_MAX)
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);

	msg.len = mode_lengths[mode];
	if (msg.len == 0)
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);

	msg.data = data;
	data[0] = mode;
	data[1] = p1;
	data[2] = p2;
	data[3] = p3;
	data[4] = p4;
	data[5] = p5;
	data[6] = p6;
	if ((rv=diag_l3_send(d_conn, &msg)))
		return diag_iseterr(rv);

	/* And get response(s) within a short while */
	rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);
	if (rv < 0) {
		fprintf(stderr, "Request failed, retrying...\n");
		if ((rv=diag_l3_send(d_conn, &msg)))
			return diag_iseterr(rv);
		rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);
		if (rv < 0) {
			fprintf(stderr, "Retry failed, resynching...\n");
			rv= d_conn->d_l3_proto->diag_l3_proto_timer(d_conn, 6000);	//force keepalive
			if (rv < 0) {
				fprintf(stderr, "\tfailed, connection to ECU may be lost!\n");
				return diag_iseterr(rv);
			}
			fprintf(stderr, "\tOK.\n");
			return DIAG_ERR_TIMEOUT;

		}
	}

	//This part is super confusing: ihandle comes from the handle from a callback passed
	//between L2 and L3 with handles to handles etc..
	switch (ihandle) {
		/* We dont process the info in watch/decode mode */
		case RQST_HANDLE_WATCH:
		case RQST_HANDLE_DECODE:
			return rv;
			break;
		default:
			break;
	}

	/*
	 * Go thru the ecu_data and see what was received.
	 */
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if (ep->rxmsg) {
			/* Some data arrived from this ecu */
			rxmsg = ep->rxmsg;
			rxdata = ep->rxmsg->data;

			/* A bit of ugliness is required to bail out on NegativeResponse messages when using
			 * an iso14230 L2.
			 */
			if (d_conn->d_l3l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_ISO14230) {
				if (rxdata[0] == 0x7f) {
					return DIAG_ERR_ECUSAIDNO;
				}
			}
			switch (mode) {
				case 1:
					if (rxdata[0] != 0x41) {
						ep->mode1_data[p1].type = TYPE_FAILED;
						break;
					}
					memcpy(ep->mode1_data[p1].data, rxdata,
						rxmsg->len);
					ep->mode1_data[p1].len = rxmsg->len;
					ep->mode1_data[p1].type = TYPE_GOOD;

					break;
				case 2:
					if (rxdata[0] != 0x42) {
						ep->mode2_data[p1].type = TYPE_FAILED;
						break;
					}
					memcpy(ep->mode2_data[p1].data, rxdata,
						rxmsg->len);
					ep->mode2_data[p1].len = rxmsg->len;
					ep->mode2_data[p1].type = TYPE_GOOD;
					break;
			}
		}
	}
	return 0;
}


/*
 * Send some data to the ECU (L3)
 */
int
l3_do_send(struct diag_l3_conn *d_conn, void *data, size_t len, void *handle)
{
	struct diag_msg msg={0};
	int rv;
	if (len > 255)
		return DIAG_ERR_GENERAL;

	/* Put in src/dest etc, L3 or L2 may override/ignore them */
	msg.src = global_cfg.src;
	msg.dest = global_cfg.tgt;

	msg.len = (uint8_t) len;
	msg.data = (uint8_t *)data;
	diag_l3_send(d_conn, &msg);

	/* And get response(s) */
	rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);

	return rv;
}
/*
 * Same but L2 type
 */
int
l2_do_send(struct diag_l2_conn *d_conn, void *data, size_t len, void *handle)
{
	struct diag_msg msg={0};
	int rv;
	if (len > 255)
		return DIAG_ERR_GENERAL;

	/* Put in src/dest etc, L2 may override/ignore them */
	msg.src = global_cfg.src;
	msg.dest = global_cfg.tgt;

	msg.len = len;
	msg.data = (uint8_t *)data;
	diag_l2_send(d_conn, &msg);

	/* And get response(s) */
	rv = diag_l2_recv(d_conn, 300, l2raw_data_rcv, handle);

	return rv;
}


/*
 * Clear data that is relevant to an ECU
 */
static int
clear_data(void)
{
	ecu_count = 0;
	memset(ecu_info, 0, sizeof(ecu_info));

	memset(merged_mode1_info, 0, sizeof(merged_mode1_info));
	memset(merged_mode5_info, 0, sizeof(merged_mode5_info));

	return 0;
}

/*
 * Common start routine used by all protocols
 * - initialises the diagnostic layer
 * - opens a Layer 2 device for the specified Layer 1 protocol
  * returns L2 file descriptor
 */
static struct diag_l2_conn * do_l2_common_start(int L1protocol, int L2protocol,
	flag_type type, unsigned int bitrate, target_type target, source_type source )
{
	int rv;
	struct diag_l0_device *dl0d = global_dl0d;
	struct diag_l2_conn *d_conn = NULL;

	if (!global_dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return NULL;
	}
	/* Clear out all ECU data as we're starting again */
	clear_data();

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		diag_end();
		return NULL;
	}

	rv = diag_l2_open(dl0d, L1protocol);
	if (rv) {
		if ((rv != DIAG_ERR_BADIFADAPTER) &&
			(rv != DIAG_ERR_PROTO_NOTSUPP))
			fprintf(stderr, "Failed to open hardware interface\n");

		return diag_pseterr(rv);
	}

	/* Now do the Layer 2 startcommunications */

	d_conn = diag_l2_StartCommunications(dl0d, L2protocol, type,
		bitrate, target, source);

	if (d_conn == NULL) {
		diag_l2_close(dl0d);
		fprintf(stderr, FLFMT "l2_common_start: l2_StartComm failed\n", FL);
		return NULL;
	}


	/*
	 * Now Get the L2 flags, and if this is a network type where
	 * startcommunications always works, we have to try and see if
	 * the ECU is there
	 *
	 * Some interface types will always return success from StartComms()
	 * (like J1850)
	 * but you only know if the ECU is on the network if you then can
	 * send/receive data to it in the appropriate format.
	 * For those type of interfaces we send a J1979 mode1 pid0 request
	 * (since we are a scantool, thats the correct thing to send)
	 * But this will happen when the caller tries to add a J1979 L3
	 * handler; at the L2 level "mode 1 pid 0" is meaningless.
	 */

	return d_conn;
}


/*
 * 9141 init
 */
int
do_l2_9141_start(int destaddr)
{
	struct diag_l2_conn *d_conn;

	d_conn = do_l2_common_start(DIAG_L1_ISO9141, DIAG_L2_PROT_ISO9141,
		DIAG_L2_TYPE_SLOWINIT, global_cfg.speed, (uint8_t)destaddr,
		global_cfg.src);

	if (d_conn == NULL)
		return diag_iseterr(DIAG_ERR_GENERAL);

	/* Connected ! */
	global_l2_conn = d_conn;

	return 0;
}

/*
 * 14120 init
 */
int
do_l2_14230_start(int init_type)
{
	struct diag_l2_conn *d_conn;
	flag_type flags = 0;

	if (global_cfg.addrtype)
		flags = DIAG_L2_TYPE_FUNCADDR;
	else
		flags = 0;

	flags |= DIAG_L2_IDLE_J1978;	/* Use J1978 idle msgs */

	flags |= (init_type & DIAG_L2_TYPE_INITMASK) ;

	d_conn = do_l2_common_start(DIAG_L1_ISO14230, DIAG_L2_PROT_ISO14230,
		flags, global_cfg.speed, global_cfg.tgt, global_cfg.src);

	if (d_conn == NULL)
		return diag_iseterr(DIAG_ERR_GENERAL);

	/* Connected ! */
	global_l2_conn = d_conn;

	return 0;
}

/*
 * J1850 init, J1850 interface type passed as l1_type
 */
static int
do_l2_j1850_start(int l1_type)
{
	flag_type flags = 0;
	struct diag_l2_conn *d_conn;

	d_conn = do_l2_common_start(l1_type, DIAG_L2_PROT_SAEJ1850,
		flags, global_cfg.speed, 0x6a, global_cfg.src);

	if (d_conn == NULL)
		return diag_iseterr(DIAG_ERR_GENERAL);

	/* Connected ! */
	global_l2_conn = d_conn;

	return 0;
}

/*
 * Generic init, using parameters set by user.
 * Currently only called from cmd_diag_connect;
 */
int
do_l2_generic_start(void)
{
	struct diag_l2_conn *d_conn;
	struct diag_l0_device *dl0d = global_dl0d;
	int rv;
	flag_type flags = 0;

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		diag_end();
		return diag_iseterr(rv);
	}

	/* Open interface using current L1 proto and hardware */
	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "l2_generic_start: open failed for protocol %d on %s\n",
			global_cfg.L1proto, dl0d->dl0->shortname);
		return diag_iseterr(rv);
	}

	if (global_cfg.addrtype)
		flags = DIAG_L2_TYPE_FUNCADDR;
	else
		flags = 0;

	flags |= (global_cfg.initmode & DIAG_L2_TYPE_INITMASK) ;

	d_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
		flags, global_cfg.speed, global_cfg.tgt, global_cfg.src);

	if (d_conn == NULL) {
	rv=diag_geterr();
		diag_l2_close(dl0d);
		return diag_iseterr(rv);
	}

	/* Connected ! */

	global_l2_conn = d_conn;

	return 0;
}

/*
 * Gets the data for every supported test using global L3 connection
 *
 * Returns <0 on failure, 0 on good and 1 on interrupted
 *
 * If Interruptible is 1, then this is interruptible by the stdin
 * becoming ready for read (using diag_os_ipending()), which amounts to "was Enter pressed".
 *
 * It is used in "Interuptible" mode when doing "monitor" command
 */
int
do_j1979_getdata(int interruptible)
{
	unsigned int i,j;
	int rv;
	struct diag_l3_conn *d_conn;
	ecu_data_t *ep;
	struct diag_msg *msg;

	d_conn = global_l3_conn;
	if (d_conn == NULL)
		return diag_iseterr(DIAG_ERR_GENERAL);

	diag_os_ipending();	//this is necessary on WIN32 to "purge" the last state of the enter key; we can't just poll stdin.

	/*
	 * Now get all the data supported
	 */
	for (i=3; i<0x100; i++) {
		if (merged_mode1_info[i]) {
			fprintf(stderr, "Requesting Mode 1 Pid 0x%02X...\n", i);
			rv = l3_do_j1979_rqst(d_conn, 0x1, (uint8_t) i, 0x00,
				0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
			if (rv < 0) {
				fprintf(stderr, "Mode 1 Pid 0x%02X request failed (%d)\n",
					i, rv);
			} else {
				msg = find_ecu_msg(0, 0x41);
				if (msg == NULL)
					fprintf(stderr, "Mode 1 Pid 0x%02X request no-data (%d)\n",
					i, rv);
			}

			if (interruptible) {
				if (diag_os_ipending())
					return 1;
			}
		}
	}

	/* Get mode2/pid2 (DTC that caused freezeframe) */
	fprintf(stderr, "Requesting Mode 0x02 Pid 0x02 (Freeze frame DTCs)...\n");
	rv = l3_do_j1979_rqst(d_conn, 0x2, 2, 0x00,
		0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);

	if (rv < 0) {
		fprintf(stderr, "Mode 0x02 Pid 0x02 request failed (%d)\n", rv);
		return DIAG_ERR_GENERAL;
	}
	msg = find_ecu_msg(0, 0x42);
	if (msg == NULL) {
		fprintf(stderr, "Mode 0x02 Pid 0x02 request no-data (%d)\n", rv);
		return DIAG_ERR_GENERAL;
	}
	diag_os_ipending();	//again, required for WIN32 to "purge" last keypress
	/* Now go thru the ECUs that have responded with mode2 info */
	for (j=0, ep=ecu_info; j<ecu_count; j++, ep++) {
		if ( (ep->mode1_data[2].type == TYPE_GOOD) &&
			(ep->mode1_data[2].data[2] |
				ep->mode1_data[2].data[3]) ) {
			for (i=3; i<=0x100; i++) {
				if (ep->mode2_info[i]) {
					fprintf(stderr, "Requesting Mode 0x02 Pid 0x%02X...\n", i);
					rv = l3_do_j1979_rqst(d_conn, 0x2, (uint8_t)i, 0x00,
						0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
					if (rv < 0) {
						fprintf(stderr, "Mode 0x02 Pid 0x%02X request failed (%d)\n", i, rv);
					} else {
						msg = find_ecu_msg(0, 0x42);
						if (msg == NULL) {
							fprintf(stderr, "Mode 0x02 Pid 0x%02X request no-data (%d)\n", i, rv);
							return DIAG_ERR_GENERAL;
						}
					}

				}
				if (interruptible) {
					if (diag_os_ipending())	//was Enter pressed
						return 1;
				}
			}
		}
	}
	return 0;
}

/*
 * Find out basic info from the ECU (what it supports, DTCs etc)
 *
 * This is the basic work horse routine
 */
void do_j1979_basics()
{
	ecu_data_t *ep;
	unsigned int i;
	int o2monitoring = 0;

	/*
	 * Get supported PIDs and Tests etc
	 */
	do_j1979_getpids();

	global_state = STATE_SCANDONE ;

	/*
	 * Get current DTCs/MIL lamp status/Tests supported for this ECU
	 * and test, and wait for those tests to complete
	 */
	do_j1979_getdtcs();

	/*
	 * Get data supported by ECU, non-interruptibly
	 */
	do_j1979_getdata(0);

	/*
	 * And now do stuff with that data
	 */
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if ( (ep->mode1_data[2].type == TYPE_GOOD) &&
			(ep->mode1_data[2].data[2] | ep->mode1_data[2].data[3]) ) {
			fprintf(stderr, "ECU %d Freezeframe data exists, caused by DTC ",
				i);
			print_single_dtc(ep->mode1_data[2].data[2] , ep->mode1_data[2].data[3]);
			fprintf(stderr, "\n");
		}

		if (ep->mode1_data[0x1c].type == TYPE_GOOD) {
			fprintf(stderr, "ECU %d is ", i);
			switch(ep->mode1_data[0x1c].data[2]) {
			case 1:
				fprintf(stderr, "OBD II (California ARB)");
				break;
			case 2:
				fprintf(stderr, "OBD (Federal EPA)");
				break;
			case 3:
				fprintf(stderr, "OBD and OBD II");
				break;
			case 4:
				fprintf(stderr, "OBD I");
				break;
			case 5:
				fprintf(stderr, "not OBD");
				break;
			case 6:
				fprintf(stderr, "EOBD (Europe)");
				break;
			default:
				fprintf(stderr, "unknown (%d)", ep->mode1_data[0x1c].data[2]);
				break;
			}
			fprintf(stderr, " compliant\n");
		}

		/*
		 * If ECU supports Oxygen sensor monitoring, then do O2 sensor
		 * stuff
		 */
		if ( (ep->mode1_data[1].type == TYPE_GOOD) &&
			(ep->mode1_data[1].data[4] & 0x20) ) {
			o2monitoring = 1;
		}
	}
	do_j1979_getO2sensors();
	if (o2monitoring > 0) {
		do_j1979_O2tests();
	} else {
		fprintf(stderr, "Oxygen (O2) sensor monitoring not supported\n");
	}
}

int
print_single_dtc(databyte_type d0, databyte_type d1)
{
	char buf[256];

	uint8_t db[2];
	db[0] = d0;
	db[1] = d1;

	fprintf(stderr, "%s",
		diag_dtc_decode(db, 2, NULL, NULL, dtc_proto_j2012, buf,
		sizeof(buf)));

	return 0;
}

static void
print_dtcs(uint8_t *data, uint8_t len)
{
	/* Print the DTCs just received */
	int i, j;

	for (i=0, j=1; (i<3) && ((j+1) < len); i++, j+=2) {
		if ((data[j]==0) && (data[j+1]==0))
			continue;
		print_single_dtc(data[j], data[j+1]);
	}
}

/*
 * Get test results for constantly monitored systems
 */
void
do_j1979_cms()
{
	int rv;
	unsigned int i;
	struct diag_l3_conn *d_conn;
	struct diag_msg *msg;

	d_conn = global_l3_conn;

	fprintf(stderr, "Requesting Mode 7 (Current cycle emission DTCs)...\n");
	rv = l3_do_j1979_rqst(d_conn, 0x07, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
	if (rv == DIAG_ERR_TIMEOUT) {
		/* Didn't get a response, this is valid if there are no DTCs */
		fprintf(stderr, "No DTCs stored.\n");
		return;
	}
	if (rv != 0) {
		fprintf(stderr, "Failed to get test results for continuously monitored systems\n");
		return;
	}

	fprintf(stderr, "Currently monitored DTCs: ");

	for (i=0; i<ecu_count;i++) {
		LL_FOREACH(ecu_info[i].rxmsg, msg) {
			print_dtcs(msg->data, msg->len);
		}

	}

	fprintf(stderr, "\n");
	return;
}


/*
 * Get test results for non-constantly monitored systems
 */
void
do_j1979_ncms(int printall)
{
	int rv;
	struct diag_l3_conn *d_conn;
	unsigned int i, j;
//	int supported=0;		//not used ?
	ecu_data_t *ep;

	uint8_t merged_mode6_info[0x100];

	d_conn = global_l3_conn;

	/* Merge all ECU mode6 info into one place*/
	memset(merged_mode6_info, 0, sizeof(merged_mode6_info));
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (j=0; j<sizeof(ep->mode6_info);j++) {
			merged_mode6_info[j] |= ep->mode6_info[j] ;
		}
		//if (ep->mode6_info[0] != 0)	//XXX not sure what this accomplished
			//supported = 1;	//this never gets used ...
	}

	if (merged_mode6_info[0] == 0x00) {
		/* Either not supported, or tests havent been done */
		do_j1979_getmodeinfo(6, 3);
	}

	if (merged_mode6_info[0] == 0x00) {
		fprintf(stderr, "ECU doesn't support non-continuously monitored system tests\n");
		return;
	}

	/*
	 * Now do the tests
	 */
	for (i=0 ; i < 60; i++) {
		if ((merged_mode6_info[i]) && ((i & 0x1f) != 0)) {
			/* Do test */
			fprintf(stderr, "Requesting Mode 6 TestID 0x%02X...\n", i);
			rv = l3_do_j1979_rqst(d_conn, 6, (uint8_t)i, 0x00,
				0x00, 0x00, 0x00, 0x00,
				(void *)(printall? &_RQST_HANDLE_NCMS:&_RQST_HANDLE_NCMS2));
			if (rv < 0) {
				fprintf(stderr, "Mode 6 Test ID 0x%02X failed\n", (unsigned int) i);
			}
		}
	}
	return;
}

/*
 * Get mode info
 * response_offset : index into received packet where the the supported_pid bytemasks start.
 * TODO: add return value to signal total failure (if l3_do_j1979_rqst lost the connection to
 * the ECU)
 */
void
do_j1979_getmodeinfo(uint8_t mode, int response_offset)
{
	int rv;
	struct diag_l3_conn *d_conn;
	int pid;
	unsigned int i, j;
	ecu_data_t *ep;
	int not_done;
	uint8_t *data;

	d_conn = global_l3_conn;

	/*
	 * Test 0, 0x20, 0x40, 0x60 (etc) for each mode returns information
	 * as to which tests are supported. Test 0 will return a bitmask 4
	 * bytes long showing which of the tests 0->0x1f are supported. Test
	 * 0x20 will show 0x20->0x3f etc
	 */
	for (pid = 0; pid < 0x100; pid += 0x20) {
		/*
		 * Do Mode 'mode' Pid 'pid' request to find out
		 * what is supported
		 */
		fprintf(stderr, "Exploring Mode 0x%02X supported PIDs (block 0x%02X)...\n", mode, pid);
		rv = l3_do_j1979_rqst(d_conn, mode, (uint8_t) pid, 0x00,
			0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
		if (rv != 0) {
			/* No response */
			break;
		}

		/* Process the results */
		for (j=0, ep=ecu_info, not_done = 0; j<ecu_count; j++, ep++) {
			if (ep->rxmsg == NULL)
				continue;
			if (ep->rxmsg->data[0] != (mode + 0x40))
				continue;

			/* Valid response for this request */

			/* Sort out where to store the received data */
			switch (mode) {
			case 1:
				data = ep->mode1_info;
				break;
			case 2:
				data = ep->mode2_info;
				break;
			case 5:
				data = ep->mode5_info;
				break;
			case 6:
				data = ep->mode6_info;
				break;
			case 8:
				data = ep->mode8_info;
				break;
			case 9:
				data = ep->mode9_info;
				break;
			default:
				data = NULL;
				break;
			}

			if (data == NULL)
				break;

			data[0] = 1;	/* Pid 0, 0x20, 0x40 always supported */
			for (i=0 ; i<=0x20; i++) {
				if (l2_check_pid_bits(&ep->rxmsg->data[response_offset], (int)i))
					data[i + pid] = 1;
			}
			if (data[0x20 + pid] == 1)
				not_done = 1;
		}

		/* Now, check if all ECUs said the next pid isnt supported */
		if (not_done == 0)
			break;
	}	//for
	return;
}


/*
 * Get the supported PIDs and Tests (Mode 1, 2, 5, 6, 9)
 *
 * This doesnt get the data for those pids, just the info as to
 * what the ECU supports
 */
void
do_j1979_getpids()
{
	ecu_data_t *ep;
	unsigned int i, j;

	do_j1979_getmodeinfo(1, 2);
	do_j1979_getmodeinfo(2, 2);
	do_j1979_getmodeinfo(5, 3);
	do_j1979_getmodeinfo(6, 3);
	do_j1979_getmodeinfo(8, 2);
	do_j1979_getmodeinfo(9, 3);

	/*
	 * Combine all the supported Mode1 PIDS
	 * from the ECUs into one bitmask, do same
	 * for Mode5
	 */
	memset(merged_mode1_info, 0, sizeof(merged_mode1_info));
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (j=0; j<sizeof(ep->mode1_info);j++) {
			merged_mode1_info[j] |= ep->mode1_info[j] ;
		}
	}

	memset(merged_mode5_info, 0, sizeof(merged_mode5_info));
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (j=0; j<sizeof(ep->mode5_info);j++) {
			merged_mode5_info[j] |= ep->mode5_info[j] ;
		}
	}
	return;
}

/*
 * Do the O2 tests for this O2 sensor
 */
void
do_j1979_O2tests()
{
	int i;

	if (merged_mode5_info[0] == 0) {
		fprintf(stderr, "Oxygen (O2) sensor tests not supported\n");
		return;
	}

	for (i=0; i<=7; i++) {
		if (global_O2_sensors & (1<<i))
			do_j1979_getO2tests(i);
	}
	return;
}


/*
 * Do O2 tests for O2Sensor
 *
 * O2sensor is the bit number
 */
void
do_j1979_getO2tests(int O2sensor)
{

	int rv;
	struct diag_l3_conn *d_conn;
	int i;

	uint8_t o2s = 1<<O2sensor ;

	d_conn = global_l3_conn;

	for (i=1 ; i<=0x1f; i++) {
		fprintf(stderr, "O2 Sensor %d Tests: -\n", O2sensor);
		if ((merged_mode5_info[i]) && ((i & 0x1f) != 0)) {
			/* Do test for of i + testID */
			fprintf(stderr, "Requesting Mode 0x05 TestID 0x%02X...\n", i);
			rv = l3_do_j1979_rqst(d_conn, 5, (uint8_t) i, o2s,
				0x00, 0x00, 0x00, 0x00,
				(void *) &_RQST_HANDLE_O2S);
			if ((rv < 0) || (find_ecu_msg(0, 0x45)==NULL)) {
				fprintf(stderr, "Mode 5 Test ID 0x%d failed\n", i);
			}
			/* Receive routine will have printed results */
		}
	}
}

/*
 * Get current DTCs/MIL lamp status/Tests supported for this ECU
 * and test, and wait for those tests to complete
 */
int
do_j1979_getdtcs()
{
	int rv;
	struct diag_l3_conn *d_conn;
	ecu_data_t *ep;
	unsigned int i;
	int num_dtcs, readiness, mil;

	d_conn = global_l3_conn;

	if (merged_mode1_info[1] == 0) {
		fprintf(stderr, "ECU(s) do not support DTC#/test query - can't do tests\n");
		return 0;
	}

	fprintf(stderr, "Requesting Mode 0x01 PID 0x01 (Current DTCs)...\n");
	rv = l3_do_j1979_rqst(d_conn, 1, 1, 0,
			0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);

	if ((rv < 0) || (find_ecu_msg(0, 0x41)==NULL)) {
		fprintf(stderr, "Mode 1 Pid 1 request failed %d\n", rv);
		return -1;
	}

	/* Go thru the received messages, and see readiness/MIL light */
	mil = 0; readiness = 0, num_dtcs = 0;

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x41)) {
			/* Go thru received msgs looking for DTC responses */
			if ( (ep->mode1_data[1].data[3] & 0xf0) ||
				ep->mode1_data[1].data[5] )
				readiness = 1;

			if (ep->mode1_data[1].data[2] & 0x80)
				mil = 1;

			num_dtcs += ep->mode1_data[1].data[2] & 0x7f;
		}

	}
	if (readiness == 1)
		fprintf(stderr, "Not all readiness tests have completed\n");

	if (mil == 1)
		fprintf(stderr, "MIL light ON, ");
	else
		fprintf(stderr, "MIL light OFF, ");

	fprintf(stderr, "%d stored DTC%c\n", num_dtcs, (num_dtcs==1)?' ':'s');

	if (num_dtcs) {
		/*
		 * Do Mode3 command to get DTCs
		 */

		fprintf(stderr, "Requesting Mode 0x03 (Emission DTCs)...\n");
		rv = l3_do_j1979_rqst(d_conn, 3, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
		if ((rv < 0) || (find_ecu_msg(0, 0x43)==NULL)) {
			fprintf(stderr, "ECU would not return DTCs\n");
			return -1;
		}

		/* Go thru received msgs looking for DTC responses */
		for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
			if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x43)) {
				struct diag_msg *msg;
				LL_FOREACH(ep->rxmsg, msg) {
					print_dtcs(msg->data, msg->len);
				}
				fprintf(stderr, "\n");
			}
		}
	}
	return 0;
}

/*
 * Get supported DTCS
 */
int
do_j1979_getO2sensors()
{
	int rv;
	struct diag_l3_conn *d_conn;
	unsigned int i, j;
	int num_sensors;
	ecu_data_t *ep;

	d_conn = global_l3_conn;

	global_O2_sensors = 0;
	num_sensors = 0;

	fprintf(stderr, "Requesting Mode 0x01 PID 0x13 (O2 sensors location)...\n");
	rv = l3_do_j1979_rqst(d_conn, 1, 0x13, 0,
			0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);

	if ((rv < 0) || (find_ecu_msg(0, 0x41)==NULL)) {
		fprintf(stderr, "Mode 1 Pid 0x13 request failed %d\n", rv);
		return 0;
	}

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x41)) {
			/* Maintain bitmap of sensors */
			global_O2_sensors |= ep->rxmsg->data[2];
			/* And count additional sensors on this ECU */
			for (j=0; j<=7; j++) {
				if (ep->rxmsg->data[2] & (1<<j))
					num_sensors++;
			}
		}
	}

	fprintf(stderr, "%d Oxygen (O2) sensors in vehicle\n", num_sensors);

	return 0;
}

int
diag_cleardtc(void)
{
	/* Clear DTCs */
	struct diag_l3_conn *d_conn;
	int rv;
	struct diag_msg	*rxmsg;

	d_conn = global_l3_conn;
	fprintf(stderr, "Requesting Mode 0x04 (Clear DTCs)...\n");
	rv = l3_do_j1979_rqst(d_conn, 0x04, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);

	rxmsg = find_ecu_msg(0, 0x44);

	if (rxmsg == NULL) {
		fprintf(stderr, "ClearDTC requested failed - no appropriate response\n");
		return -1;
	}

	return rv;
}

typedef int (start_fn)(int);

struct protocol {
	const char	*desc;
	start_fn *start;
	int	flags;
};

const struct protocol protocols[] = {
	{"SAEJ1850-VPW", do_l2_j1850_start, DIAG_L1_J1850_VPW},
	{"SAEJ1850-PWM", do_l2_j1850_start, DIAG_L1_J1850_PWM},
	{"ISO14230_FAST", do_l2_14230_start, DIAG_L2_TYPE_FASTINIT},
	{"ISO9141", do_l2_9141_start, 0x33},
	{"ISO14230_SLOW", do_l2_14230_start, DIAG_L2_TYPE_SLOWINIT},
};

/*
 * Connect to ECU by trying all protocols
 * - We do the fast initialising protocols before the slow ones
 * This will set global_l3_conn. Ret 0 if ok
 */
int
ecu_connect(void)
{
	int connected=0;
	int rv = DIAG_ERR_GENERAL;
	const struct protocol *p;

	if ((global_state >= STATE_CONNECTED) || (global_l3_conn != NULL)) {
		printf("ecu_connect() : already connected !\n");
		return DIAG_ERR_GENERAL;
	}


	for (p = protocols; !connected && p < &protocols[ARRAY_SIZE(protocols)]; p++) {
		fprintf(stderr,"\nTrying %s:\n", p->desc);
		rv = p->start(p->flags);
		if (rv == 0) {
			struct diag_l3_conn *d_l3_conn;

			fprintf(stderr, "L2 connection OK; tring to add SAE J1979 layer...\n");
	//At this point we have a valid L2 connection, but it is possible that
	//no communication has been established with an ECU yet (see DIAG_L2_FLAG_CONNECTS_ALWAYS)
	//To confirm we really have a connection we try to start the J1979 L3 layer and try
	//sending a J1979 keep-alive request (service 1 pid 0).
	//diag_l3_start() does exactly that.

			d_l3_conn = diag_l3_start("SAEJ1979", global_l2_conn);
			if (d_l3_conn == NULL) {
				rv=DIAG_ERR_ECUSAIDNO;
				fprintf(stderr, "Failed to enable SAEJ1979 mode\n");
				//So we'll try another protocol. But close that L2 first:
				diag_l2_StopCommunications(global_l2_conn);
				diag_l2_close(global_dl0d);

				global_l2_conn = NULL;
				global_state = STATE_IDLE;
				continue;
			}
			global_l3_conn = d_l3_conn;
			global_state = STATE_L3ADDED;

			fprintf(stderr, "%s Connected.\n", p->desc);
			break;	//exit for loop

		} else {
			fprintf(stderr, "%s Failed!\n", p->desc);
		}
	}

	if (diag_cli_debug)
		fprintf(stderr, "debug: L2 connection ID %p, L3 ID %p\n",
			(void *)global_l2_conn, (void *)global_l3_conn);

	return rv? diag_iseterr(rv):0;
}

/*
 * Initialise
 */
static int
do_init(void)
{
	clear_data();

	return 0;
}

/*
 * Explain command line usage
 */
static void do_usage (void)
{
	fprintf( stderr, "FreeDiag ScanTool:\n\n" ) ;
	fprintf( stderr, "  Usage -\n" ) ;
	fprintf( stderr, "	scantool [-h][-a|-c][-f <file]\n\n" ) ;
	fprintf( stderr, "  Where:\n" ) ;
	fprintf( stderr, "\t-h   -- Display this help message\n" ) ;
	fprintf( stderr, "\t-a   -- Start in Application/Interface mode\n" ) ;
	fprintf( stderr, "\t		(some other program provides the\n" ) ;
	fprintf( stderr, "\t		user interface)\n" ) ;
	fprintf( stderr, "\t-c   -- Start in command-line interface mode\n" ) ;
	fprintf( stderr, "\t		(this is the default)\n" ) ;
	fprintf( stderr, "\t-f <file> Runs the commands from <file> at startup\n");
	fprintf( stderr, "\n" ) ;
}



static void format_o2(char *buf, int maxlen, UNUSED(int english),
	const struct pid *p, response_t *data, int n)
{
		double v = DATA_SCALED(p, DATA_1(p, n, data));
		int t = DATA_1(p, n + 1, data);

		if (t == 0xff)
				snprintf(buf, maxlen, p->fmt1, v);
		else
				snprintf(buf, maxlen, p->fmt2, v, t * p->scale2 + p->offset2);
}


static void
format_aux(char *buf, int maxlen, UNUSED(int english), const struct pid *p,
	response_t *data, int n)
{
		snprintf(buf, maxlen, (DATA_RAW(p, n, data) & 1) ? "PTO Active" : "----");
}



static void
format_fuel(char *buf, int maxlen, UNUSED(int english), const struct pid *p,
	response_t *data, int n)
{
		int s = DATA_1(p, n, data);

		switch (s) {
		case 1 << 0:
			snprintf(buf, maxlen, "Open");
			break;
		case 1 << 1:
			snprintf(buf, maxlen, "Closed");
			break;
		case 1 << 2:
			snprintf(buf, maxlen, "Open-Driving");
			break;
		case 1 << 3:
			snprintf(buf, maxlen, "Open-Fault");
			break;
		case 1 << 4:
			snprintf(buf, maxlen, "Closed-Fault");
			break;
		default:
			snprintf(buf, maxlen, "Open(rsvd)");
			break;
		}

		/* XXX Fuel system 2 status */
}


static void
format_data(char *buf, int maxlen, int english, const struct pid *p, response_t *data, int n)
{
		double v;

		v = DATA_SCALED(p, DATA_RAW(p, n, data));
		if (english && p->fmt2)
				snprintf(buf, maxlen, p->fmt2, DATA_ENGLISH(p, v));
		else
				snprintf(buf, maxlen, p->fmt1, v);
}


/* conversion factors from the "units" package */

static const struct pid pids[] = {
	{0x03, "Fuel System Status", format_fuel, 2,
		"", 0.0, 0.0,
		"", 0.0, 0.0},
	{0x04, "Calculated Load Value", format_data, 1,
		"%5.1f%%", (100.0/255), 0.0,
		"", 0.0, 0.0},
	{0x05, "Engine Coolant Temperature", format_data, 1,
		"%3.0fC", 1, -40,
		"%3.0fF", 1.8, 32},
	{0x06, "Short term fuel trim Bank 1", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
	{0x07, "Long term fuel trim Bank 1", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
	{0x08, "Short term fuel trim Bank 2", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
	{0x09, "Long term fuel trim Bank 2", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
	{0x0a, "Fuel Pressure", format_data, 1,
		"%3.0fkPaG", 3.0, 0.0,
		"%4.1fpsig", 0.14503774, 0.0},
	{0x0b, "Intake Manifold Pressure", format_data, 1,
		"%3.0fkPaA", 1.0, 0.0,
		"%4.1finHg", 0.29529983, 0.0},
	{0x0c, "Engine RPM", format_data, 2,
		"%5.0fRPM", 0.25, 0.0,
		"", 0.0, 0.0},
	{0x0d, "Vehicle Speed", format_data, 1,
		"%3.0fkm/h", 1.0, 0.0,
		"%3.0fmph", 0.62137119, 0.0},
	{0x0e, "Ignition timing advance Cyl #1", format_data, 1,
		"%4.1f deg", 0.5,	-64.0,
		"", 0.0, 0.0},
	{0x0f, "Intake Air Temperature", format_data, 1,
		"%3.0fC", 1.0, -40.0,
		"%3.0fF", 1.8, 32.0},
	{0x10, "Air Flow Rate", format_data, 2,
		"%6.2fgm/s", 0.01, 0.0,
		"%6.1flb/min", 0.13227736, 0.0},
	{0x11, "Absolute Throttle Position", format_data, 1,
		"%5.1f%%", (100.0/255), 0.0,
		"", 0.0, 0.0},
	{0x12, "Commanded Secondary Air Status", format_data, 1,
		"", 0, 0,
		"", 0, 0},	//can't format bit fields
	{0x13, "Location of Oxygen Sensors", format_data, 1,
		"", 0, 0,
		"", 0, 0},	//can't format bit fields
	{0x14, "Bank 1 Sensor 1 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x15, "Bank 1 Sensor 2 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x16, "Bank 1 Sensor 3 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x17, "Bank 1 Sensor 4 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x18, "Bank 2 Sensor 1 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x19, "Bank 2 Sensor 2 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x1a, "Bank 2 Sensor 3 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x1b, "Bank 2 Sensor 4 Voltage/Trim", format_o2, 2,
		"%5.3fV", 0.005, 0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
	{0x1e, "Auxiliary Input Status", format_aux, 1,
		"", 0.0, 0.0,
		"", 0.0, 0.0},
};


const struct pid *get_pid ( unsigned int i )
{
	if ( i >= ARRAY_SIZE(pids) )
		return NULL ;

	return & pids[i] ;
}


/*
 * Main
 */

int
main(int argc, char **argv)
{
	int user_interface = 1 ;
	int i ;
	const char *startfile=NULL;	/* optional commands to run at startup */

	for ( i = 1 ; i < argc ; i++ ) {
		if ( argv[i][0] == '-' || argv[i][0] == '+' ) {
			switch ( argv[i][1] ) {
			case 'c' : user_interface = 1 ; break ;
			case 'a' : user_interface = 0 ; break ;
			case 'f' :
				user_interface = 1;
				i++;
				if (i < argc) {
					startfile = argv[i];
				} else {
					do_usage();
					exit(1);
				}
				break;
			case 'h' : do_usage() ; exit(0 ) ;
			default : do_usage() ; exit(1) ;
			}
		} else {
			do_usage() ;
			exit(1) ;
		}
	}

	/* Input buffer */

	do_init();

	if ( user_interface )
		enter_cli(progname, startfile);
	else
		enter_aif(progname );

	/* Done */
	exit(0);
}


int
cmd_up(UNUSED(int argc), UNUSED(char **argv))
{
	return CMD_UP;
}


int
cmd_exit(UNUSED(int argc), UNUSED(char **argv))
{
	return CMD_EXIT;
}

