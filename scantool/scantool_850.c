/*
 *      freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017 Adam Goldman
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
 * CLI routines - 850 subcommand
 *
 * Extended diagnostics for '96-'98 Volvo 850, S40, C70, S70, V70, XC70 and V90
 *
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_err.h"

#include "diag_l7_volvo.h"
#include "scantool.h"
#include "scantool_cli.h"

struct ecu_info {
	uint8_t addr;
	char *name;
	char *desc;
	char *dtc_prefix;
};

struct ecu_info ecu_list[] = {
	{0x01, "abs", "antilock brakes", "ABS"},
#if 0
	/*
	 * Address 0x10 communicates by KWP71 protocol, so we currently
	 * can't talk to it.
	 */
	{0x10, "m43", "Motronic M4.3 (DLC pin 3)", "EFI"}, /* 12700bps */
	{0x10, "m44old", "Motronic M4.4 (old protocol)", "EFI"}, /* 9600bps */
#endif
	{0x11, "msa", "MSA 15.7 engine management (diesel vehicles)" ,"EFI"},
	/* 0x13 - Volvo Scan Tool */
	{0x18, "add", "912-D fuel-driven heater (cold climate option)", "HEA"},
	{0x29, "ecc", "electronic climate control", "ECC"},
	{0x2d, "vgla", "Volvo Guard Lock and Alarm", "GLA"},
	{0x2e, "psl", "left power seat", "PSL"},
	{0x2f, "psr", "right power seat", "PSR"},
	{0x41, "immo", "immobilizer", "IMM"},
	{0x51, "combi", "combined instrument panel", "CI"},
	{0x58, "srs", "airbags and belt tensioner", "SRS"},
	{0x6e, "aw50", "AW50-42 transmission", "AT"},
	{0x7a, "m44", "Motronic M4.4 engine management", "EFI"},
	{0, NULL, NULL, NULL}
};

static int cmd_850_help(int argc, char **argv);
static int cmd_850_connect(int argc, char **argv);
static int cmd_850_disconnect(int argc, UNUSED(char **argv));
static int cmd_850_ping(int argc, UNUSED(char **argv));
static int cmd_850_sendreq(int argc, char **argv);

const struct cmd_tbl_entry v850_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_850_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
		cmd_850_help, 0, NULL},

	{ "connect", "connect <ecuname>", "Connect to ECU. Use '850 connect ?' to show ECU names.",
		cmd_850_connect, 0, NULL},
	{ "disconnect", "disconnect", "Disconnect from ECU",
		cmd_850_disconnect, 0, NULL},
	{ "sendreq", "sendreq <byte0 [byte1 ...]>", "Send raw data to the ECU and print response",
		cmd_850_sendreq, 0, NULL},
	{ "ping", "ping", "Verify communication with the ECU", cmd_850_ping,
		0, NULL},

	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Exit program",
		cmd_exit, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

static int
cmd_850_help(int argc, char **argv)
{
	return help_common(argc, argv, v850_cmd_table);
}

/*
 * Capitalize the first letter of the supplied string.
 * Returns a static buffer that will be reused on the next call.
 */
static char *
capitalize(const char *in)
{
	static char buf[80];

	strncpy(buf, in, sizeof(buf));
	buf[sizeof(buf)-1] = '\0';

	if(isalpha(buf[0]) && islower(buf[0]))
		buf[0] = toupper(buf[0]);
	return buf;
}

/*
 * Look up an ECU by name.
 */
static struct ecu_info *
ecu_info_by_name(const char *name)
{
	struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (strcasecmp(name, ecu->name) == 0)
			return ecu;
	}

	return NULL;
}

/*
 * Get an ECU's address by name.
 */
static int
ecu_addr_by_name(const char *name)
{
	struct ecu_info *ecu;
	unsigned long int i;
	char *p;

	if (isdigit(name[0])) {
		i = strtoul(name, &p, 0);
		if (*p != '\0')
			return -1;
		if(i > 0x7f)
			return -1;
		return i;
	}

	ecu = ecu_info_by_name(name);
	if (ecu == NULL) {
		return -1;
	} else {
		return ecu->addr;
	}
}

/*
 * Get an ECU's description by address.
 */
static char *
ecu_desc_by_addr(uint8_t addr)
{
	struct ecu_info *ecu;
	static char buf[7];

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (addr == ecu->addr)
			return ecu->desc;
	}

	sprintf(buf, "ECU %02X", addr);
	return buf;
}

/*
 * Get the description of the currently connected ECU.
 */
static char *
current_ecu_desc(void)
{
	int addr;

	if (global_state < STATE_CONNECTED)
		return "???";

	addr = global_l2_conn->diag_l2_destaddr;

	if((addr < 0) || (addr > 0x7f))
		return "???";

	return ecu_desc_by_addr(addr);
}

/*
 * Print a list of known ECUs. Not all ECUs in this list are necessarily
 * present in the vehicle.
 */
static void
print_ecu_list(void)
{
	struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		printf(" %s\t%s\n", ecu->name, capitalize(ecu->desc));
	}
}

/*
 * Indicates whether we're currently connected.
 */
static enum {
	NOT_CONNECTED, CONNECTED_KWP6227, CONNECTED_OTHER
} connection_status(void)
{
	if (global_state < STATE_CONNECTED) {
		return NOT_CONNECTED;
	} else if (global_l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_KWP6227) {
		return CONNECTED_KWP6227;
	} else {
		return CONNECTED_OTHER;
	}
}

