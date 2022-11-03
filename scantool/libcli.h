#ifndef LIBCLI_H
#define LIBCLI_H

/*
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 *
 * Copyright (C) 2015 Tomasz Kaźmierczak (tomek-k@wp.eu)
 *                    - added command completion
 * (c) fenugrec 2014-2022
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ******************************************
 *
 * Generic CLI processor library
 * Split out from freediag code
 */


#include <stdio.h>              /* For FILE */


/** Return values from the commands */
enum cli_retval {
	CMD_OK=0,       /* OK */
	CMD_USAGE,      /* Bad usage, print usage info */
	CMD_FAILED,     /* Cmd failed */
	CMD_EXIT,       /* Exit called */
	CMD_UP          /* Go up one level in command tree */
};


/******* Command table definitions ********/

/** Command descriptor
 *
 * command table consists of an array of these
 * */
struct cmd_tbl_entry {
	const char *command;            /* Command name */
	const char *usage;              /* Usage info */
	const char *help;               /* Help Text */
	enum cli_retval (*routine)(int argc, char **argv);          /* Command Routine */
	const int flags;                /* Flag, see below */

	const struct cmd_tbl_entry *sub_cmd_tbl;                /* If specified, command is a sub-table */
};

// optional : add built-in commands with default implementations
#define CLI_TBL_BUILTINS \
	{ "up", "up", "Return to previous menu level", cmd_up, 0, NULL}, \
	{ "exit", "exit", "Exits program", cmd_exit, 0, NULL}, \
	{ "quit", "quit", "Exits program", cmd_exit, CLI_CMD_HIDDEN, NULL}

#define CLI_TBL_END { NULL, NULL, NULL, NULL, 0, NULL}  //must be last element of every command table

/** list of customizable callbacks.
 * Unused entries can be set to NULL
 */
struct cli_callbacks {
	void (*cli_logcmd)(int argc, char **argv);      // optional, called for each processed command
	void (*cli_atexit)(void);       //optional, will be called after a CMD_EXIT
};

#define CLI_CMD_HIDDEN  (1 << 0)        /* Hidden command */
#define CLI_CMD_FILEARG (1 << 1) /* Command accepts a filename as an argument*/
#define CLI_CMD_CUSTOM (1 << 2) /* Command handles other subcommands not in the subtable, max 1 per table */



/************************** public funcs ***************/

/** Change default callbacks
 */
void cli_set_callbacks(const struct cli_callbacks *);

/** Start an interactive CLI session
 *
 * @param name prompt string
 * @param initscript optional; file to source commands from on init
 * @param cmdtable
 */
void enter_cli(const char *name, const char *initscript, const struct cmd_tbl_entry *cmdtable);



/** Prompt for some input.
 *
 * @param prompt : optional
 * @return a new 0-terminated string with trailing CR/LF stripped, NULL if no more input
 *
 * Caller must free returned buffer. Used if we don't
 * have readline, and when reading init or command files.
 * No line editing or history.
 */
char *basic_get_input(const char *prompt, FILE *instream);


// XXX todo : cleanup and doc these
enum cli_retval cmd_up(int argc, char **argv);
enum cli_retval cmd_exit(int argc, char **argv);
enum cli_retval cmd_source(int argc, char **argv);

enum cli_retval help_common(int argc, char **argv, const struct cmd_tbl_entry *cmd_table);

#endif
