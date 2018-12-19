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
 * Mostly ODBII Compliant Scan Tool (as defined in SAE J1978)
 *
 * CLI routines - diag subcommand
 *
 * This is extended stuff for playing with ECUs, allowing you to
 * start a L2 connection to an ECU, add a L3 connection etc
 *
 */

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_err.h"

#include "scantool.h"
#include "scantool_cli.h"


static int cmd_diag_help(int argc, char **argv);

static int cmd_diag_disconnect(int argc, char **argv);
static int cmd_diag_connect(int argc, char **argv);
static int cmd_diag_sendreq(int argc, char **argv);
static int cmd_diag_read(int argc, char **argv);

static int cmd_diag_addl3(int argc, char **argv);
static int cmd_diag_reml3(UNUSED(int argc), UNUSED(char **argv));

static int cmd_diag_probe(int argc, char **argv);
static int cmd_diag_fastprobe(int argc, char **argv);

const struct cmd_tbl_entry diag_cmd_table[] = {
	{ "help", "help [command]", "Gives help for a command",
		cmd_diag_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
		cmd_diag_help, FLAG_HIDDEN, NULL},

	{ "connect", "connect", "Connect to ECU", cmd_diag_connect, 0, NULL},

	{ "disconnect", "disconnect", "Disconnect from ECU", cmd_diag_disconnect,
		0, NULL},

	{ "sendreq", "sendreq [byte0 [byte1 [...]]]", "Send raw data to the ECU and print response",
		cmd_diag_sendreq, 0, NULL},
	{ "sr", "sendreq [byte0 [byte1 [...]]]", "Send a command to the ECU and print response",
		cmd_diag_sendreq, FLAG_HIDDEN, NULL},
	{ "read", "read [waittime]",
		"Receive some data from the ECU waiting waittime seconds",
		cmd_diag_read, 0, NULL},
	{ "rx", "read [waittime]", "Receive some data from the ECU",
		cmd_diag_read, FLAG_HIDDEN, NULL},

	{ "addl3", "addl3 [protocol]", "Add (start) a L3 protocol",
		cmd_diag_addl3, 0, NULL},
	{ "reml3", "reml3", "Remove (stop) an L3 protocol",
		cmd_diag_reml3, 0, NULL},

	{ "probe", "probe start_addr [stop_addr]", "Scan bus using ISO9141 5 baud init [slow!]", cmd_diag_probe, 0, NULL},
	{ "fastprobe", "fastprobe start_addr [stop_addr [func]]", "Scan bus using ISO14230 fast init with physical or functional addressing", cmd_diag_fastprobe, 0, NULL},
	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Exit program",
		cmd_exit, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

static int
cmd_diag_help(int argc, char **argv) {
	return help_common(argc, argv, diag_cmd_table);
}

static int
cmd_diag_addl3(int argc, char **argv) {
	int i;
	const char *proto;

	if (argc != 2) {
		return CMD_USAGE;
	}

	if (strcmp(argv[1], "?") == 0) {
		printf("Valid protocols are: ");
		for (i=0; diag_l3_protocols[i] != NULL; i++) {
			printf("%s ", diag_l3_protocols[i]->proto_name);
		}
		printf("\n");
		return CMD_USAGE;
	}

	/* Add a L3 stack above the open L2 */
	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_FAILED;
	}

	if (global_state >= STATE_L3ADDED) {
		printf("L3 protocol already connected\n");
		return CMD_OK;
	}

	if (global_l3_conn != NULL) {
		fprintf(stderr, FLFMT "Oops : there's a global L3 conn with an invalid global_state ! Report this !\n", FL);
		return CMD_FAILED;
	}

	//match specified L3proto with available protos
	for (i=0, proto = NULL; diag_l3_protocols[i] != NULL; i++) {
		if (strcasecmp(diag_l3_protocols[i]->proto_name, argv[1]) == 0) {
			proto = diag_l3_protocols[i]->proto_name;
			break;
		}
	}

	if (proto == NULL) {
		printf("No such protocol, use %s ? for list of protocols\n",
			argv[0]);
		return CMD_OK;
	}

	//use the global L2 connection to start an L3 connection.
	global_l3_conn = diag_l3_start(proto, global_l2_conn);

	if (global_l3_conn !=NULL) {
		global_state = STATE_L3ADDED ;
		printf("Done\n");
	} else {
		printf("Failed to add L3 protocol\n");
	}


	return CMD_OK;
}

