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


int diag_cli_debug;

//declare an array of structs to associate debug flag masks with short description.
//See diag.h
const struct debugflags_descr debugflags[]={
	{OPEN, "Open events","OPEN"},
	{CLOSE, "Close events","CLOSE"},
	{READ, "Read events","READ"},
	{WRITE, "Write events","WRITE"},
	{IOCTL, "Ioctl stuff (setspeed etc)","IOCTL"},
	{PROTO, "Protocol stuff","PROTO"},
	{INIT, "Init stuff","INIT"},
	{DATA, "Dump data if READ or WRITE","DATA"},
	{TIMER, "Timer stuff","TIMER"},
	{NIL, NULL, NULL}
};

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
static int cmd_debug_l0test(int argc, char **argv);

const struct cmd_tbl_entry debug_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_debug_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
		cmd_debug_help, FLAG_HIDDEN, NULL},

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
	{ "all", "all [val]", "Show/set All layer debug level",
		cmd_debug_all, 0, NULL},
	{ "l0test", "l0test [testnum]", "Dumb interface tests. Disconnect from vehicle first !",
		cmd_debug_l0test, 0, NULL},
	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Exit program",
		cmd_exit, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

static int
cmd_debug_help(int argc, char **argv)
{
	if (argc<2) {
		printf("Debugging flags are set per level according to the values set in diag.h\n");
		printf("Setting [val] to -1 will enable all debug messages for that level.\n"
				"Available flags:\n");
		int i;
		for (i=0; debugflags[i].mask != NIL; i++) {
			printf("\t0x%4X: %s\n", debugflags[i].mask, debugflags[i].descr);
		}
	}
	return help_common(argc, argv, debug_cmd_table);
}


static void
print_resp_info(UNUSED(int mode), response_t *data)
{

	int i;
	for (i=0; i<256; i++)
	{
		if (data->type != TYPE_UNTESTED)
		{
			if (data->type == TYPE_GOOD)
			{
				printf("0x%02X: ", i );
				diag_data_dump(stdout, data->data, data->len);
				printf("\n");
			}
			else
				printf("0x%02X: Failed 0x%X\n",
					i, data->data[1]);
		}
		data++;
	}
}


static int
cmd_debug_dumpdata(UNUSED(int argc), UNUSED(char **argv))
{
	ecu_data_t *ep;
	int i;

	printf("Current Data\n");
	for (i=0, ep=ecu_info; i<MAX_ECU; i++,ep++)
	{
		if (ep->valid)
		{
			printf("ECU 0x%02X:\n", ep->ecu_addr & 0xff);
			print_resp_info(1, ep->mode1_data);
		}
	}

	printf("Freezeframe Data\n");
	for (i=0,ep=ecu_info; i<MAX_ECU; i++,ep++)
	{
		if (ep->valid)
		{
			printf("ECU 0x%02X:\n", ep->ecu_addr & 0xff);
			print_resp_info(2, ep->mode2_data);
		}
	}

	return CMD_OK;
}

static int
cmd_debug_common( const char *txt, int *val, int argc, char **argv)
{
	int r;
	int i;

	if ((argc ==2) && (argv[1][0]!='?')) {
		//decode number unless it was ?
		r = htoi(argv[1]);
		*val = r;
	}

	printf("%s debug is 0x%X: ", txt, *val);
	for (i=0; debugflags[i].mask != NIL; i++) {
		//check each flag and show what was enabled.
		if (*val & debugflags[i].mask)
			printf("%s ", debugflags[i].shortdescr);
	}
	printf("\n");

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
	return cmd_debug_common("CLI", &diag_cli_debug, argc, argv);
	//for now, value > 0x80 will enable all debugging info.
}

static int
cmd_debug_all(int argc, char **argv)
{
	int val;

	if (argc > 0) {
		val = htoi(argv[1]);
		diag_l0_debug = val;
		diag_l1_debug = val;
		diag_l2_debug = val;
		diag_l3_debug = val;
		diag_cli_debug = val;
	}
	return cmd_debug_show(1, NULL);

	return CMD_OK;
}