/*
 * Check whether the number of arguments to a command is between the specified
 * minimum and maximum. If not, print a message and return false.
 */
static bool
valid_arg_count(int min, int argc, int max)
{
	if(argc < min) {
                printf("Too few arguments\n");
                return false;
	}

	if(argc > max) {
		printf("Too many arguments\n");
		return false;
	}

	return true;
}

/*
 * Check whether the connection status matches the required connection status
 * for this command. If not, print a message and return false.
 */
static bool
valid_connection_status(unsigned int want)
{
	if (connection_status() == want)
		return true;

	switch(connection_status()) {
	case NOT_CONNECTED:
		printf("Not connected.\n");
		return false;
	case CONNECTED_OTHER:
		if(want == NOT_CONNECTED) {
			printf("Already connected with non-Volvo protocol. Please use 'diag disconnect'.\n");
		} else {
			printf("Connected with non-Volvo protocol.\n");
		}
		return false;
	case CONNECTED_KWP6227:
		printf("Already connected to %s. Please disconnect first.\n", current_ecu_desc());
		return false;
	default:
		printf("Unexpected connection state!\n");
		return false;
	}
}

/*
 * Connect to an ECU by name or address.
 */
static int
cmd_850_connect(int argc, char **argv)
{
	int addr;
	int rv;
	struct diag_l0_device *dl0d;

	if (!valid_arg_count(2, argc, 2))
                return CMD_USAGE;

	if (strcmp(argv[1], "?") == 0) {
		printf("Known ECUs are:\n");
		print_ecu_list();
		printf("Can also specify target by numeric address.\n");
		return CMD_USAGE;
	}

	if(!valid_connection_status(NOT_CONNECTED))
		return CMD_OK;

	addr = ecu_addr_by_name(argv[1]);
	if (addr < 0) {
		printf("Unknown ECU '%s'\n", argv[1]);
		return CMD_OK;
	}

	global_cfg.speed = 10400;
	global_cfg.src = 0x13;
	global_cfg.tgt = addr;
	global_cfg.L1proto = DIAG_L1_ISO9141;
	global_cfg.L2proto = DIAG_L2_PROT_KWP6227;
	global_cfg.initmode = DIAG_L2_TYPE_SLOWINIT;

	dl0d = global_dl0d;

	if (dl0d == NULL) {
		printf("No global L0. Please select + configure L0 first\n");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		diag_end();
		return diag_iseterr(rv);
	}

	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "cmd_850_connect: diag_l2_open failed\n");
		return diag_iseterr(rv);
	}

	global_l2_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
		global_cfg.initmode & DIAG_L2_TYPE_INITMASK, global_cfg.speed,
		global_cfg.tgt, global_cfg.src);
	if (global_l2_conn == NULL) {
		rv = diag_geterr();
		diag_l2_close(dl0d);
		return diag_iseterr(rv);
	}

	global_state = STATE_CONNECTED;
	printf("Connected to %s.\n", ecu_desc_by_addr(addr));

	return CMD_OK;
}

/*
 * Close the current connection.
 */
static int
cmd_850_disconnect(int argc, UNUSED(char **argv))
{
	char *desc;

	if (!valid_arg_count(1, argc, 1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	desc = current_ecu_desc();

	diag_l2_StopCommunications(global_l2_conn);
	diag_l2_close(global_dl0d);

	global_l2_conn = NULL;
	global_state = STATE_IDLE;

	printf("Disconnected from %s.\n", desc);
	return CMD_OK;
}

/*
 * Send a raw command and print the response.
 */
static int
cmd_850_sendreq(int argc, char **argv)
{
	uint8_t data[MAXRBUF] = {0};
	unsigned int len;
	unsigned int i;
	int rv;

	if (!valid_arg_count(2, argc, sizeof(data)+1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	len = argc - 1;
	for (i = 0; i < len; i++) {
		data[i] = (uint8_t) htoi(argv[i+1]);
	}

	rv = l2_do_send( global_l2_conn, data, len,
		(void *)&_RQST_HANDLE_DECODE);

	if(rv == DIAG_ERR_TIMEOUT) {
		printf("No data received\n");
	} else if(rv != 0) {
		printf("sendreq: failed error %d\n", rv);
	}

	return CMD_OK;
}

/*
 * Verify communication with the ECU.
 */
static int
cmd_850_ping(int argc, UNUSED(char **argv))
{
	if (!valid_arg_count(1, argc, 1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	if (diag_l7_volvo_ping(global_l2_conn) == 0) {
		printf("Pong!\n");
	} else {
		printf("Ping failed.\n");
	}

	return CMD_OK;
}

#if 0
static int
cmd_850_peek(int argc, char **argv)
{
	int count;
	int i;
	bool continuous;
	struct {
		uint16_t start;
		uint16_t end;
	} *peeks;

	if (!valid_arg_count(2, argc, 999))
                return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	continuous = false;
	count = argc - 1;

	if (strcasecmp(argv[argc-1], "live")==0) {
		continuous = true;
		count--;
	}

	peeks = calloc(sizeof(peeks[0]), count);
	if (peeks == NULL)
		return diag_iseterr(DIAG_ERR_NOMEM);
	for(i=0; i<count; i++) {
		peeks[i].start = 0;
		peeks[i].end = 1;
	}
	free(peeks);

	return CMD_OK;
}

static int
cmd_850_dumpram(int argc, char **argv)
{
	if (!valid_arg_count(2, argc, 2))
                return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	return CMD_OK;
}
#endif
