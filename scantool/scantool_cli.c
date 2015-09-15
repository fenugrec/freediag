/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 *
 * Copyright (C) 2015 Tomasz Kaźmierczak (tomek-k@wp.eu)
 *                    - added command completion
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

#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "scantool.h"
#include "scantool_cli.h"

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif


#define PROMPTBUFSIZE 80		//was 1024 !!
const char progname[]=SCANTOOL_PROGNAME;
const char projname[]=PROJECT_NAME;

FILE		*global_logfp;		/* Monitor log output file pointer */
#define LOG_FORMAT	"FREEDIAG log format 0.2"

FILE		*instream;

/* ROOT main menu */

/* Main menu commands */

static int cmd_help(int argc, char **argv);
//static int cmd_exit(int argc, char **argv);
static int cmd_monitor(int argc, char **argv);
static int cmd_watch(int argc, char **argv);
static int cmd_cleardtc(int argc, char **argv);
static int cmd_ecus(int argc, char **argv);

static int cmd_log(int argc, char **argv);
static int cmd_stoplog(int argc, char **argv);

UNUSED(static int cmd_play(int argc, char **argv));

static int cmd_scan(int argc, char **argv);

static int cmd_date(int argc, char **argv);
static int cmd_rem(int argc, char **argv);
static int cmd_source(int argc, char **argv);

