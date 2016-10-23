#ifndef _SCANTOOL_CLI_H_
#define _SCANTOOL_CLI_H_
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
 * CLI routines
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct cmd_tbl_entry
{
	const char *command;		/* Command name */
	const char *usage;		/* Usage info */
	const char *help;		/* Help Text */
	int	(*routine)(int argc, char **argv);	/* Command Routine */
	const int flags;		/* Flag */

	const struct cmd_tbl_entry *sub_cmd_tbl;		/* Next layer */

} cmd_tbl_entry_t ;

/* Return values from the commands */
#define CMD_OK		0	/* OK */
#define CMD_USAGE	1	/* Bad usage, print usage info */
#define CMD_FAILED	2	/* Cmd failed */
#define CMD_EXIT	3	/* Exit called */
#define CMD_UP		4	/* Go up one level in command tree */

#define FLAG_HIDDEN	(1 << 0)	/* Hidden command */
#define FLAG_FILE_ARG (1 << 1) /* Command accepts a filename as an argument*/
#define FLAG_CUSTOM (1 << 2)	/* Command handles other subcommands not in the subtable, max 1 per table */

int help_common(int argc, char **argv, const struct cmd_tbl_entry *cmd_table);
void wait_enter(const char *message);
int pressed_enter(void);

void enter_cli(const char *name, const char *initscript, const struct cmd_tbl_entry *extra_cmdtable);

extern FILE		*global_logfp;		/* Monitor log output file pointer */
void log_timestamp(const char *prefix);

/** Prompt for some input.
 * Returns a new 0-terminated string with trailing CR/LF stripped
 *
 * Caller must free returned buffer. Used if we don't
 * have readline, and when reading init or command files.
 * No line editing or history.
 */
char *
basic_get_input(const char *prompt);


/* Sub menus */

extern const struct cmd_tbl_entry set_cmd_table[];
extern int set_init(void);
extern void set_close(void);

extern const struct cmd_tbl_entry debug_cmd_table[];
extern const struct cmd_tbl_entry test_cmd_table[];
extern const struct cmd_tbl_entry diag_cmd_table[];
extern const struct cmd_tbl_entry vag_cmd_table[];
extern const struct cmd_tbl_entry dyno_cmd_table[];

#if defined(__cplusplus)
}
#endif
#endif /* _SCANTOOL_CLI_H_ */
