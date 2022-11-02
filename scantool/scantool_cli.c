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


#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_os.h"

#include "libcli.h"
#include "scantool_cli.h"
#include "scantool_diag.h"

const char *progname;
const char projname[]=PROJECT_NAME;

int diag_cli_debug;	//debug level

FILE		*global_logfp;		/* Monitor log output file pointer */
unsigned long global_log_tstart;    /* timestamp datum (in ms) of beginning of log */
#define LOG_FORMAT	"FREEDIAG log format 0.2"


//ugly, global data. Could be struct-ed together eventually
struct diag_l2_conn *global_l2_conn;
struct diag_l3_conn *global_l3_conn;
enum globstate global_state = STATE_IDLE;
struct diag_l0_device *global_dl0d;

/* ROOT main menu */

/* Main menu commands */

static int cmd_log(int argc, char **argv);
static int cmd_stoplog(int argc, char **argv);

UNUSED(static int cmd_play(int argc, char **argv));

static int cmd_date(int argc, char **argv);
static int cmd_rem(int argc, char **argv);


/* this table is appended to the "extra" cmdtable to construct the whole root cmd table */
static const struct cmd_tbl_entry basic_cmd_table[] = {
	{ "log", "log <filename>", "Log monitor data to <filename>",
		cmd_log, FLAG_FILE_ARG, NULL},
	{ "stoplog", "stoplog", "Stop logging", cmd_stoplog, 0, NULL},

	{ "play", "play filename", "Play back data from <filename>",
		cmd_play, FLAG_HIDDEN | FLAG_FILE_ARG, NULL},

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

	{ "850", "850 <command [params]>",
		"'96-'98 Volvo 850/S70/V70/etc functions, \"850 help\" for more info", NULL,
		0, v850_cmd_table},

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


static int
cmd_date(UNUSED(int argc), UNUSED(char **argv)) {
	struct tm *tm;
	time_t now;
	char str[256];

	now = time(NULL);
	tm = localtime(&now);
	if (strftime(str, sizeof(str), "%a %b %d %H:%M:%S %Y", tm) == 0) {
		printf("unable to format timestamp");
	}
	else {
		printf("%s", str);
	}


	return CMD_OK;
}


static int
cmd_rem(UNUSED(int argc), UNUSED(char **argv)) {
	return CMD_OK;
}


void
log_timestamp(const char *prefix) {
	unsigned long tv;

	tv = diag_os_getms() - global_log_tstart;

	fprintf(global_logfp, "%s %04lu.%03lu ", prefix, tv / 1000, tv % 1000);
}

static void scantool_atexit(void) {
	cmd_diag_disconnect(0, NULL);
	return;
}

static void log_command(int argc, char **argv) {
	int i;

	if (!global_logfp) {
		return;
	}

	log_timestamp(">");
	for (i = 0; i < argc; i++) {
		fprintf(global_logfp, " %s", argv[i]);
	}
	fprintf(global_logfp, "\n");
}

static int
cmd_log(int argc, char **argv) {
	char autofilename[20]="";
	char *file;
	time_t now;
	char timestr[256];
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
		for (i = 0; i < 100; i++) {
			FILE *testexist;
			sprintf(autofilename,"log.%02d",i);
			testexist = fopen(autofilename, "r");
			if (testexist == NULL) {
				//file name is free: use that
				break;
			}
			fclose(testexist);
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
	//reset timestamp reference:
    global_log_tstart=diag_os_getms();

	fprintf(global_logfp, "%s\n", LOG_FORMAT);
	log_timestamp("#");
	if (strftime(timestr, sizeof(timestr), "%a %b %d %H:%M:%S %Y", localtime(&now)) == 0) {
		fprintf(global_logfp, "unable to format timestamp");
	}
	else {
		fprintf(global_logfp, "logging started at %s", timestr);
	}
	printf("Logging to file %s\n", file);
	return CMD_OK;
}


static int
cmd_stoplog(UNUSED(int argc), UNUSED(char **argv)) {
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
cmd_play(int argc, char **argv) {
	FILE *fp;
	//int linenr;

	/* Turn on logging for monitor mode */
	if (argc < 2) {
		return CMD_USAGE;
	}

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
		printf("DATE:\t+/- to step, S/E to goto start or end, Q to quit\n");
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


char *find_rcfile(void) {

#ifdef USE_RCFILE
	char *rchomeinit;
	char *homedir;
	homedir = getenv("HOME");
	FILE *newrcfile;

	if (homedir) {
		/* we add "/." and "rc" ... 4 characters */
		if (diag_malloc(&rchomeinit, strlen(homedir) + strlen(projname) + 5)) {
			return NULL;
		}
		strcpy(rchomeinit, homedir);
		strcat(rchomeinit, "/.");
		strcat(rchomeinit, projname);
		strcat(rchomeinit, "rc");

		newrcfile=fopen(rchomeinit,"r");
		if (newrcfile) {
			fclose(newrcfile);
			return rchomeinit;
		} else {
			fprintf(stderr, FLFMT "Could not open %s : ignoring", FL, rchomeinit);
			free(rchomeinit);
			//try INIFILE next, if enabled
		}
	}
#endif


#ifdef USE_INIFILE
	char *inihomeinit;
	FILE *inifile;
	if (diag_malloc(&inihomeinit, strlen(progname) + strlen(".ini") + 1)) {
		return NULL;
	}

	strcpy(inihomeinit, progname);
	strcat(inihomeinit, ".ini");

	inifile=fopen(inihomeinit,"r");
	if (inifile) {
		fclose(inifile);
		return inihomeinit;
	} else {
		fprintf(stderr, FLFMT "Could not open %s : ignoring", FL, inihomeinit);
		free(inihomeinit);
	}
#endif

	return NULL;
}


/** temporary enter_cli() wrapper
 * 
 * combines basic_table with extra_cmdtable before calling enter_cli.
 * Will become irrelevant after "libcli" split
 * 
 * TODO: Leaks memory because the concatenated table is never free'd.
 */
void scantool_cli(const char *prompt, const char *initscript, const struct cmd_tbl_entry *extra_cmdtable) {

	const struct cmd_tbl_entry *total_table = basic_cmd_table;

	global_logfp = NULL;

	if (extra_cmdtable) {
		// alloc a new table to append extra table
		int i = 0;
		const struct cmd_tbl_entry *ctp_iter;
		struct cmd_tbl_entry *ctp;
		for (ctp_iter = extra_cmdtable; ctp_iter && ctp_iter->command; ctp_iter++) {
			i++;
		}
		assert(i); //need at least 1 entry...

		diag_calloc(&ctp, i + ARRAY_SIZE(basic_cmd_table));
		if (!ctp) {
			return;
		}
		memcpy(ctp, extra_cmdtable, i * sizeof(struct cmd_tbl_entry));
		memcpy(&ctp[i], basic_cmd_table, sizeof(basic_cmd_table));
		total_table = ctp;
	}

	struct cli_callbacks cbs = {
		.cli_logcmd = log_command,
		.cli_atexit = scantool_atexit,
	};
	cli_set_callbacks(&cbs);
	enter_cli(prompt, initscript, total_table);
	return;
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
int htoi(char *buf) {
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
		if (val >= base) { /* Value too big for this base */
			return 0;
		}
		rv *= base;
		rv += val;

		buf++;
	}
	return sign? rv:-rv ;
}

/*
 * Wait until ENTER is pressed
 */
void wait_enter(const char *message) {
	printf("%s", message);
	while (1) {
		int ch = getc(stdin);
		if (ch == '\n') {
			break;
		}
	}
}

/*
 * Determine whether ENTER has been pressed
 */
int pressed_enter() {
	return diag_os_ipending();
}