static const struct cmd_tbl_entry root_cmd_table[]=
{
	{ "scan", "scan", "Start SCAN process", cmd_scan, 0, NULL},
	{ "monitor", "monitor [english/metric]", "Continuously monitor rpm etc",
		cmd_monitor, 0, NULL},

	{ "log", "log <filename>", "Log monitor data to <filename>",
		cmd_log, FLAG_FILE_ARG, NULL},
	{ "stoplog", "stoplog", "Stop logging", cmd_stoplog, 0, NULL},

	{ "play", "play filename", "Play back data from <filename>",
		cmd_play, FLAG_HIDDEN | FLAG_FILE_ARG, NULL},

	{ "cleardtc", "cleardtc", "Clear DTCs from ECU", cmd_cleardtc, 0, NULL},
	{ "ecus", "ecus", "Show ECU information", cmd_ecus, 0, NULL},

	{ "watch", "watch [raw/nodecode/nol3]",
		"Watch the diagnostic bus and, if not in raw/nol3 mode, decode data",
		cmd_watch, 0, NULL},

	{ "set", "set <parameter value>",
		"Sets/displays parameters, \"set help\" for more info", NULL,
		0, set_cmd_table},

	{ "test", "test <command [params]>",
		"Perform various tests, \"test help\" for more info", NULL,
		0, test_cmd_table},

	{ "diag", "diag <command [params]>",
		"Extended diagnostic functions, \"diag help\" for more info", NULL,
		0, diag_cmd_table},

	{ "vw", "vw <command [params]",
		"VW diagnostic protocol functions, \"vw help\" for more info", NULL,
		0, vag_cmd_table},

	{ "dyno", "dyno <command [params]",
		"Dyno functions, \"dyno help\" for more info", NULL,
		0, dyno_cmd_table},

	{ "debug", "debug [parameter = debug]",
		"Sets/displays debug data and flags, \"debug help\" for available commands", NULL,
		0, debug_cmd_table},

	{ "date", "date", "Prints date & time", cmd_date, FLAG_HIDDEN, NULL},
	{ "#", "#", "Does nothing", cmd_rem, FLAG_HIDDEN, NULL},
	{ "source", "source <file>", "Read commands from a file", cmd_source, FLAG_FILE_ARG, NULL},

	{ "help", "help [command]", "Gives help for a command", cmd_help, 0, NULL },
	{ "?", "? [command]", "Gives help for a command", cmd_help, FLAG_HIDDEN, NULL },
	{ "exit", "exit", "Exits program", cmd_exit, 0, NULL},
	{ "quit", "quit", "Exits program", cmd_exit, FLAG_HIDDEN, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};

#ifdef HAVE_LIBREADLINE
//current global command level for command completion
struct cmd_tbl_entry const *current_cmd_level = root_cmd_table;
//command level in the command line, also needed for command completion
struct cmd_tbl_entry const *completion_cmd_level = root_cmd_table;
#endif

#define INPUT_MAX 1024

/*
 * Caller must free returned buffer. Used if we don't
 * have readline, and when reading init or command files.
 * No line editing or history.
 */
static char *
basic_get_input(const char *prompt)
{
	char *input;
	int do_prompt;

	if (diag_malloc(&input, INPUT_MAX))
			return NULL;

	do_prompt = 1;
	while (1) {
		if (do_prompt && prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		do_prompt = 1;
		if (fgets(input, INPUT_MAX, instream)) {
			break;
		} else {
			if (feof(instream)) {
				free(input);
				return NULL;
			} else {
				/* Ignore error and try again, but don't prompt. */
				clearerr(instream);
				do_prompt = 0;
			}
		}
	}
	input[strcspn(input, "\r\n")] = '\0'; /* Remove trailing CR/LF */
	return input;
}

#ifdef HAVE_LIBREADLINE

/* Caller must free returned buffer */
static char *
get_input(const char *prompt)
{
	char *input;
	/* XXX Does readline change the prompt? */
	char localprompt[128];
	strncpy(localprompt, prompt, sizeof(localprompt));

	input = readline(localprompt);
	if (input && *input)
		add_history(input);
	return input;
}

char *
command_generator(const char *text, int state)
{
	static int list_index, length;
	const struct cmd_tbl_entry *cmd_entry;

	//a new word to complete
	if(state == 0) {
		list_index = 0;
		length = strlen(text);
	}

	//find the command
	while(completion_cmd_level[list_index].command != NULL) {
		cmd_entry = &completion_cmd_level[list_index];
		list_index++;
		if(strncmp(cmd_entry->command, text, length) == 0 && !(cmd_entry->flags & FLAG_HIDDEN)) {
			char *ret_name;
			//we must return a copy of the string; libreadline frees it for us
			if(diag_malloc(&ret_name, strlen(cmd_entry->command)+1) != 0)
				return (char *)NULL;
			strcpy(ret_name, cmd_entry->command);
			return ret_name;
		}
	}
	return (char *)NULL;
}

char **
scantool_completion(const char *text, int start, UNUSED(int end))
{
	char **matches;

	//start == 0 is when the command line is either empty or contains only whitespaces
	if(start == 0) {
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
		while(cmd_length > 0 && cmd[cmd_length] == ' ') {
			//find out what it might be...
			int found = 0;
			for(int i = 0; parsed_level[i].command != NULL; i++) {
				//if found the command on the current level
				if(!(parsed_level[i].flags & FLAG_HIDDEN) &&
				   strlen(parsed_level[i].command) == cmd_length &&
				   strncmp(parsed_level[i].command, cmd, cmd_length) == 0) {
					//does it have sub-commands?
					if(parsed_level[i].sub_cmd_tbl != NULL) {
						//go deeper
						parsed_level = parsed_level[i].sub_cmd_tbl;
						rl_attempted_completion_over = 0;
						found = 1;
					} else if(parsed_level[i].flags & FLAG_FILE_ARG) {
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
			if(found == 0) {
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
	if(matches == NULL)
		//this will disable the default (filename and username) completion in case no command matches are found
		rl_attempted_completion_over = 1;
	return matches;
}

static void
readline_init(void)
{
	//our custom completion function
	rl_attempted_completion_function = scantool_completion;
}

#else	// so no libreadline

static char *
get_input(const char *prompt)
{
	return basic_get_input(prompt);
}

static void readline_init(void) {}

#endif	//HAVE_LIBREADLINE

static char *
command_line_input(const char *prompt)
{
	if (instream == stdin)
		return get_input(prompt);

	/* Reading from init or command file; no prompting or history */
	return basic_get_input(NULL);
}

int
help_common(int argc, char **argv, const struct cmd_tbl_entry *cmd_table)
{
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
		if (!found)
			printf("help: %s: no such command\n", argv[1]);

	} else {
		/* Print help */
		printf("Available commands are :-\n");
		ctp = cmd_table;
		while (ctp->command) {
			if ((ctp->flags & FLAG_HIDDEN) == 0)
				printf("	%s\n", ctp->usage);
			ctp++;
		}
		printf("\nTry \"help <command>\" for further help\n");

	}

	return CMD_OK;
}

static int
cmd_help(int argc, char **argv)
{
	return help_common(argc, argv, root_cmd_table);
}


static int
cmd_date(UNUSED(int argc), UNUSED(char **argv))
{
	struct tm *tm;
	time_t now;

	now = time(NULL);
	tm = localtime(&now);
	printf("%s", asctime(tm));

	return CMD_OK;
}


static int
cmd_rem(UNUSED(int argc), UNUSED(char **argv))
{
	return CMD_OK;
}


static void
log_timestamp(const char *prefix)
{
	unsigned long tv;

	tv=diag_os_chronoms(0);

	fprintf(global_logfp, "%s %04lu.%03lu ", prefix, tv / 1000, tv % 1000);
}

static void
log_command(int argc, char **argv)
{
	int i;

	if (!global_logfp)
		return;

	log_timestamp(">");
	for (i = 0; i < argc; i++)
			fprintf(global_logfp, " %s", argv[i]);
	fprintf(global_logfp, "\n");
}

static int
cmd_log(int argc, char **argv)
{
	char autofilename[20]="";
	char *file;
	struct stat buf;
	time_t now;
	int i;

	file=autofilename;
	if (global_logfp != NULL) {
		printf("Already logging\n");
		return CMD_FAILED;
	}

	/* Turn on logging */
	if (argc > 1) {
			file = argv[1];	//if a file name was specified, use that
	} else {
		//else, generate an auto log file
		for (i = 0; i <= 100; i++) {
			sprintf(autofilename,"log.%02d",i);
			if (stat(file, &buf) == -1 && errno == ENOENT)
				break;
		}
		if (i == 100) {
			printf("Can't create log.%d; remember to clean old auto log files\n",i);
			return CMD_FAILED;
		}
	}

	global_logfp = fopen(file, "a");	//add to end of log or create file

	if (global_logfp == NULL) {
		printf("Failed to create log file %s\n", file);
		return CMD_FAILED;
	}

	now = time(NULL);
	//reset stopwatch:
	unsigned long t1;
	t1=diag_os_chronoms(0);
	(void) diag_os_chronoms(t1);

	fprintf(global_logfp, "%s\n", LOG_FORMAT);
	log_timestamp("#");
	fprintf(global_logfp, "logging started at %s",
		asctime(localtime(&now)));

	printf("Logging to file %s\n", file);
	return CMD_OK;
}


static int
cmd_stoplog(UNUSED(int argc), UNUSED(char **argv))
{
	/* Turn off logging */
	if (global_logfp == NULL) {
		printf("Logging was not on\n");
		return CMD_FAILED;
	}

	fclose(global_logfp);
	global_logfp = NULL;

	return CMD_OK;
}

static int
cmd_play(int argc, char **argv)
{
	FILE *fp;
	//int linenr;

	/* Turn on logging for monitor mode */
	if (argc < 2)
		return CMD_USAGE;

	fp = fopen(argv[1], "r");

	if (fp == NULL) {
		printf("Failed to open log file %s\n", argv[1]);
		return CMD_FAILED;
	}

	//linenr = 0;	//not used yet ?

	/* Read data file in */
	/* XXX logging */

	/* Loop and call display routines */
	while (1) {
		int ch;
		printf("Warning : incomplete code");
		printf("DATE:	+/- to step, S/E to goto start or end, Q to quit\n");
		ch = getc(stdin);
		switch (ch) {
			case '-':
			case '+':
			case 'E':
			case 'e':
			case 'S':
			case 's':
			case 'Q':
			case 'q':
				break;
		}

	}
	fclose(fp);

	return CMD_OK;
}


//cmd_watch : this creates a diag_l3_conn
//TODO: "press any key to stop" ...
static int
cmd_watch(int argc, char **argv)
{
	int rv;
	struct diag_l2_conn *d_l2_conn;
	struct diag_l3_conn *d_l3_conn=NULL;
	struct diag_l0_device *dl0d;
	int rawmode = 0;
	int nodecode = 0;
	int nol3 = 0;

	if (argc > 1) {
		if (strcasecmp(argv[1], "raw") == 0)
			rawmode = 1;
		else if (strcasecmp(argv[1], "nodecode") == 0)
			nodecode = 1;
		else if (strcasecmp(argv[1], "nol3") == 0)
			nol3 = 1;
		else {
			printf("Don't understand \"%s\"\n", argv[1]);
			return CMD_USAGE;
		}
	}

	rv = diag_init();
	if (rv != 0) {
		fprintf(stderr, "diag_init failed\n");
		diag_end();
		return CMD_FAILED;
	}
	dl0d = diag_l2_open(l0_names[set_interface_idx].longname, set_subinterface, global_cfg.L1proto);
	if (dl0d == NULL) {
		rv = diag_geterr();
		printf("Failed to open hardware interface, ");
		if (rv == DIAG_ERR_PROTO_NOTSUPP)
			printf("does not support requested L1 protocol\n");
		else if (rv == DIAG_ERR_BADIFADAPTER)
			printf("adapter probably not connected\n");
		else
			printf("%s\n",diag_errlookup(rv));
		return CMD_FAILED;
	}
	if (rawmode) {
		d_l2_conn = diag_l2_StartCommunications(dl0d, DIAG_L2_PROT_RAW,
			0, set_speed,
			global_cfg.tgt,
			global_cfg.src);
	} else {
		d_l2_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
			DIAG_L2_TYPE_MONINIT, set_speed, global_cfg.tgt, global_cfg.src);
	}

	if (d_l2_conn == NULL) {
		printf("Failed to connect to hardware in monitor mode\n");
		diag_l2_close(dl0d);
		return CMD_FAILED;
	}
	//here we have a valid d_l2_conn over dl0d.

	if (rawmode == 0) {
		/* Put the SAE J1979 stack on top of the ISO device */

		if (nol3 == 0) {
			d_l3_conn = diag_l3_start("SAEJ1979", d_l2_conn);
			if (d_l3_conn == NULL) {
				printf("Failed to enable SAEJ1979 mode\n");
				diag_l2_StopCommunications(d_l2_conn);
				diag_l2_close(dl0d);
				return CMD_FAILED;
			}
		} else {
			d_l3_conn = NULL;
		}

		printf("Waiting for data to be received\n");
		while (1) {
			if (d_l3_conn != NULL) {
				rv = diag_l3_recv(d_l3_conn, 10000,
					j1979_watch_rcv,
					(nodecode) ? NULL:(void *)d_l3_conn);
			} else {
				rv = diag_l2_recv(d_l2_conn, 10000,
					j1979_watch_rcv, NULL);
			}
			if (rv == 0)
				continue;
			if (rv == DIAG_ERR_TIMEOUT)
				continue;
		}
	} else {
		//rawmode !=0 here
		/*
		 * And just read stuff, callback routine will print out the data
		 */
		printf("Waiting for data to be received\n");
		while (1) {
			rv = diag_l2_recv(d_l2_conn, 10000,
				j1979_data_rcv, (void *)&_RQST_HANDLE_WATCH);
			if (rv == 0)
				continue;
			if (rv == DIAG_ERR_TIMEOUT)
				continue;
			printf("recv returns %d\n", rv);
			break;
		}
	}
	if (d_l3_conn != NULL)
		diag_l3_stop(d_l3_conn);

	diag_l2_StopCommunications(d_l2_conn);
	diag_l2_close(dl0d);

	return CMD_OK;
}


/*
 * Print the monitorable data out, use SI units by default, or "english"
 * units
 */
static void
print_current_data(int english)
{
	char buf[24];
	ecu_data_t *ep;
	unsigned int i;
	unsigned int j;

	printf("\n\nPress return to checkpoint then return to quit\n");
	printf("%-30.30s %-15.15s FreezeFrame\n",
		"Parameter", "Current");

	for (j = 0 ; get_pid(j) != NULL ; j++) {
		const struct pid *p = get_pid(j) ;

		for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
			if (DATA_VALID(p, ep->mode1_data) ||
				DATA_VALID(p, ep->mode2_data)) {
				printf("%-30.30s ", p->desc);

				if (DATA_VALID(p, ep->mode1_data))
					p->cust_sprintf(buf, english, p,
						ep->mode1_data, 2);
				else
					sprintf(buf, "-----");

				printf("%-15.15s ", buf);

				if (DATA_VALID(p, ep->mode2_data))
					p->cust_sprintf(buf, english, p,
						ep->mode2_data, 3);
				else
					sprintf(buf, "-----");

				printf("%-15.15s\n", buf);
			}
		}
	}
}

static void
log_response(int ecu, response_t *r)
{
	int i;
	assert(global_logfp != NULL);

	/* Only print good records */
	if (r->type != TYPE_GOOD)
		return;

	printf("%d: ", ecu);
	for (i = 0; i < r->len; i++) {
		fprintf(global_logfp, "%02X ", r->data[i]);
	}
	fprintf(global_logfp, "\n");
}

static void
log_current_data(void)
{
	response_t *r;
	ecu_data_t *ep;
	unsigned int i;

	if (!global_logfp)
		return;

	log_timestamp("D");
	fprintf(global_logfp, "MODE 1 DATA\n");
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (r = ep->mode1_data;
			r < &ep->mode1_data[ARRAY_SIZE(ep->mode1_data)]; r++) {
				log_response((int)i, r);
		}
	}

	log_timestamp("D");
	fprintf(global_logfp, "MODE 2 DATA\n");
	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		for (r = ep->mode2_data;
			r < &ep->mode2_data[ARRAY_SIZE(ep->mode2_data)]; r++) {
			log_response((int)i, r);
		}
	}
}

static int
cmd_monitor(int argc, char **argv)
{
	int rv;
	int english = 0;


	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_FAILED;
	}

	// If user states English or Metric, use that, else use config item
	if (argc > 1) {
		if (strcasecmp(argv[1], "english") == 0)
			english = 1;
		else if (strcasecmp(argv[1], "metric") == 0)
			english = 0;
		else
			return CMD_USAGE;
	} else {
		english = global_cfg.units;
	}

	printf("Please wait\n");

	/*
	 * Now just receive data and log it for ever
	 */

	while (1) {
		rv = do_j1979_getdata(1);
		/* Key pressed */
		if (rv == 1 || rv<0) {
			//enter was pressed to interrupt,
			//or there was an error.
			break;
		}
		/* print the data */
		print_current_data(english);

		/* Save the data */
		log_current_data();

		/* Get/Print current DTCs */
		do_j1979_cms();
	}
	return CMD_OK;
}

// scan : use existing L3 J1979 connection, or establish a new one by trying all known protos.

static int
cmd_scan(UNUSED(int argc), UNUSED(char **argv))
{
	int rv=DIAG_ERR_GENERAL;
	if (argc > 1)
		return CMD_USAGE;

	if (global_state == STATE_L3ADDED) {
		if (global_l3_conn != NULL) {
			if (strcmp(global_l3_conn->d_l3_proto->proto_name, "SAEJ1979") ==0) {
				printf("Re-using active L3 connection.\n");
				rv=0;
			} else {
				printf("L3 connection must be SAEJ1979 ! Try disconnecting and running scan again.\n");
				return CMD_FAILED;
			}
		} else {
			printf("Error: inconsistent global_state. Report this!\n");
			return CMD_FAILED;
		}
	} else if (global_state >= STATE_CONNECTED) {
		printf("Already connected, please disconnect first, or manually add SAEJ1979 L3 layer.\n");
		return CMD_FAILED;
	} else {
		rv = ecu_connect();
	}

	if (rv == 0) {
		printf("Connection to ECU established\n");

		/* Now ask basic info from ECU */
		do_j1979_basics();
		/* Now get test results for continuously monitored systems */
		do_j1979_cms();
		/* And the non continuously monitored tests */
		printf("Non-continuously monitored system tests (failures only): -\n");
		do_j1979_ncms(0);
	} else {
		printf("Connection to ECU failed\n");
		printf("Please check :-\n");
		printf("\tAdapter is connected to PC\n");
		printf("\tCable is connected to Vehicle\n");
		printf("\tVehicle is switched on\n");
		printf("\tVehicle is OBDII compliant\n");
		return CMD_FAILED;
	}
	return CMD_OK;
}



static int
cmd_cleardtc(UNUSED(int argc), UNUSED(char **argv))
{
	char *input;

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	input = basic_get_input("Are you sure you wish to clear the Diagnostic "
			"Trouble Codes (y/n) ? ");
	if (!input)
		return CMD_OK;

	if ((strcasecmp(input, "yes") == 0) || (strcasecmp(input, "y")==0)) {
		if (diag_cleardtc() == 0)
			printf("Done\n");
		else
			printf("Failed\n");
	} else {
		printf("Not done\n");
	}

	free(input);
	return CMD_OK;
}



static int
cmd_ecus(UNUSED(int argc), UNUSED(char **argv))
{
	ecu_data_t *ep;
	unsigned int i;

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_OK;
	}

	printf("%d ECUs found\n", ecu_count);

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		printf("ECU %d: Address 0x%02X ", i, ep->ecu_addr & 0xff);
		if (ep->supress)
			printf("output supressed for monitor mode\n");
		else
			printf("\n");
	}
	return CMD_OK;
}


