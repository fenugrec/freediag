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
 * CLI routines - debug subcommand
 */

#include "diag.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "scantool.h"
#include "scantool_cli.h"

CVSID("$Id$");

int diag_cmd_debug;

static int cmd_debug_dumpdata(int argc, char **argv);
static int cmd_debug_pids(int argc, char **argv);
static int cmd_debug_help(int argc, char **argv);
static int cmd_debug_show(int argc, char **argv);

static int cmd_debug_cli(int argc, char **argv);
static int cmd_debug_l0(int argc, char **argv);
static int cmd_debug_l1(int argc, char **argv);
static int cmd_debug_l2(int argc, char **argv);
static int cmd_debug_l3(int argc, char **argv);
static int cmd_debug_all(int argc, char **argv);

const struct cmd_tbl_entry debug_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_debug_help, 0, NULL},

	{ "dumpdata", "dumpdata", "Show Mode1 Pid1/2 responses",
		cmd_debug_dumpdata, 0, NULL},

	{ "pids", "pids", "Shows PIDs supported by ECU",
		cmd_debug_pids, 0, NULL},

	{ "show", "show", "Shows current debug levels",
		cmd_debug_show, 0, NULL},

	{ "l0", "l0 [val]", "Show/set Layer0 debug level",
		cmd_debug_l0, 0, NULL},
	{ "l1", "l1 [val]", "Show/set Layer1 debug level",
		cmd_debug_l1, 0, NULL},
	{ "l2", "l2 [val]", "Show/set Layer2 debug level",
		cmd_debug_l2, 0, NULL},
	{ "l3", "l3 [val]", "Show/set Layer3 debug level",
		cmd_debug_l3, 0, NULL},
	{ "cli", "cli [val]", "Show/set CLI debug level",
		cmd_debug_cli, 0, NULL},
	{ "all", "all", "Show/set All layer debug level",
		cmd_debug_all, 0, NULL},
	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Return to previous menu level",
		cmd_up, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

static int
cmd_debug_help(int argc, char **argv)
{
	return help_common(argc, argv, debug_cmd_table);
}

#ifdef WIN32
static void
print_resp_info(int mode, response_t *data)
#else
static void
print_resp_info(int mode __attribute__((unused)), response_t *data)
#endif
{

	int i;
	for (i=0; i<256; i++)
	{
		if (data->type != TYPE_UNTESTED)
		{
			if (data->type == TYPE_GOOD)
			{
				int j;
				printf("0x%02x: ", i );
				for (j=0; j<data->len; j++)
					printf("%02x ", data->data[j]);
				printf("\n");
			}
			else
				printf("0x%02x: Failed 0x%x\n",
					i, data->data[1]);
		}
		data++;
	}
}

#ifdef WIN32
static int
cmd_debug_dumpdata(int argc,
char **argv)
#else
static int
cmd_debug_dumpdata(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	ecu_data_t *ep;
	int i;

	printf("Current Data\n");
	for (i=0, ep=ecu_info; i<MAX_ECU; i++,ep++)
	{
		if (ep->valid)
		{
			printf("ECU %d:\n", ep->ecu_addr & 0xff);
			print_resp_info(1, ep->mode1_data);
		}
	}

	printf("Freezeframe Data\n");
	for (i=0,ep=ecu_info; i<MAX_ECU; i++,ep++)
	{
		if (ep->valid)
		{
			printf("ECU %d:\n", ep->ecu_addr & 0xff);
			print_resp_info(2, ep->mode2_data);
		}
	}

	return CMD_OK;
}

static int
cmd_debug_common( const char *txt, int *val, int argc, char **argv)
{
	int r;

	if (argc == 1)
	{
		printf("%s debug is 0x%x\n", txt, *val);
	}
	else
	{
		r = htoi(argv[1]);
		*val = r;
	}
	return CMD_OK;
}

static int
cmd_debug_l0(int argc, char **argv)
{
	return cmd_debug_common("L0", &diag_l0_debug, argc, argv);
}
static int
cmd_debug_l1(int argc, char **argv)
{
	return cmd_debug_common("L1", &diag_l1_debug, argc, argv);
}
static int
cmd_debug_l2(int argc, char **argv)
{
	return cmd_debug_common("L2", &diag_l2_debug, argc, argv);
}
static int
cmd_debug_l3(int argc, char **argv)
{
	return cmd_debug_common("L3", &diag_l3_debug, argc, argv);
}
static int
cmd_debug_cli(int argc, char **argv)
{
	return cmd_debug_common("CLI", &diag_cmd_debug, argc, argv);
	//for now, value > 0x80 will enable all debugging info.
}

static int
cmd_debug_all(int argc, char **argv)
{
	int val;

	if (argc == 1)
	{
		return cmd_debug_show(1, NULL);
	}
	else
	{
		val = htoi(argv[1]);
		diag_l0_debug = val;
		diag_l1_debug = val;
		diag_l2_debug = val;
		diag_l3_debug = val;
		diag_cmd_debug = val;
	}
	return CMD_OK;
}

#ifdef WIN32
static int
cmd_debug_show(int argc,
char **argv)
#else
static int
cmd_debug_show(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
/*	int layer, val; */

	printf("Debug values: L0 0x%x, L1 0x%x, L2 0x%x L3 0x%x CLI 0x%x\n",
		diag_l0_debug, diag_l1_debug, diag_l2_debug, diag_l3_debug,
		diag_cmd_debug);
	return CMD_OK;
}

static void
print_pidinfo(int mode, uint8_t *pid_data)
{
	int i,j,p;

	j = 0; p = 0;
	printf(" Mode %d:\n	", mode);
	for (i=0; i<=0x60; i++)
	{
		if (pid_data[i]) {
			printf("0x%x ", i);
			j++; p++;
		}
		if (j == 10)
		{
			j = 0;
			printf("\n	");
		}
	}
	if ((p == 0) || (j != 0))
		printf("\n");
}

#ifdef WIN32
static int cmd_debug_pids(int argc,
char **argv)
#else
static int cmd_debug_pids(int argc __attribute__((unused)),
char **argv __attribute__((unused)))
#endif
{
	ecu_data_t *ep;
	int i;

	if (global_state < STATE_SCANDONE)
	{
		printf("SCAN has not been done, please do a scan\n");
		return CMD_OK;
	}

	for (i=0,ep=ecu_info; i<MAX_ECU; i++,ep++)
	{
		if (ep->valid)
		{
			printf("ECU %d address 0x%x: Supported PIDs:\n",
				i, ep->ecu_addr & 0xff);
			print_pidinfo(1, ep->pids);
			print_pidinfo(2, ep->mode2_info);
			print_pidinfo(5, ep->mode5_info);
			print_pidinfo(6, ep->mode6_info);
			print_pidinfo(8, ep->mode8_info);
			print_pidinfo(9, ep->mode9_info);
		}
	}
	printf("\n");

	return CMD_OK;
}
