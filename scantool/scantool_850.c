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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_err.h"
#include "diag_os.h"

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

static bool have_read_dtcs = false;

static int cmd_850_help(int argc, char **argv);
static int cmd_850_connect(int argc, char **argv);
static int cmd_850_disconnect(int argc, UNUSED(char **argv));
static int cmd_850_ping(int argc, UNUSED(char **argv));
static int cmd_850_sendreq(int argc, char **argv);
static int cmd_850_peek(int argc, char **argv);
static int cmd_850_dumpram(int argc, char **argv);
static int cmd_850_read(int argc, char **argv);
static int cmd_850_readnv(int argc, char **argv);
static int cmd_850_id(int argc, UNUSED(char **argv));
static int cmd_850_dtc(int argc, UNUSED(char **argv));
static int cmd_850_cleardtc(int argc, UNUSED(char **argv));

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
	{ "peek", "peek <addr1>[w|l][.addr2] [addr2 ...] [live]", "Display contents of RAM, once or continuously",
		cmd_850_peek, 0, NULL},
	{ "dumpram", "dumpram <filename> [fast]", "Dump entire RAM contents to file (Warning: takes 20+ minutes)",
		cmd_850_dumpram, 0, NULL},
	{ "read", "read <id1>|*<addr1> [id2 ...] [live]", "Display live data, once or continuously",
		cmd_850_read, 0, NULL},
	{ "readnv", "readnv id1 [id2 ...]", "Display non-volatile data",
		cmd_850_readnv, 0, NULL},
	{ "id", "id", "Display ECU identification",
		cmd_850_id, 0, NULL},
	{ "dtc", "dtc", "Retrieve DTCs",
		cmd_850_dtc, 0, NULL},
	{ "cleardtc", "cleardtc", "Clear DTCs from ECU",
		cmd_850_cleardtc, 0, NULL},

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
	have_read_dtcs = false;

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
	have_read_dtcs = false;
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

/*
 * Print one line of a hex dump, with an address followed by one or more
 * values.
 */
static int
print_hexdump_line(FILE *f, uint16_t addr, int addr_chars, uint8_t *buf, uint16_t len)
{
	if (fprintf(f, "%0*X:", addr_chars, addr) < 0)
		return 1;
	while (len--) {
		if (fprintf(f, " %02X", *buf++) < 0)
			return 1;
	}
	if (fputc('\n', f) == EOF)
		return 1;
	return 0;
}

struct read_or_peek_item {
	uint16_t start;		/* starting address or identifier */
	uint16_t end;		/* ending address - for peeks only */
	enum namespace ns;
};

/*
 * Parse an address argument on a peek command line.
 */
