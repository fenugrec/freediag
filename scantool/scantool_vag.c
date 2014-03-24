/*
 * !!! INCOMPLETE !!!!
 *
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
 * CLI routines - vag subcommand
 */

#include "diag.h"

#include "diag_vag.h"
#include "scantool.h"
#include "scantool_cli.h"

CVSID("$Id$");

static int cmd_vag_help(int argc, char **argv);
const struct cmd_tbl_entry vag_cmd_table[] =
{
	{ "help", "help [command]", "Gives help for a command",
		cmd_vag_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
		cmd_vag_help, 0, NULL},


	{ "up", "up", "Return to previous menu level",
		cmd_up, 0, NULL},
	{ "quit","quit", "Return to previous menu level",
		cmd_up, FLAG_HIDDEN, NULL},
	{ "exit", "exit", "Exit program",
		cmd_exit, 0, NULL},

	{ NULL, NULL, NULL, NULL, 0, NULL}
};


/*
 * Table of english descriptions of the VW ECU addresses
 */
struct vw_id_info
{
	const int id;
	const char *command;
} ;

const struct vw_id_info vw_ids[] =
{
	{DIAG_VAG_ECU_ENGINE, "Engine" },
	{DIAG_VAG_ECU_GEARBOX, "Gearbox" },
	{DIAG_VAG_ECU_ABS, "ABS" },
	{DIAG_VAG_ECU_AIRBAGS, "Airbag" },
	{DIAG_VAG_ECU_LOCKS, "Locking" },
	{0, NULL},
};

static int
cmd_vag_help(int argc, char **argv)
{
	return help_common(argc, argv, vag_cmd_table);
}
