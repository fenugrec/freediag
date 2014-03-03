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

#include "scantool.h"
#include "scantool_cli.h"

CVSID("$Id$");

static int cmd_diag_help(int argc, char **argv);

static int cmd_diag_disconnect(int argc, char **argv);
static int cmd_diag_connect(int argc, char **argv);
static int cmd_diag_sendreq(int argc, char **argv);
static int cmd_diag_read(int argc, char **argv);

static int cmd_diag_addl3(int argc, char **argv);

static int cmd_diag_probe(int argc, char **argv);
static int cmd_diag_fastprobe(int argc, char **argv);

const struct cmd_tbl_entry diag_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_diag_help, 0, NULL},

	{ "connect", "connect", "Connect to ECU", cmd_diag_connect, 0, NULL},

	{ "disconnect", "disconnect", "Disconnect from ECU", cmd_diag_disconnect,
		0, NULL},

	{ "sendreq", "sendreq data0 data1 data2 ...", "Send a command to the ECU and print response",
		cmd_diag_sendreq, 0, NULL},
	{ "sr", "sendreq data0 data1 data2 ...", "Send a command to the ECU and print response",
		cmd_diag_sendreq, FLAG_HIDDEN, NULL},
	{ "read", "read [waittime]",
		"Receive some data from the ECU waiting waittime seconds",
		cmd_diag_read, 0, NULL},
	{ "rx", "read [waittime]", "Receive some data from the ECU",
		cmd_diag_read, FLAG_HIDDEN, NULL},

	{ "addl3", "addl3 protocol", "Add [start] a L3 protocol",
		cmd_diag_addl3, 0, NULL},

	{ "probe", "probe start_addr stop_addr", "Scan bus using ISO9141 5 baud init [slow!]", cmd_diag_probe, 0, NULL},
	{ "fastprobe", "fastprobe start_addr stop_addr [func]", "Scan bus using ISO14230 fast init with physical or functional addressing", cmd_diag_fastprobe, 0, NULL},
	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Return to previous menu level",
		cmd_up, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

static int
cmd_diag_help(int argc, char **argv)
{
	return help_common(argc, argv, diag_cmd_table);
}

static int
cmd_diag_addl3(int argc, char **argv)
{
	int i;
	const char *l3_protos[] = { "SAEJ1979", "VAG", "ISO14230", NULL };
	const char *proto;

	/* Add a L3 stack above the open L2 */
	if (global_state < STATE_CONNECTED)
	{
		printf("Not connected to ECU\n");
		return CMD_OK;
	}
	if (global_state > STATE_CONNECTED)
	{
		printf("L3 protocol already connected\n");
		return CMD_OK;
	}
	if (argc == 1)
	{
		return CMD_USAGE;
	}
	if (strcmp(argv[1], "?") == 0)
	{
		printf("Valid protocols are: ");
		for (i=0; l3_protos[i] != NULL; i++)
		{
			printf("%s ", l3_protos[i]);
		}
		printf("\n");
		return CMD_OK;
	}
	for (i=0, proto = NULL; l3_protos[i] != NULL; i++)
	{
		if (strcasecmp(l3_protos[i], argv[1]) == 0)
		{
			proto = l3_protos[i];
			break;
		}
	}
	if (proto == NULL)
	{
		printf("No such protocol, use %s ? for list of protocols\n",
			argv[0]);
		return CMD_OK;
	}
	else
	{
		global_l3_conn = diag_l3_start(proto, global_l2_conn);

		if (global_l3_conn) {
			global_state = STATE_L3ADDED ;
			printf("Done\n");
		}
		else
		{
			printf("Failed to add L3 protocol\n");
		}
	}

	return CMD_OK;
}