/*
 * CLI, returns results as CMD_xxx (such as CMD_EXIT)
 * If argc is supplied, then this is one shot cli, ie run the command
 */
static int
do_cli(const struct cmd_tbl_entry *cmd_tbl, const char *prompt, int argc, char **argv)
{
	/* Build up argc/argv */
	const struct cmd_tbl_entry *ctp;
	int cmd_argc;
	char *cmd_argv[20];
	char *input = NULL;
	int rv, done;
	int i;

	char promptbuf[PROMPTBUFSIZE];	/* Was 1024, who needs that long a prompt? (the part before user input up to '>') */
	static char nullstr[2]={0,0};

#ifdef HAVE_LIBREADLINE
	//set the current command level for command completion
	current_cmd_level = cmd_tbl;
#endif

	rv = 0, done = 0;
	snprintf(promptbuf, PROMPTBUFSIZE, "%s> ", prompt);
	while (!done) {
		char *inptr, *s;

		if (argc == 0) {
			/* Get Input */
			if (input)
				free(input);
			input = command_line_input(promptbuf);
			if (!input) {
					if (instream == stdin)
						printf("\n");
					break;
			}

			/* Parse it */
			inptr = input;
			if (*inptr == '@') 	//printing comment
			{
				printf("%s\n", inptr);
				continue;
			}
			if (*inptr == '#')		//non-printing comment
			{
				continue;
			}
			cmd_argc = 0;
			while ( (s = strtok(inptr, " 	")) != NULL ) {
				cmd_argv[cmd_argc] = s;
				inptr = NULL;
				if (cmd_argc == (ARRAY_SIZE(cmd_argv)-1))
					break;
				cmd_argc++;
			}
			cmd_argv[cmd_argc] = nullstr;
		} else {
			/* Use supplied argc */
			cmd_argc = argc;
			for (i=0; i<=argc; i++)
				cmd_argv[i] = argv[i];
		}

		if (cmd_argc != 0) {
			ctp = cmd_tbl;
			while (ctp->command) {
				if (strcasecmp(ctp->command, cmd_argv[0]) == 0) {
					if (ctp->sub_cmd_tbl) {
						log_command(1, cmd_argv);
						snprintf(promptbuf, PROMPTBUFSIZE,"%s/%s",
							prompt, ctp->command);
						/* Sub menu */
						rv = do_cli(ctp->sub_cmd_tbl,
							promptbuf,
							cmd_argc-1,
							&cmd_argv[1]);
#ifdef HAVE_LIBREADLINE
						//went out of the sub-menu, so update the command level for command completion
						current_cmd_level = cmd_tbl;
#endif
						if (rv==CMD_EXIT)	//allow exiting prog. from a submenu
							done=1;
						snprintf(promptbuf, PROMPTBUFSIZE, "%s> ", prompt);
					} else {
						/* Found command */
						log_command(cmd_argc, cmd_argv);
						rv = ctp->routine(cmd_argc, cmd_argv);
						switch (rv) {
							case CMD_USAGE:
								printf("Usage: %s\n", ctp->usage);
								break;
							case CMD_EXIT:
								rv = CMD_EXIT;
								done = 1;
								break;
							case CMD_UP:
								rv = CMD_UP;
								done = 1;
								break;
						}
					}
					break;
				}
				if (!done)
					ctp++;
			}
			if (ctp->command == NULL) {
				printf("Huh? Try \"help\"\n");
			}
			if (argc) {
				/* Single command */
				done = 1;
				break;
			}
		}
		if (done)
			break;
	}	//while !done
	if (input)
			free(input);
	if (rv == CMD_UP)
		return CMD_OK;
	if (rv == CMD_EXIT) {
		char *disco="disconnect";
		if (global_logfp != NULL)
			cmd_stoplog(0, NULL);
		if (global_state > STATE_IDLE) {
			do_cli(diag_cmd_table, "", 1, &disco);	//XXX should be called recursively in case there are >1 active L3 conns...
		}
		rv=diag_end();
		if (rv)
			fprintf(stderr, FLFMT "diag_end failed !?\n", FL);
		rv = CMD_EXIT;
	}
	return rv;
}