// cmd_diag_reml3 : undoes what diag_addl3 did.
static int cmd_diag_reml3(UNUSED(int argc), UNUSED(char **argv)) {
	int rv;
	struct diag_l3_conn *old_dl3c = global_l3_conn;

	if (global_l3_conn == NULL) {
		printf("No active global L3 connection.\n");
		return CMD_OK;
	}

	if (global_state < STATE_L3ADDED) {
		printf("Global state wasn't set properly ? Report this !\n");
		return CMD_FAILED;
	}

	global_l3_conn = global_l3_conn->next;	//in case there was more than 1

	rv=diag_l3_stop(old_dl3c);

	if (global_l3_conn == NULL) {
		global_state = STATE_CONNECTED; // we probably still have an L2
						// hanging there
	}

	return rv? diag_ifwderr(rv):0;
}


//cmd_diag_prob_common [startaddr] [stopaddr]
//This should stop searching at the first succesful init
//and update the global connection
static int
cmd_diag_probe_common(int argc, char **argv, int fastflag) {
	unsigned int start, end, i;
	int rv;
	struct diag_l0_device *dl0d = global_dl0d;
	struct diag_l2_conn *d_conn;
	uint32_t funcmode = 0;

	if (argc < 2) {
		return CMD_USAGE;
	}
	if (strcmp(argv[1], "?") == 0) {
		return CMD_USAGE;
	}

	if (global_state != STATE_IDLE) {
		printf("Cannot probe while there is an active global connection.\n");
		return CMD_FAILED;
	}

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return CMD_FAILED;
	}

	start = htoi(argv[1]);

	if (argc == 2) {
		end = start;
	} else {
		end = htoi(argv[2]);
	}


	if (fastflag && argc>=4) {
		if (strcasecmp(argv[3], "func") == 0) {
			funcmode = DIAG_L2_TYPE_FUNCADDR;
		}
	}

	if ((start > 255) || (end > 255)) {
		printf("Values must be between 0 and 255\n");
		return CMD_OK;
	}
	if (end < start) {
		printf("Start must not be greater than End address\n");
		return CMD_OK;
	}

	/* Open interface using hardware type ISO9141 */
	rv = diag_l2_open(dl0d, DIAG_L1_ISO9141);
	if (rv) {
		printf("Failed to open hardware interface, error 0x%X",rv);
		if (rv == DIAG_ERR_PROTO_NOTSUPP) {
			printf(", does not support requested L1 protocol\n");
		} else if (rv == DIAG_ERR_BADIFADAPTER) {
			printf(", adapter probably not connected\n");
		} else {
			printf("\n");
		}
		return CMD_FAILED;
	}

	printf("Scanning:\n");
	for (i=start; i<=end; i++) {
		printf("\t0x%X ", i);
		fflush(stdout) ;

		if (fastflag) {
			d_conn = diag_l2_StartCommunications(dl0d,
				DIAG_L2_PROT_ISO14230,
				DIAG_L2_TYPE_FASTINIT | funcmode,
				global_cfg.speed, (target_type) i, global_cfg.src);
		} else {
			d_conn = diag_l2_StartCommunications(
				dl0d, DIAG_L2_PROT_ISO9141,
				DIAG_L2_TYPE_SLOWINIT, global_cfg.speed,
				(target_type)i, global_cfg.src);
		}

		if (d_conn == NULL) {
			continue;
		}

		int gotsome;
		struct diag_l2_data d;

		printf(" connected !!\n");
		fflush(stdout);

		global_state = STATE_CONNECTED;
		global_l2_conn = d_conn;

		/* Get the keybytes */
		diag_l2_ioctl(d_conn, DIAG_IOCTL_GET_L2_DATA, &d);
		if (fastflag) {
			printf("Keybytes: 0x%X 0x%X\n", d.kb1, d.kb2);
		} else {
			printf("received: 0x%X 0x%X\n", d.kb1, d.kb2);
		}

		/* Now read some data */

		rv = 0; gotsome = 0;
		while (rv >= 0) {
			rv = diag_l2_recv(d_conn, 100, l2raw_data_rcv, NULL);
			if (rv > 0) {
				gotsome = 1;
			}
		}
		if (gotsome) {
			printf("\n");
		} else if (rv != DIAG_ERR_TIMEOUT) {
			printf("- read failed %d\n", rv);
		}

		return CMD_OK;
	}	//for addresses
	//Failed => clean up
	diag_l2_close(dl0d);
	printf("\n");
	return CMD_OK;
}

static int
cmd_diag_probe(int argc, char **argv) {
	return cmd_diag_probe_common(argc, argv, 0);
}

