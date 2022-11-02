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
 */


#include <assert.h>
#include <stdbool.h>

#include <time.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cconf.h"	//XXX hax for HAVE_LIBREADLINE, need to move that to libcli.h

#include "libcli.h"

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif


/************** misc defs */
#define PROMPTBUFSIZE 80		//Length of prompt before the '>' character.
#define CLI_MAXARGS 300
#define INPUT_MAX 1400	//big enough to fit long "diag sendreq..." commands

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))


/****** compiler-specific tweaks ******/
#ifdef __GNUC__
	#define UNUSED(X) 	X __attribute__((unused))	//magic !
#else
	#define UNUSED(X)	X	//how can we suppress "unused parameter" warnings on other compilers?
#endif // __GNUC__

//hacks for MS Visual studio / visual C
#if defined(_MSC_VER)
	#if _MSC_VER < 1910 /* anything older than Visual Studio 2017 */
		#define snprintf _snprintf	//danger : _snprintf doesn't guarantee zero-termination !?
									//as of Visual Studio 2015 the snprintf function is c99 compatible.
									//https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/snprintf-snprintf-snprintf-l-snwprintf-snwprintf-l?view=msvc-160
		#pragma message("Warning: MSVC _sprintf() may be dangerous ! Please ask your compiler vendor to supply a C99-compliant snprintf()...")
	#endif /* _MSC_VER < 1910  Visual Studio 2017 */
#endif /* _MSC_VER Visual Studio */




/************ private data **********/
static const struct cmd_tbl_entry *root_cmd_table;	/* point to current root table */
static struct cli_callbacks callbacks = {0};

#ifdef HAVE_LIBREADLINE
//current global command level for command completion
static const struct cmd_tbl_entry *current_cmd_level;
//command level in the command line, also needed for command completion
static const struct cmd_tbl_entry *completion_cmd_level;
#endif



/************* fwd decls */
static int do_cli(const struct cmd_tbl_entry *cmd_tbl, const char *prompt, FILE *instream, int argc, char **argv);


char *basic_get_input(const char *prompt, FILE *instream) {
	char *input;
	bool do_prompt;

	input = malloc(INPUT_MAX);
	if (!input) {
		return NULL;
	}

	do_prompt = 1;
	while (1) {
		if (do_prompt && prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}

		if (fgets(input, INPUT_MAX, instream)) {
			break;
		}
		if (feof(instream)) {
			free(input);
			return NULL;
		}
		/* Ignore error and try again, but don't prompt. */
		clearerr(instream);
		do_prompt = 0;
	}
	input[strcspn(input, "\r\n")] = '\0'; /* Remove trailing CR/LF */
	return input;
}


#ifdef HAVE_LIBREADLINE

/* Caller must free returned buffer */
static char *
get_input(const char *prompt) {
	char *input;
	/* XXX Does readline change the prompt? */
	char localprompt[128];
	strncpy(localprompt, prompt, sizeof(localprompt));

	input = readline(localprompt);
	if (input && *input) {
		add_history(input);
	}
	return input;
}

char *
command_generator(const char *text, int state) {
	static int list_index, length;
	const struct cmd_tbl_entry *cmd_entry;

	//a new word to complete
	if (state == 0) {
		list_index = 0;
		length = strlen(text);
	}

	//find the command
	while (completion_cmd_level[list_index].command != NULL) {
		cmd_entry = &completion_cmd_level[list_index];
		list_index++;
		if (strncmp(cmd_entry->command, text, length) == 0 && !(cmd_entry->flags & FLAG_HIDDEN)) {
			char *ret_name;
			//we must return a copy of the string; libreadline frees it for us
			ret_name = malloc(strlen(cmd_entry->command) + 1);
			if (!ret_name) {
				return NULL;
			}
			strcpy(ret_name, cmd_entry->command);
			return ret_name;
		}
	}
	return (char *)NULL;
}