static int
cmd_diag_probe_common(int argc, char **argv, int fastflag)
{
	unsigned int start, end, i;
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l2_conn *d_conn;
	uint32_t funcmode = 0;

	if (argc < 2)
		return CMD_USAGE;

	start = htoi(argv[1]);
	if (argc == 2) 
	{
		end = start; 
	}
	else
	{
		end = htoi(argv[2]);
	}


	if (fastflag)
	{
		if (strcasecmp(argv[3], "func") == 0)
			funcmode = DIAG_L2_TYPE_FUNCADDR;
	}

	if ((start > 255) || (end > 255))
	{
		printf("Values must be between 0 and 255\n");
		return CMD_OK;
	}
	if (end < start)
	{
		printf("Start must not be greater than End address\n");
		return CMD_OK;
	}

	rv = diag_init();
	if (rv < 0)
	{
		printf("Failed to initialise diagnostic layer\n");
		diag_close();
		return CMD_OK;
	}
	/* Open interface using hardware type ISO9141 */
	dl0d = diag_l2_open(l0_names[set_interface_idx].longname, set_subinterface,
		DIAG_L1_ISO9141);
	if (dl0d == 0)
	{
		rv = diag_geterr();
		printf("Failed to open hardware interface, error 0x%X",rv);
		if (rv == DIAG_ERR_PROTO_NOTSUPP)
			printf(", does not support requested L1 protocol\n");
		else if (rv == DIAG_ERR_BADIFADAPTER)
			printf(", adapter probably not connected\n");
		else
			printf("\n");
		return CMD_OK;
	}

	printf("Scanning address : ");
	for (i=start; i<=end; i++)
	{
		printf("0x%x ", i);
		fflush(stdout) ;


		if (fastflag)
			d_conn = diag_l2_StartCommunications(dl0d,
				DIAG_L2_PROT_ISO14230,
				DIAG_L2_TYPE_FASTINIT | funcmode,
				set_speed, i, set_testerid);
		else
			d_conn = diag_l2_StartCommunications(dl0d,
				DIAG_L2_PROT_ISO9141,
				DIAG_L2_TYPE_SLOWINIT,
				set_speed, i, set_testerid);

		if (d_conn != NULL)
		{
			int gotsome;
			struct diag_l2_data d;

			printf(" connected !!\n");
			fflush(stdout);

			global_state = STATE_CONNECTED;
			global_l2_conn = d_conn;

			/* Get the keybytes */
			diag_l2_ioctl(d_conn, DIAG_IOCTL_GET_L2_DATA, &d);
			if (fastflag)
				printf("Keybytes: 0x%x 0x%x\n", d.kb1, d.kb2);
			else
				printf("msg 00: 0x%x 0x%x\n", d.kb1, d.kb2);

			/* Now read some data */

			rv = 0; gotsome = 0;
			while (rv >= 0)
			{
				rv = diag_l2_recv(d_conn, 2000, l2raw_data_rcv, NULL);
				if (rv > 0)
					gotsome = 1;
			}
			if (gotsome)
				printf("\n");
			else if (rv != DIAG_ERR_TIMEOUT)
				printf("- read failed %d\n", rv);

			/* XXX until we can disconnect from an ECU */
			return CMD_OK;
		}
	}
	diag_l2_close(dl0d);
	diag_close();	//opposite of diag_init()
	printf("\n");
	return CMD_OK;
}

static int
cmd_diag_probe(int argc, char **argv)
{
	return cmd_diag_probe_common(argc, argv, 0);
}

static int
cmd_diag_fastprobe(int argc, char **argv)
{
	return cmd_diag_probe_common(argc, argv, 1);
}

#ifdef WIN32
static int
cmd_diag_connect(int argc,
char **argv)
#else
static int
cmd_diag_connect(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	int rv;

	rv = do_l2_generic_start();
	if (rv==0)
	{
		printf("Connection to ECU established\n");
		global_state = STATE_CONNECTED;
	}
	else
	{
		printf("\nConnection to ECU failed\n");
		printf("Please check :-\n");
		printf("	Adapter is connected to PC\n");
		printf("	Cable is connected to Vehicle\n");
		printf("	Vehicle is switched on\n");
	}
	return CMD_OK;
}

#ifdef WIN32
static int
cmd_diag_disconnect(int argc,
char **argv)
#else
static int
cmd_diag_disconnect(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	if (global_state < STATE_CONNECTED)
	{
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	if (global_state >= STATE_L3ADDED)
	{
		/* Close L3 protocol */
		diag_l3_stop(global_l3_conn);
	}
	diag_l2_StopCommunications(global_l2_conn);
	diag_l2_close(global_l2_dl0d);

	global_l2_conn = NULL;
	global_state = STATE_IDLE;

	return CMD_OK;
}


static int
cmd_diag_read(int argc, char **argv)
{
	int timeout = 0;

	if (global_state < STATE_CONNECTED)
	{
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	if (argc > 1)
		timeout = atoi(argv[1]) * 1000;

	if (global_state < STATE_L3ADDED)
	{
		/* No L3 protocol, do L2 stuff */
		(void)diag_l2_recv(global_l2_conn, timeout, l2raw_data_rcv,
			NULL);

	}
	else
	{
		(void)diag_l3_recv(global_l3_conn, timeout, j1979_data_rcv,
			(void *)RQST_HANDLE_WATCH);
	}
	return CMD_OK;
}

/*
 * Send some data, and wait for a response
 */
static int
cmd_diag_sendreq(int argc, char **argv)
{
	uint8_t	data[MAXRBUF];
	unsigned int	len;
	int	i, j, rv;

	if (global_state < STATE_CONNECTED)
	{
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	if (argc < 2)
	{
		printf("Too few arguments\n");
		return CMD_USAGE;
	}

	memset(data, 0, sizeof(data));
	
	for (i=1, j=0; i < argc; i++, j++)
		data[j] = htoi(argv[i]);
	len = j ;


	if (global_state < STATE_L3ADDED)
	{
		rv = l2_do_send( global_l2_conn, data, len,
			(void *)RQST_HANDLE_DECODE);
	}
	else
	{
		/* Send data with handle to tell callback to print results */
		rv = l3_do_send( global_l3_conn, data, len,
			(void *)RQST_HANDLE_DECODE);
	}

	if (rv != 0)
	{
		if (rv == DIAG_ERR_TIMEOUT)
			printf("No data received\n");
		else
			printf("sendreq: failed error %d\n", rv);
	}
	return CMD_OK;
}