/* execute commands read from *filename;
 * ret CMD_OK if file was readable (command/parsing problems are OK)
 * ret CMD_FAILED if file was unreadable
 * forward CMD_EXIT if applicable */
static int
command_file(char *filename)
{
	int rv;
	FILE *prev_instream = instream;

	if ( (instream=fopen(filename, "r"))) {
		rv=do_cli(root_cmd_table, progname, 0, NULL);
		fclose(instream);
		instream=prev_instream;
		return (rv==CMD_EXIT)? CMD_EXIT:CMD_OK;
	}
	instream=prev_instream;
	return CMD_FAILED;
}

static int
cmd_source(int argc, char **argv)
{
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

//rc_file : returns CMD_OK or CMD_EXIT only.
static int
rc_file(void)
{
	int rv;
	//this loads either a $home/.<progname>.rc or ./<progname>.ini (in order of preference)
	//to load general settings.

	/*
	 * "." files don't play that well on some systems.
	 * USE_RCFILE is not defined by default.
	 */

#ifdef USE_RCFILE
	char *rchomeinit;
	char *homedir;
	homedir = getenv("HOME");
	FILE *newrcfile;

	if (homedir) {
		/* we add "/." and "rc" ... 4 characters */
		if (diag_malloc(&rchomeinit, strlen(homedir) + strlen(progname) + 5)) {
			diag_iseterr(DIAG_ERR_NOMEM);
			return CMD_OK;
		}
		strcpy(rchomeinit, homedir);
		strcat(rchomeinit, "/.");
		strcat(rchomeinit, projname);
		strcat(rchomeinit, "rc");

		rv=command_file(rchomeinit);
		if (rv == CMD_FAILED) {
			fprintf(stderr, FLFMT "Could not load rc file %s; ", FL, rchomeinit);
			newrcfile=fopen(rchomeinit,"a");
			if (newrcfile) {
				//create the file if it didn't exist
				fprintf(newrcfile, "\n#empty rcfile auto created by %s\n",progname);
				fclose(newrcfile);
				fprintf(stderr, "empty file created.\n");
				free(rchomeinit);
				return CMD_OK;
			} else {
				//could not create empty rcfile
				fprintf(stderr, "could not create empty file %s.", rchomeinit);
				free(rchomeinit);
				return CMD_OK;
			}
		} else {
			//command_file was at least partly successful (rc file exists)
			printf("%s: Settings loaded from %s\n",progname,rchomeinit);
			free(rchomeinit);
			return CMD_OK;
		}

	}	//if (homedir)
#endif


#ifdef USE_INIFILE
	char * inihomeinit;
	if (diag_malloc(&inihomeinit, strlen(progname) + strlen(".ini") + 1)) {
		diag_iseterr(DIAG_ERR_NOMEM);
		return CMD_OK;
	}

	strcpy(inihomeinit, progname);
	strcat(inihomeinit, ".ini");

	rv=command_file(inihomeinit);
	if (rv == CMD_FAILED) {
		fprintf(stderr, FLFMT "Problem with %s, no configuration loaded\n", FL, inihomeinit);
		free(inihomeinit);
		return CMD_OK;
	}
	printf("%s: Settings loaded from %s\n", progname, inihomeinit);
	free(inihomeinit);
#endif
	return rv;	//could be CMD_EXIT

}

void
enter_cli(const char *name)
{
	global_logfp = NULL;
	//progname = name;	//we use the supplied *name instead.

	printf("%s: %s version %s\n", name, projname, PACKAGE_VERSION);
	printf("%s: Type HELP for a list of commands\n", name);
	printf("%s: Type SCAN to start ODBII Scan\n", name);
	printf("%s: Then use MONITOR to monitor real-time data\n", name);
	printf("%s: **** IMPORTANT : this is beta software ! Use at your own risk.\n", name);
	printf("%s: **** Remember, \"debug all -1\" displays all debugging info.\n", name);

	readline_init();
	set_init();
	if (rc_file() != CMD_EXIT) {
		printf("\n");
		/* And go start CLI */
		instream = stdin;
		(void)do_cli(root_cmd_table, name, 0, NULL);
	}
	set_close();

}

/*
 * ************
 * Useful, non specific routines
 * ************
 */

/*
 * Decimal/Octal/Hex to integer routine
 * formats:
 * [-]0[0-7] : octal
 * [-]0x[0-9,A-F,a-f] : hex
 * [-]$[0-9,A-F,a-f] : hex
 * [-][0-9] : dec
 * Returns 0 if unable to decode.
 */
int htoi(char *buf)
{
	/* Hex text to int */
	int rv = 0;
	int base = 10;
	int sign=0;	//1 = positive; 0 =neg

	if (*buf != '-') {
		//change sign
		sign=1;
	} else {
		buf++;
	}

	if (*buf == '$') {
		base = 16;
		buf++;
	} else if (*buf == '0') {
		buf++;
		if (tolower(*buf) == 'x') {
			base = 16;
			buf++;
		} else {
			base = 8;
		}
	}

	while (*buf) {
		char upp = toupper(*buf);
		int val;

		if ((upp >= '0') && (upp <= '9')) {
			val = ((*buf) - '0');
		} else if ((upp >= 'A') && (upp <= 'F')) {
			val = (upp - 'A' + 10);
		} else {
			return 0;
		}
		if (val >= base)	/* Value too big for this base */
			return 0;
		rv *= base;
		rv += val;

		buf++;
	}
	return sign? rv:-rv ;
}

/*
 * Wait until ENTER is pressed
 */
void wait_enter(const char *message)
{
	printf(message);
	while (1) {
		int ch = getc(stdin);
		if (ch == '\n')
		break;
	}
}

/*
 * Determine whether ENTER has been pressed
 */
int pressed_enter()
{
#ifdef WIN32
	fprintf(stderr, "Warning : diag_os_ipending() called from pressed_enter !! Please report this !\n");
#endif
	return diag_os_ipending();
}