char **
scantool_completion(const char *text, int start, UNUSED(int end)) {
	char **matches;

	//start == 0 is when the command line is either empty or contains only whitespaces
	if (start == 0) {
		//we are at the beginning of the command line, so the completion command level is equal to the current command level
		completion_cmd_level = current_cmd_level;
		rl_attempted_completion_over = 0;
	}
	//(start != end) means that we are trying to complete a command;
	//(start > 0 and start == end) means that all commands are completed and we need to check for their sub-commands;
	//we handle here both cases so that the completion_cmd_level is always up-to-date
	else {
		//parse the command line so that we know on what command level we should do the completion
		struct cmd_tbl_entry const *parsed_level = current_cmd_level;

		//we need to omit leading whitespaces
		size_t begin_at = strspn(rl_line_buffer, " ");
		const char *cmd = &rl_line_buffer[begin_at];
		//now get the length of the first command
		size_t cmd_length = strcspn(cmd, " ");

		//check all completed commands
		while (cmd_length > 0 && cmd[cmd_length] == ' ') {
			//find out what it might be...
			bool found = 0;
			for (int i = 0; parsed_level[i].command != NULL; i++) {
				//if found the command on the current level
				if (!(parsed_level[i].flags & FLAG_HIDDEN) &&
				   strlen(parsed_level[i].command) == cmd_length &&
				   strncmp(parsed_level[i].command, cmd, cmd_length) == 0) {
					//does it have sub-commands?
					if (parsed_level[i].sub_cmd_tbl != NULL) {
						//go deeper
						parsed_level = parsed_level[i].sub_cmd_tbl;
						rl_attempted_completion_over = 0;
						found = 1;
					} else if (parsed_level[i].flags & FLAG_FILE_ARG) {
						//the command accepts a filename as an argument, so use the libreadline's filename completion
						rl_attempted_completion_over = 0;
						return NULL;
					} else {
						//if no sub-commands, then no more completion
						rl_attempted_completion_over = 1;
						return NULL;
					}
					//stop searching on this level and go to another command in the command line (if any)
					break;
				}
			}
			//went through all commands and didn't find anything? then it is an unknown command
			if (!found) {
				rl_attempted_completion_over = 1;
				return NULL;
			}

			//move past the just-parsed command
			cmd = &cmd[cmd_length];
			//again, omit whitespaces
			begin_at = strspn(cmd, " ");
			cmd = &cmd[begin_at];
			//length of the next command
			cmd_length = strcspn(cmd, " ");
		}

		//update the completion command level for the command_generator() function
		completion_cmd_level = parsed_level;
	}

	matches = rl_completion_matches(text, command_generator);
	if (matches == NULL) {
		//this will disable the default (filename and username) completion in case no command matches are found
		rl_attempted_completion_over = 1;
	}
	return matches;
}

static void
readline_init(const struct cmd_tbl_entry *curtable) {
	//preset levels for current table
	current_cmd_level = curtable;
	completion_cmd_level = curtable;

	//our custom completion function
	rl_attempted_completion_function = scantool_completion;
}

#else	// so no libreadline

static char *
get_input(const char *prompt) {
	return basic_get_input(prompt, stdin);
}

static void readline_init(UNUSED(const struct cmd_tbl_entry *cmd_table)) {}

#endif	//HAVE_LIBREADLINE


/** get input from user or file
*
* @instream either stdin or file
* @return NULL if no more input
*/
static char *
command_line_input(const char *prompt, FILE *instream) {
	if (instream == stdin) {
		return get_input(prompt);
	}

	/* Reading from init or command file; no prompting or history */
	return basic_get_input(NULL, instream);
}


// just a thin wrapper
static void libcli_logcmd(int argc, char **argv) {
	if (callbacks.cli_logcmd) {
		callbacks.cli_logcmd(argc, argv);
	}
	return;
}


void cli_set_callbacks(const struct cli_callbacks* new_callbacks) {
	if (!new_callbacks) {
		return;
	}
	callbacks = *new_callbacks;
	return;
}


/** execute commands read from filename;
 * 
 * ret CMD_OK if file was readable (command/parsing problems are OK)
 * ret CMD_FAILED if file was unreadable
 * forward CMD_EXIT if applicable */
