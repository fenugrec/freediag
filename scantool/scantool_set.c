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
 * CLI routines - "set" commands
 *
 *
 */
#include <stdbool.h>

#include "diag.h"
#include "diag_cfg.h"	//for cfgi
#include "diag_l0.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "scantool.h"
#include "scantool_cli.h"
#include "utlist.h"

/** Global parameters **/
/* struct global_cfg contains all global parameters */
struct globcfg global_cfg;

struct diag_l0_device *global_dl0d;


/*
 * XXX All commands should probably have optional "init" hooks.
 */
int set_init(void)
{
	/* Reset parameters to defaults. */

	global_cfg.speed = 10400;	/* Comms speed; ECUs will probably send at 10416 bps (96us per bit) */

	global_cfg.src = 0xf1;	/* Our tester ID */
	global_cfg.addrtype = 1;	/* Use functional addressing */
	global_cfg.tgt = 0x33;	/* Dest ECU address */

	global_cfg.L1proto = DIAG_L1_ISO9141;	/* L1 protocol type */

	global_cfg.L2idx = 0;
	global_cfg.L2proto = l2proto_list[0]->diag_l2_protocol; /* cannot guarantee 9141 was compiled... DIAG_L2_PROT_ISO9141; */

	global_cfg.initmode = DIAG_L2_TYPE_FASTINIT ;

	global_cfg.units = 0;		/* English (1), or Metric (0) */

	global_cfg.l0name = l0dev_list[0]->shortname;	/* Default H/w interface to use */

	printf( "%s: Interface set to default: %s\n", progname, global_cfg.l0name);

	global_dl0d=NULL;

	return 0;
}

void set_close(void)
{
	return;
}



/* SET sub menu */
static int cmd_set_custom(int argc, char **argv);
static int cmd_set_help(int argc, char **argv);
static int cmd_set_show(int argc, char **argv);
static int cmd_set_speed(int argc, char **argv);
static int cmd_set_testerid(int argc, char **argv);
static int cmd_set_destaddr(int argc, char **argv);
static int cmd_set_addrtype(int argc, char **argv);
static int cmd_set_l1protocol(int argc, char **argv);
static int cmd_set_l2protocol(int argc, char **argv);
static int cmd_set_initmode(int argc, char **argv);
static int cmd_set_display(int argc, char **argv);
static int cmd_set_interface(int argc, char **argv);

const struct cmd_tbl_entry set_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_set_help, 0, NULL},
	{ "?", "help [command]", "Gives help for a command",
		cmd_set_help, FLAG_HIDDEN, NULL},

	{ "interface", "interface [NAME]", "Interface to use. Use set interface ? to get a list of names",
		cmd_set_interface, 0, NULL},

	{ "display", "display [english/metric]", "English or metric display",
		cmd_set_display, 0, NULL},

	{ "speed", "speed [speed]", "ECU communications speed",
		cmd_set_speed, 0, NULL},
	{ "testerid", "testerid [testerid]", "Source ID/address",
		cmd_set_testerid, 0, NULL},
	{ "destaddr", "destaddr [destaddr]","Destination ID/address",
		cmd_set_destaddr, 0, NULL},

	{ "addrtype", "addrtype [func/phys]", "Address type, physical or functional.",
		cmd_set_addrtype, 0, NULL},

	{ "l1protocol", "l1protocol [protocolname]", "Hardware (L1) protocol to use. Use 'set l1protocol ?' to show valid choices.",
		cmd_set_l1protocol, 0, NULL},

	{ "l2protocol", "l2protocol [protocolname]", "Software (L2) protocol to use. Use 'set l2protocol ?' to show valid choices.",
		cmd_set_l2protocol, 0, NULL},

	{ "initmode", "initmode [modename]", "Bus initialisation mode to use. Use 'set initmode ?' to show valid choices.",
		cmd_set_initmode, 0, NULL},

	{ "show", "show", "Shows all settable values, including L0-specific items",
		cmd_set_show, 0, NULL},

	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Exit program",
		cmd_exit, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},
	{ "", "", "",
		cmd_set_custom, FLAG_CUSTOM | FLAG_HIDDEN, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};

const char * const l1_names[] = //these MUST be in the same order as they are listed in diag_l1.h !!
{
	"ISO9141", "ISO14230",
	"J1850-VPW", "J1850-PWM", "CAN", "", "", "RAW", NULL
};

//These MUST match the DIAG_L2_TYPE_* flags in diag_l2.h  so that
// l2_initmodes[DIAG_L2_TYPE_XX] == "XX" !!
const char * const l2_initmodes[] =
{
	"5BAUD", "FAST", "CARB", NULL
};