static int
parse_peek_arg(char *arg, struct read_or_peek_item *item)
{
	char *p, *q;

	item->ns = NS_MEMORY;
	item->start = strtoul(arg, &p, 0);
	if (*p == '\0') {
		item->end = item->start;
	} else if ((p[0] == 'w' || p[0] == 'W') && p[1] == '\0') {
		item->end = item->start + 1;
	} else if ((p[0] == 'l' || p[0] == 'L') && p[1] == '\0') {
		item->end = item->start + 3;
	} else if ((p[0] == '.' || p[0] == '-') && p[1] != '\0') {
		item->end = strtoul(p+1, &q, 0);
		if (*q != '\0' || item->end < item->start) {
			printf("Invalid address range '%s'\n", arg);
			return 1;
		}
	} else {
		printf("Invalid address '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on a read command line.
 */
static int
parse_read_arg(char *arg, struct read_or_peek_item *item)
{
	char *p;

	if(arg[0] == '*')
		return parse_peek_arg(arg+1, item);

	item->ns = NS_LIVEDATA;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on a readnv command line.
 */
static int
parse_readnv_arg(char *arg, struct read_or_peek_item *item)
{
	char *p;

	item->ns = NS_NV;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}


/*
 * Execute a read, peek or readnv command.
 */
static int
read_family(int argc, char **argv, enum namespace ns)
{
	int count;
	int i;
	bool continuous;
	struct read_or_peek_item *items;
	uint8_t buf[20];
	uint16_t addr, len;
	int gotbytes;

	if (!valid_arg_count(2, argc, 999))
                return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	continuous = false;
	count = argc - 1;

	if (ns!=NS_NV && strcasecmp(argv[argc-1], "live")==0) {
		continuous = true;
		count--;
		if (count < 1)
			return CMD_USAGE;
	}

	items = calloc(sizeof(items[0]), count);
	if (items == NULL)
		return diag_iseterr(DIAG_ERR_NOMEM);
	for (i=0; i<count; i++) {
		if (ns == NS_MEMORY) {
			if (parse_peek_arg(argv[i+1], &(items[i])) != 0)
				goto done;
		} else if (ns == NS_LIVEDATA) {
			if (parse_read_arg(argv[i+1], &(items[i])) != 0)
				goto done;
		} else { /* NS_NV */
			if (parse_readnv_arg(argv[i+1], &(items[i])) != 0)
				goto done;
		}
	}

	diag_os_ipending();
	while (1) {
		for (i=0; i<count; i++) {
			if(items[i].ns != NS_MEMORY) {
				addr = items[i].start;
				gotbytes = diag_l7_volvo_read(global_l2_conn, items[i].ns, addr, sizeof(buf), buf);
				if (gotbytes < 0) {
					printf("Error reading %02X\n", addr);
					goto done;
				} else if (gotbytes == 0) {
					printf("%02X: no data\n", addr);
				} else if ((unsigned int)gotbytes > sizeof(buf)) {
					print_hexdump_line(stdout, addr, 2, buf, sizeof(buf));
					printf(" (%d bytes received, only first %d shown)\n", gotbytes, sizeof(buf));
				} else if (gotbytes > 0) {
					print_hexdump_line(stdout, addr, 2, buf, gotbytes);
				}
			} else {
				addr = items[i].start;
				len = (items[i].end - items[i].start) + 1;
				while(len > 0) {
					if(diag_l7_volvo_read(global_l2_conn, NS_MEMORY, addr, (len<8)?len:8, buf) == len) {
						print_hexdump_line(stdout, addr, 4, buf, (len<8)?len:8);
					} else {
						printf("Error reading %s%04X\n", (ns==NS_LIVEDATA)?"*":"", items[i].start);
						goto done;
					}
				len -= (len<8)?len:8;
				addr += 8;
				}
			}
		}
		if (!continuous || diag_os_ipending())
			break;
	}

done:
	free(items);
	return CMD_OK;
}

/*
 * Read and display one or more values from RAM.
 *
 * Takes a list of addresses to read. Each address can have a suffix "w" or
 * "l" to indicate 2 or 4 bytes, respectively; otherwise a single byte is
 * read. Each item in the list can also be an address range with the starting
 * and ending addresses separated by ".".
 *
 * The word "live" can be added at the end to continuously read and display
 * the requested addresses until interrupted.
 */
static int
cmd_850_peek(int argc, char **argv)
{
	return read_family(argc, argv, NS_MEMORY);
}

/*
 * Read and display one or more live data parameters.
 *
 * Takes a list of one-byte identifier values. If a value is prefixed with *,
 * it is treated as an address or address range to read from RAM instead of
 * a live data parameter identifier; in this way, a list of "read" and "peek"
 * operations can be done in a single command.
 *
 * The word "live" can be added at the end to continuously read and display
 * the requested addresses until interrupted.
 */
static int
cmd_850_read(int argc, char **argv)
{
	return read_family(argc, argv, NS_LIVEDATA);
}

/*
 * Read and display one or more non-volatile parameters.
 *
 * Takes a list of one-byte identifier values.
 */
static int
cmd_850_readnv(int argc, char **argv)
{
	return read_family(argc, argv, NS_NV);
}

/*
 * Display ECU identification.
 */
static int
cmd_850_id(int argc, UNUSED(char **argv))
{
	uint8_t buf[15];
	int rv;

	if (!valid_arg_count(1, argc, 1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	rv = diag_l7_volvo_read(global_l2_conn, NS_NV, 0xf0, sizeof(buf), buf);
	if (rv < 0) {
		printf("Couldn't read identification.\n");
		return CMD_OK;
	}
	if (rv != sizeof(buf)) {
		printf("Identification response was %d bytes, expected %d\n", rv, sizeof(buf));
		return CMD_OK;
	}
	if (buf[0] != 0) {
		printf("First identification response byte was %02X, expected 0\n", buf[0]);
		return CMD_OK;
	}
	if (!isprint(buf[5]) || !isprint(buf[6]) || !isprint(buf[7]) ||
	    !isprint(buf[12]) || !isprint(buf[13]) || !isprint(buf[14])) {
		printf("Unexpected characters in version response\n");
		return CMD_OK;
	}

	printf("Hardware ID: P%02X%02X%02X%02X revision %.3s\n", buf[1], buf[2], buf[3], buf[4], buf+5);
	printf("Software ID:  %02X%02X%02X%02X revision %.3s\n", buf[8], buf[9], buf[10], buf[11], buf+12);

	return CMD_OK;
}

/*
 * Dump the entire contents of RAM to the specified file as a hex dump with
 * 8 bytes per line.
 *
 * ECUs may have holes in the memory map (example: Motronic M4.4 has RAM
 * at 0000-00FF and XRAM at F800-FFFF and nothing in between). So we try
 * reading in 8 byte chunks and if an attempt to read a given address fails,
 * just skip the hexdump line for that address and continue on to the next one.
 * If the "fast" option is specified on the command line, skip ahead to 0xF000
 * when a read attempt fails.
 */
static int
cmd_850_dumpram(int argc, char **argv)
{
	uint16_t addr;
	FILE *f;
	bool happy;
	uint8_t buf[8];
	bool fast;

	if (argc == 2) {
		fast = false;
	} else if(argc == 3 && strcasecmp(argv[2], "fast") == 0) {
		fast = true;
	} else {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	f = fopen(argv[1], "w");
	if (f == NULL) {
		perror("Can't open file");
		return CMD_OK;
	}

	printf("Dumping RAM to %s...\n", argv[1]);

	addr = 0;
	while (1) {
		if (diag_l7_volvo_read(global_l2_conn, NS_MEMORY, addr, 8, buf) == 8) {
			happy = 1;
			errno = 0;
			if (print_hexdump_line(f, addr, 4, buf, 8) != 0) {
				if (errno != 0) {
					perror("\nError writing file");
				} else {
					/*error, but fprintf didn't set errno*/
					printf("\nError writing file");
				}
				return CMD_OK;
			}
		} else {
			happy = 0;
		}
		if ((addr&0x1f) == 0) {
			printf("\r%04X %s", addr, happy?":)":":/");
			fflush(stdout);
		}
		if (addr == 0xfff8)
			break;
		addr += 8;

		if (fast && !happy && addr < 0xf000)
			addr = 0xf000;
	}

	if (fclose(f) != 0) {
		perror("\nError writing file");
		return CMD_OK;
	}

	printf("\r%04X :D\n", addr);

	return CMD_OK;
}

/*
 * Display list of stored DTCs.
 */
static int
cmd_850_dtc(int argc, UNUSED(char **argv))
{
	uint8_t buf[12];
	int rv;
	int i;

	if (!valid_arg_count(1, argc, 1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	rv = diag_l7_volvo_dtclist(global_l2_conn, sizeof(buf), buf);
	if (rv < 0) {
		printf("Couldn't retrieve DTCs.\n");
		return CMD_OK;
	}
	have_read_dtcs = true;

	if (rv == 0) {
		printf("No stored DTCs.\n");
		return CMD_OK;
	}

	printf("Stored DTCs:");
	for (i=0; i<rv; i++) {
		printf(" %02X", buf[i]);
	}
	putchar('\n');

	return CMD_OK;
}

/*
 * Clear stored DTCs.
 */
static int
cmd_850_cleardtc(int argc, UNUSED(char **argv))
{
	char *input;
	int rv;

	if (!valid_arg_count(1, argc, 1))
		return CMD_USAGE;

	if(!valid_connection_status(CONNECTED_KWP6227))
		return CMD_OK;

	input = basic_get_input("Are you sure you wish to clear the Diagnostic Trouble Codes (y/n) ? ", stdin);
	if (!input)
		return CMD_OK;

	if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
		printf("Not done\n");
		goto done;
	}

	if (!have_read_dtcs) {
		free(input);
		input = basic_get_input("You haven't read the DTCs yet. Are you sure you wish to clear them (y/n) ? ", stdin);
		if (!input)
			return CMD_OK;
		if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
			printf("Not done\n");
			goto done;
		}
	}

	rv = diag_l7_volvo_cleardtc(global_l2_conn);
	if (rv == 0) {
		printf("No DTCs to clear!\n");
	} else if (rv == 1) {
		printf("Done\n");
	} else {
		printf("Failed\n");
	}

done:
	free(input);
	return CMD_OK;
}