static int command_file(const char *filename) {
	int rv;
	FILE *fstream;

	if ( (fstream=fopen(filename, "r"))) {
		printf("running commands from file %s...\n", filename);
		rv=do_cli(root_cmd_table, NULL, fstream, 0, NULL);
		fclose(fstream);
		return (rv==CMD_EXIT)? CMD_EXIT:CMD_OK;
	}
	return CMD_FAILED;
}


/* Find matching cmd_tbl_entry for command *cmd in table *cmdt.
 * Returns the command or the custom handler if
 * no match was found and the custom handler exists.
 */
static const struct cmd_tbl_entry *find_cmd(const struct cmd_tbl_entry *cmdt, const char *cmd) {
	const struct cmd_tbl_entry *ctp;
	const struct cmd_tbl_entry *custom_cmd;

	assert(cmdt != NULL);

	ctp = cmdt;
	custom_cmd = NULL;

	while (ctp->command) {
		if (ctp->flags & FLAG_CUSTOM) {
			// found a custom handler; save it
			custom_cmd = ctp;
		}

		if (strcasecmp(ctp->command, cmd) == 0) {
			return ctp;
		}
		ctp++;
	}

	return custom_cmd;
}




static char nullstr[2] = {0,0};	//can't be const char because it goes into argv

/** CLI processor
 *
 * @prompt Optional. Will be prompted only if argc==0
 * @argc  If supplied, then this is one shot cli, ie run the command
 * @instream stdin or file to source commands
 *
 * @return results as CMD_xxx (such as CMD_EXIT)
 *
 * prints *prompt,
 */
static int do_cli(const struct cmd_tbl_entry *cmd_tbl, const char *prompt, FILE *instream, int argc, char **argv) {
	/* Build up argc/argv */
	const struct cmd_tbl_entry *ctp;
	int cmd_argc;
	char *cmd_argv[CLI_MAXARGS];
	char *input = NULL;
	int rv;
	bool done;	//when set, sub-command processing is ended and returns to upper level
	int i;

	if (!prompt) {
		prompt = "";
	}

	char promptbuf[PROMPTBUFSIZE];	/* Was 1024, who needs that long a prompt? (the part before user input up to '>') */

#ifdef HAVE_LIBREADLINE
	//set the current command level for command completion
	current_cmd_level = cmd_tbl;
#endif

	rv = CMD_FAILED;
	done = 0;
	snprintf(promptbuf, PROMPTBUFSIZE, "%s> ", prompt);
	while (!done) {
		char *inptr, *s;

		if (argc == 0) {
			/* Get Input */
			if (input) {
				free(input);
			}
			input = command_line_input(promptbuf, instream);
			if (!input) {
				break;
			}

			/* Parse it */
			inptr = input;
			if (*inptr == '@') {	//printing comment
				printf("%s\n", inptr);
				continue;
			}
			if (*inptr == '#') {		//non-printing comment
				continue;
			}
			cmd_argc = 0;
			while ( (s = strtok(inptr, " \t")) != NULL ) {
				cmd_argv[cmd_argc] = s;
				inptr = NULL;
				if (cmd_argc == (ARRAY_SIZE(cmd_argv)-1)) {
					fprintf(stderr, "Warning : excessive # of arguments\n");
					break;
				}
				cmd_argc++;
			}
			cmd_argv[cmd_argc] = nullstr;
		} else {
			/* Use supplied argc */
			cmd_argc = argc;
			for (i = 0; i < argc; i++) {
				cmd_argv[i] = argv[i];
			}
		}

		if (cmd_argc == 0) {
			continue;
		}
		ctp = find_cmd(cmd_tbl, cmd_argv[0]);

		if (ctp == NULL) {
			printf("Unrecognized command. Try \"help\"\n");
			if ((instream != stdin) || (argc > 0)) {
				//processing a file, or running a subcommand : abort
				break;
			}
			//else : continue getting input
			continue;
		}

		if (ctp->sub_cmd_tbl) {
			/* has sub-commands */
			libcli_logcmd(1, cmd_argv);
			snprintf(promptbuf, PROMPTBUFSIZE,"%s/%s",
				prompt, ctp->command);
			/* Sub menu */
			rv = do_cli(ctp->sub_cmd_tbl,
				promptbuf,
				instream,
				cmd_argc-1,
				&cmd_argv[1]);
#ifdef HAVE_LIBREADLINE
			//went out of the sub-menu, so update the command level for command completion
			current_cmd_level = cmd_tbl;
#endif
			if (rv == CMD_EXIT) { // allow exiting prog. from a
					      // submenu
				done = 1;
			}
			snprintf(promptbuf, PROMPTBUFSIZE, "%s> ", prompt);
		} else {
			// Regular command
			libcli_logcmd(cmd_argc, cmd_argv);
			rv = ctp->routine(cmd_argc, cmd_argv);
			switch (rv) {
				case CMD_USAGE:
					printf("Usage: %s\n%s\n", ctp->usage, ctp->help);
					break;
				case CMD_UP:
					if (cmd_tbl == root_cmd_table) {
						//ignore cmd_up here
						break;
					}
					// fallthrough
				case CMD_EXIT:
					done = 1;
					break;
			}
		}

		if (argc) {
			/* Single command */
			break;
		}
	}	//while !done

	if (input) {
		free(input);
	}
	if (rv == CMD_UP) {
		return CMD_OK;
	}
	return rv;
}