// handle dynamic options (L0-specific).
// argv[0] is the config shortname, etc.
// if argv[0] is '?' (special case), this prints available subcommands.
static int cmd_set_custom(int argc, char **argv) {
	struct cfgi *cfgp;
	char *setstr;
	bool helping=0;
	bool show_current=0;
	int newval;

	if (!global_dl0d) {
		// no L0 selected yet
		if (strcmp(argv[0], "?") == 0)
			return CMD_OK;
		printf("No such item !\nAdditional items may be available after setting the interface type.\nUse \"set interface NAME\" to set the interface type.\n");
		return CMD_FAILED;
	}

	if (strcmp(argv[0], "?") == 0) {
		//list available custom commands for the current L0.
		LL_FOREACH(diag_l0_getcfg(global_dl0d), cfgp) {
			printf("\t%s\n", cfgp->shortname);
		}
		return CMD_OK;
	}

	if (argc >= 2) {
		if (strcmp(argv[1], "?") == 0) {
			helping = 1;
		}
	} else {
		// no args: display current settings
		 show_current = 1;
	}

	/* find the config item */
	LL_FOREACH(diag_l0_getcfg(global_dl0d), cfgp) {
		if (strcasecmp(cfgp->shortname, argv[0]) == 0) break;
	}

	if (!cfgp) {
		printf("No such item !\n");
		return CMD_FAILED;
	}

	if (show_current) {
		char *val = diag_cfg_getstr(cfgp);
		printf("%s: %s\n", argv[0], val);
		free(val);
		return CMD_OK;
	}

	if (helping) {
		printf("%s\n", cfgp->descr);
		diag_cfg_refresh(cfgp);
		if (cfgp->numopts > 0) {
			int i;
			printf("Available options:\n");
			for (i=0; i < cfgp->numopts; i++) {
				printf("\t\t%s\n", cfgp->opt[i]);
			}
		}
		return CMD_OK;
	}

/* TODO : move this to diag_cfg.* if it works */
	newval = htoi(argv[1]);
	switch (cfgp->type) {
	case CFGT_STR:
		diag_cfg_setstr(cfgp, argv[1]);
		break;
	case CFGT_U8:
		diag_cfg_setu8(cfgp, (uint8_t) newval);
		break;
	case CFGT_INT:
		diag_cfg_setint(cfgp, newval);
		break;
	case CFGT_BOOL:
		diag_cfg_setbool(cfgp, (bool) newval);
		break;
	default:
		return CMD_FAILED;
	}

	setstr = diag_cfg_getstr(cfgp);
	printf("%s set to: %s\n", cfgp->shortname, setstr);
	free(setstr);
	return CMD_OK;
}

static int
cmd_set_show(UNUSED(int argc), UNUSED(char **argv))
{
	/* Show stuff; calling the cmd_set_*() functions with argc=0 displays the current setting. */
	cmd_set_interface(0,NULL);
	cmd_set_speed(0, NULL);
	cmd_set_display(0,NULL);
	cmd_set_testerid(0,NULL);
	cmd_set_addrtype(0,NULL);
	cmd_set_destaddr(0,NULL);
	cmd_set_l1protocol(0,NULL);
	cmd_set_l2protocol(0,NULL);
	cmd_set_initmode(0,NULL);

	/* Parse L0-specific config items */
	if (global_dl0d) {
		struct cfgi *cfgp;
		printf("L0 options:\n");
		LL_FOREACH(diag_l0_getcfg(global_dl0d), cfgp) {
			char *cs = diag_cfg_getstr(cfgp);
			if (cfgp->shortname == NULL || cs==NULL) continue;

			printf("\t%s=%s\n",cfgp->shortname, cs);
			free(cs);
		}
	}

	return CMD_OK;
}


static int cmd_set_interface(int argc, char **argv)
{
	const struct diag_l0 *iter;

	if (argc <= 1) {
		printf("interface: using %s\n",
			global_cfg.l0name);
		return CMD_OK;
	}
	if (argc > 2) {
		printf("Too many arguments !\n");
		return CMD_USAGE;
	}

	int i, helping = 0, found = 0;
	if (strcmp(argv[1], "?") == 0) {
		helping = 1;
		printf("hardware interface: use \"set interface NAME\" .\n"
		"NAME is the interface type. Valid NAMEs are: \n");
	}
	for (i=0; l0dev_list[i]; i++) {
		iter = l0dev_list[i];
		//loop through l0 interface names, either printing or comparing to argv[1]
		if (helping)
			printf("%s ", iter->shortname);
		else
			if (strcasecmp(argv[1], iter->shortname) == 0) {
				global_cfg.l0name = iter->shortname;
				found = 1;
				break;	//no use in continuing
			}
	}

	if (helping) {
		printf("\n");
		return CMD_OK;
	}

	if (!found) {
		printf("interface: invalid interface %s\n", argv[1]);
		printf("interface: use \"set interface ?\" to see list of names\n");
		return CMD_FAILED;
	}

	printf("interface is now %s\n", global_cfg.l0name);

	/* close + free current global dl0d. */
	if (global_dl0d) {
		/* XXX warn before breaking a (possibly) active L0-L2 chain */
		diag_l0_close(global_dl0d);
		diag_l0_del(global_dl0d);
	}

	global_dl0d = diag_l0_new(global_cfg.l0name);
	if (!global_dl0d) printf("Error loading interface %s.\n", global_cfg.l0name);

	return CMD_OK;
}