static int
cmd_debug_show(UNUSED(int argc), UNUSED(char **argv))
{
/*	int layer, val; */

	printf("Debug values: L0 0x%X, L1 0x%X, L2 0x%X L3 0x%X CLI 0x%X\n",
		diag_l0_debug, diag_l1_debug, diag_l2_debug, diag_l3_debug,
		diag_cli_debug);
	return CMD_OK;
}

/*print_pidinfo() : print supported PIDs (0 to 0x60) */
static void
print_pidinfo(int mode, uint8_t *pid_data)
{
	int i,j;	/* j : # pid per line */

	printf(" Mode %d:", mode);
	for (i=0, j=0; i<=0x60; i++) {
		if (j == 8) j=0;

		if (pid_data[i]) {
			if (j == 0) printf("\n \t");	//once per line
			printf("0x%02X ", i);
			j++;
		}
	}
	printf("\n");
}


static int cmd_debug_pids(UNUSED(int argc), UNUSED(char **argv))
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
			printf("ECU %d address 0x%02X: Supported PIDs:\n",
				i, ep->ecu_addr & 0xff);
			print_pidinfo(1, ep->mode1_info);
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

//cmd_debug_l0test : run a variety of low-level
//tests, for dumb interfaces. Do not use while connected
//to a vehicle: this sends garbage data on the K-line which
//could interfere with ECUs, although very unlikely.

static int cmd_debug_l0test(int argc, char **argv) {
#define MAX_L0TEST 14
	struct diag_l0_device *dl0d = global_dl0d;
	unsigned int testnum=0;

	if ((argc <= 1) || (strcmp(argv[1], "?") == 0) || (sscanf(argv[1],"%u", &testnum) != 1)) {
		printf("usage: %s [testnum], where testnum is a number between 1 and %d.\n", argv[0], MAX_L0TEST);
		printf("you must have done \"set interface dumbt [port]\" and \"set dumbopts\" before proceding.\n");

		printf("Available tests:\n"
				"\t1 : slow pulse TXD (K) with diag_tty_break.\n"
				"\t2 : fast pulse TXD (K) : send 0x55 @ 10400bps, 5ms interbyte (P4)\n"
				"\t10: fast pulse TXD (K) : send 0x55 @ 15000bps, 5ms interbyte (P4)\n"
				"\t3 : slow pulse RTS.\n"
				"\t4 : slow pulse DTR.\n"
				"\t5 : fast pulse TXD (K) with diag_tty_break.\n"
				"\t6 : fast pulse TXD (K) with diag_tty_fastbreak.\n"
				"\t13: simulate iso14230 fastinit with diag_tty_fastbreak.\n"
				"\t7 : simple half duplex removal speed test (10400bps)\n"
				"\t14: simple half duplex removal speed test (360bps)\n"
				"\t8 : block half duplex removal speed test.\n"
				"\t9 : read timeout accuracy check\n"
				"\t11: half duplex incomplete read timeout test.\n"
				"\t12: diag_tty_write() duration.\n");
		return CMD_OK;
	}
	if ((testnum < 1) || (testnum > MAX_L0TEST)) {
		printf("Invalid test.\n");
		return CMD_USAGE;
	}

	if (!dl0d) {
		printf("No global L0. Please select + conf L0 first\n");
		return CMD_FAILED;
	}

	if (strcmp(dl0d->dl0->shortname, "DUMBT") != 0) {
		printf("Wrong global L0, please set to DUMBT\n");
		return CMD_FAILED;
	}
	if (diag_init())
		return CMD_FAILED;

	printf("Trying test %u...\n", testnum);

	// I think the easiest way to pass on "testnum" on to diag_l0_dumbtest.c is
	// to pretend testnum is an L1protocol. Then we can use diag_l2_open to start the
	// test.

	(void) diag_l2_open(dl0d, (int) testnum);

	//We don't need to _close anything since DUMBT is designed to "fail", i.e.
	//return no new dl0d, etc.
	return CMD_OK;


}