/* start a cli with <name> as a prompt, and optionally run the <initscript> file */
void enter_cli(const char *name, const char *initscript, const struct cmd_tbl_entry *cmdtable) {
	assert(cmdtable);

	root_cmd_table = cmdtable;

	readline_init(cmdtable);

	if (initscript != NULL) {
		int rv=command_file(initscript);
		switch (rv) {
			case CMD_OK:
				/* script was succesful, start a normal CLI afterwards */
				break;
			case CMD_FAILED:
				printf("Problem with file %s\n", initscript);
				// fallthrough, yes
			default:
			case CMD_EXIT:
				goto exit_cleanup;
		}
	}

	printf("\n");
	/* And go start CLI */
	(void)do_cli(root_cmd_table, name, stdin, 0, NULL);

exit_cleanup:
	if (callbacks.cli_atexit) {
		callbacks.cli_atexit();
	}

	root_cmd_table = NULL;
	return;

}


/********* generic commands */

int cmd_source(int argc, char **argv) {
	char *file;
	int rv;

	if (argc < 2) {
			printf("No filename\n");
		return CMD_USAGE;
	}

	file = argv[1];
	rv=command_file(file);
	if (rv == CMD_FAILED) {
			printf("Couldn't read %s\n", file);
	}

	return rv;
}


int help_common(int argc, char **argv, const struct cmd_tbl_entry *cmd_table) {
/*	int i;*/
	const struct cmd_tbl_entry *ctp;

	if (argc > 1) {
		/* Single command help */
		int found = 0;
		ctp = cmd_table;
		while (ctp->command) {
			if (strcasecmp(ctp->command, argv[1]) == 0) {
				printf("%s: %s\n", ctp->command, ctp->help);
				printf("Usage: %s\n", ctp->usage);
				found++;
				break;
			}
			ctp++;
		}
		if (!found) {
			printf("help: %s: no such command\n", argv[1]);
		}
		return CMD_OK;
	}

	/* Print help */
	printf("Available commands are :\n");
	ctp = cmd_table;
	while (ctp->command) {
		if ((ctp->flags & FLAG_HIDDEN) == 0) {
			printf("\t%s\n", ctp->usage);
		}
		if (ctp->flags & FLAG_CUSTOM) {
			/* list custom subcommands too */
			printf("Custom commands for the current level:\n");
			char *cust_special[]= { "?", NULL };
			char **temp_argv = &cust_special[0];
			ctp->routine(1, temp_argv);
		}
		ctp++;
	}
	printf("\nTry \"help <command>\" for further help\n");


	return CMD_OK;
}

int cmd_help(int argc, char **argv) {
	return help_common(argc, argv, root_cmd_table);
}

int
cmd_up(UNUSED(int argc), UNUSED(char **argv)) {
	return CMD_UP;
}


int
cmd_exit(UNUSED(int argc), UNUSED(char **argv)) {
	return CMD_EXIT;
}