static int
cmd_diag_fastprobe(int argc, char **argv) {
	return cmd_diag_probe_common(argc, argv, 1);
}


/*
 * Generic init, using parameters set by user.
 * Currently only called from cmd_diag_connect;
 */
static int
do_l2_generic_start(void) {
	struct diag_l2_conn *d_conn;
	struct diag_l0_device *dl0d = global_dl0d;
	int rv;
	flag_type flags = 0;

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	/* Open interface using current L1 proto and hardware */
	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "l2_generic_start: open failed for protocol %d on %s\n",
			global_cfg.L1proto, dl0d->dl0->shortname);
		return diag_ifwderr(rv);
	}

	if (global_cfg.addrtype) {
		flags = DIAG_L2_TYPE_FUNCADDR;
	} else {
		flags = 0;
	}

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



//cmd_diag_connect : attempt to connect to ECU
//using the current global l2proto, l1proto, etc.
static int
cmd_diag_connect(UNUSED(int argc), UNUSED(char **argv)) {
	int rv;

	if (argc > 1) {
		return CMD_USAGE;
	}

	if (global_state >= STATE_CONNECTED) {
		printf("Already connected !\n");
		return CMD_OK;
	}

	rv = do_l2_generic_start();
	if (rv==0) {
		printf("Connection to ECU established!\n");
		global_state = STATE_CONNECTED;
	} else {
		printf("\nConnection to ECU failed\n");
		printf("Please check :\n");
		printf("\tAdapter is connected to PC\n");
		printf("\tCable is connected to Vehicle\n");
		printf("\tVehicle is switched on\n");
	}
	return CMD_OK;
}


//Currently, this stops + removes the current global L3 conn.
//If there are no more L3 conns, also stop + close the global L2 conn.
static int
cmd_diag_disconnect(UNUSED(int argc), UNUSED(char **argv)) {
	if (argc > 1) {
		return CMD_USAGE;
	}

	if (global_state < STATE_CONNECTED) {
		return CMD_OK;
	}

	if (global_state >= STATE_L3ADDED) {
		/* Close L3 protocol */
		cmd_diag_reml3(0,NULL);
	}

	if (global_l3_conn == NULL) {
		// no other l3 conns, so stop the global L2 conn
		diag_l2_StopCommunications(global_l2_conn);
		diag_l2_close(global_dl0d);

		global_l2_conn = NULL;
		global_state = STATE_IDLE;
	} else {
		printf("There is another active L3 connection : %p\n : %s",
			(void *) global_l3_conn, global_l3_conn->d_l3_proto->proto_name);
		printf("Run disconnect again to close it.\n");
		return CMD_OK;
	}

	return CMD_OK;
}


static int
cmd_diag_read(int argc, char **argv) {
	unsigned int timeout = 0;

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	if (argc > 1) {
		timeout = (unsigned int)atoi(argv[1]) * 1000;
	}

	if (global_state < STATE_L3ADDED) {
		/* No L3 protocol, do L2 stuff */
		(void)diag_l2_recv(global_l2_conn, timeout, l2raw_data_rcv,
			NULL);

	} else {
		(void)diag_l3_recv(global_l3_conn, timeout, j1979_data_rcv,
			(void *)&_RQST_HANDLE_WATCH);
	}
	return CMD_OK;
}

/*
 * Send some data, and wait for a response
 */
static int
cmd_diag_sendreq(int argc, char **argv) {
	uint8_t	data[MAXRBUF];
	unsigned int	i,j,len;
	int	rv;

	if (argc < 2) {
		printf("Too few arguments\n");
		return CMD_USAGE;
	}

	if (strcmp(argv[1], "?") == 0) {
		return CMD_USAGE;
	}

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	memset(data, 0, sizeof(data));

	for (i=1, j=0; (i < (unsigned int) argc) && (i < sizeof(data)); i++, j++) {
		data[j] = (uint8_t) htoi(argv[i]);
	}
	len = j ;


	if (global_state < STATE_L3ADDED) {
		rv = l2_do_send( global_l2_conn, data, len,
			(void *)&_RQST_HANDLE_DECODE);
	} else {
		/* Send data with handle to tell callback to print results */
		rv = l3_do_send( global_l3_conn, data, len,
			(void *)&_RQST_HANDLE_DECODE);
	}

	if (rv != 0) {
		if (rv == DIAG_ERR_TIMEOUT) {
			printf("No data received\n");
		} else {
			printf("sendreq: failed error %d\n", rv);
		}
	}
	return CMD_OK;
}