static int
cmd_set_display(int argc, char **argv)
{
	if (argc > 1)
	{
		if (strcasecmp(argv[1], "english") == 0)
			global_cfg.units = 1;
		else if (strcasecmp(argv[1], "metric") == 0)
			global_cfg.units = 0;
		else
			return CMD_USAGE;
	}
	else
		printf("display: %s units\n", global_cfg.units?"english":"metric");

	return CMD_OK;
}

static int
cmd_set_speed(int argc, char **argv)
{
	if (argc > 1) {
		global_cfg.speed = htoi(argv[1]);
	} else {
		printf("speed: Connect speed: %d\n", global_cfg.speed);
	}

	return CMD_OK;
}

static int
cmd_set_testerid(int argc, char **argv)
{
	if (argc > 1)
	{
		int tmp;
		tmp = htoi(argv[1]);
		if ( (tmp < 0) || (tmp > 0xff))
			printf("testerid: must be between 0 and 0xff\n");
		else
			global_cfg.src = (uint8_t) tmp;
	}
	else
		printf("testerid: Source ID to use: 0x%X\n", global_cfg.src);

	return CMD_OK;
}

static int
cmd_set_destaddr(int argc, char **argv)
{
	if (argc > 1) {
		int tmp;
		tmp = htoi(argv[1]);
		if ( (tmp < 0) || (tmp > 0xff))
			printf("destaddr: must be between 0 and 0xff\n");
		else
			global_cfg.tgt = (uint8_t) tmp;
	} else {
		printf("destaddr: Destination address to connect to: 0x%X\n",
			global_cfg.tgt);
	}

	return CMD_OK;
}
static int
cmd_set_addrtype(int argc, char **argv)
{
	if (argc > 1)
	{
		if (strncmp(argv[1], "func", 4) == 0)
			global_cfg.addrtype = 1;
		else if (strncmp(argv[1], "phys", 4) == 0)
			global_cfg.addrtype = 0;
		else
			return CMD_USAGE;
	}
	else
	{
		printf("addrtype: %s addressing\n",
			global_cfg.addrtype ? "functional" : "physical");
	}

	return CMD_OK;
}

static int cmd_set_l2protocol(int argc, char **argv)
{
	if (argc > 1) {
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0) {
			prflag = 1;
			printf("L2 protocol: valid names are ");
		}
		for (i=0; l2proto_list[i] != NULL; i++) {
			const struct diag_l2_proto *d2p=l2proto_list[i];
			if (prflag) {
					printf("%s ", d2p->shortname);
					continue;
			}
			if (strcasecmp(argv[1], d2p->shortname) == 0) {
				found = 1;
				global_cfg.L2idx = i;
				global_cfg.L2proto = d2p->diag_l2_protocol;
				break;
			}
		}
		if (prflag) {
			printf("\n");
			return CMD_OK;
		}
		if (! found) {
			printf("l2protocol: invalid protocol %s\n", argv[1]);
			printf("l2protocol: use \"set l2protocol ?\" to see list of protocols\n");
		}
	} else {
		printf("l2protocol: Layer 2 protocol to use %s\n",
			l2proto_list[global_cfg.L2idx]->shortname);
	}
	return CMD_OK;
}

static int cmd_set_l1protocol(int argc, char **argv)
{
	if (argc > 1)
	{
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0)
		{
			prflag = 1;
			printf("L1 protocol: valid names are ");
		}
		for (i=0; l1_names[i] != NULL; i++)
		{
			if (prflag && *l1_names[i])
				printf("%s ", l1_names[i]);
			else
				if (strcasecmp(argv[1], l1_names[i]) == 0)
				{
					global_cfg.L1proto = 1 << i;
					found = 1;
				}
		}
		if (prflag)
			printf("\n");
		else if (! found)
		{
			printf("L1protocol: invalid protocol %s\n", argv[1]);
			printf("l1protocol: use \"set l1protocol ?\" to see list of protocols\n");
		}
	}
	else
	{
		int offset;

		for (offset=0; offset < 8; offset++)
		{
			if (global_cfg.L1proto == (1 << offset))
				break;
		}
		printf("l1protocol: Layer 1 (H/W) protocol to use %s\n",
			l1_names[offset]);

	}
	return CMD_OK;
}

static int cmd_set_initmode(int argc, char **argv)
{
	if (argc > 1)
	{
		int i, prflag = 0, found = 0;
		if (strcmp(argv[1], "?") == 0)
			prflag = 1;
		for (i=0; l2_initmodes[i] != NULL; i++)
		{
			if (prflag)
				printf("%s ", l2_initmodes[i]);
			else
			{
				if (strcasecmp(argv[1], l2_initmodes[i]) == 0)
				{
					found = 1;
					global_cfg.initmode = i;
				}
			}
		}
		if (prflag)
			printf("\n");
		else if (! found)
		{
			printf("initmode: invalid mode %s\n", argv[1]);
			printf("initmode: use \"set initmode ?\" to see list of initmodes\n");
		}
	}
	else
	{
		printf("initmode: Initmode to use with above protocol is %s\n",
			l2_initmodes[global_cfg.initmode]);
	}
	return CMD_OK;
}

static int
cmd_set_help(int argc, char **argv)
{
	return help_common(argc, argv, set_cmd_table);
}
