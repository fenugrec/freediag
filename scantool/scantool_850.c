/*
 *      freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017, 2023 Adam Goldman
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

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l7.h"
#include "diag_err.h"
#include "diag_os.h"

#include "libcli.h"

#include "diag_l7_d2.h"
#include "diag_l7_kwp71.h"
#include "scantool.h"
#include "scantool_cli.h"

#include "scantool_850/dtc.h"
#include "scantool_850/ecu.h"

static bool have_read_dtcs = false;
static struct diag_msg *ecu_id = NULL;

static bool live_display_running = false;
static int live_data_lines;

static enum cli_retval cmd_850_help(int argc, char **argv);
static enum cli_retval cmd_850_connect(int argc, char **argv);
static enum cli_retval cmd_850_disconnect(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_ping(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_sendreq(int argc, char **argv);
static enum cli_retval cmd_850_peek(int argc, char **argv);
static enum cli_retval cmd_850_dumpram(int argc, char **argv);
static enum cli_retval cmd_850_read(int argc, char **argv);
static enum cli_retval cmd_850_readnv(int argc, char **argv);
static enum cli_retval cmd_850_adc(int argc, char **argv);
static enum cli_retval cmd_850_id(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_dtc(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_cleardtc(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_freeze(int argc, char **argv);
static enum cli_retval cmd_850_resetsrl(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_scan_all(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_test(int argc, char **argv);

const struct cmd_tbl_entry v850_cmd_table[] = {
	{ "help", "help [command]", "Gives help for a command",
	  cmd_850_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
	  cmd_850_help, 0, NULL},

	{ "connect", "connect <ecuname>", "Connect to ECU. Use '850 connect ?' to show ECU names.",
	  cmd_850_connect, 0, NULL},
	{ "disconnect", "disconnect", "Disconnect from ECU",
	  cmd_850_disconnect, 0, NULL},
	{ "scan-all", "scan-all", "Try connecting to all possible ECUs, print identification and DTCs",
	  cmd_850_scan_all, 0, NULL},
	{ "sendreq", "sendreq <byte0 [byte1 ...]>", "Send raw data to the ECU and print response",
	  cmd_850_sendreq, 0, NULL},
	{ "ping", "ping", "Verify communication with the ECU", cmd_850_ping,
	  0, NULL},
	{ "peek", "peek <addr1>[w|l][.addr2] [addr2 ...] [live|stream]", "Display contents of RAM, once or continuously",
	  cmd_850_peek, 0, NULL},
	{ "dumpram", "dumpram <filename> [fast]", "Dump entire RAM contents to file (Warning: takes 20+ minutes)",
	  cmd_850_dumpram, 0, NULL},
	{ "read", "read <id1>|*<addr1> [id2 ...] [live|stream]", "Display live data, once or continuously",
	  cmd_850_read, 0, NULL},
	{ "adc", "adc id1 [id2 ...]", "Display ADC readings, once or continuously",
	  cmd_850_adc, 0, NULL},
	{ "readnv", "readnv id1 [id2 ...]", "Display non-volatile data",
	  cmd_850_readnv, 0, NULL},
	{ "id", "id", "Display ECU identification",
	  cmd_850_id, 0, NULL},
	{ "dtc", "dtc", "Retrieve DTCs",
	  cmd_850_dtc, 0, NULL},
	{ "cleardtc", "cleardtc", "Clear DTCs from ECU",
	  cmd_850_cleardtc, 0, NULL},
	{ "freeze", "freeze dtc1|all [dtc2 ...]", "Display freeze frame(s)",
	  cmd_850_freeze, 0, NULL},
	{ "resetsrl", "resetsrl", "Reset the Service Reminder Light",
	  cmd_850_resetsrl, 0, NULL},
	{ "test", "test <testname>", "Test vehicle components",
	  cmd_850_test, 0, NULL},

	CLI_TBL_BUILTINS,
	CLI_TBL_END
};

static enum cli_retval cmd_850_help(int argc, char **argv) {
	return cli_help_basic(argc, argv, v850_cmd_table);
}

/*
 * Wrapper around printf. When live data display is running, increments the
 * line count and clears old text remaining on the line we just printed.
 * Appends a newline to the output.
 */
static int printf_livedata(const char *format, ...) {
	va_list ap;
	int rv;

	va_start(ap, format);
	rv = vprintf(format, ap);
	va_end(ap);

	if (live_display_running) {
		live_data_lines++;
		diag_os_clrtoeol();
	}

	putchar('\n');
	return rv;
}

/*
 * Capitalize the first letter of the supplied string.
 * Returns a static buffer that will be reused on the next call.
 */
static char *capitalize(const char *in) {
	static char buf[80];

	strncpy(buf, in, sizeof(buf));
	buf[sizeof(buf)-1] = '\0';

	if (isalpha(buf[0]) && islower(buf[0])) {
		buf[0] = toupper(buf[0]);
	}
	return buf;
}

/*
 * Look up an ECU by name.
 */
static const struct ecu_info *ecu_info_by_name(const char *name) {
	const struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (strcasecmp(name, ecu->name) == 0) {
			return ecu;
		}
	}

	return NULL;
}

/*
 * Get an ECU's address by name.
 */
static int ecu_addr_by_name(const char *name) {
	const struct ecu_info *ecu;
	unsigned long int i;
	char *p;

	if (isdigit(name[0])) {
		i = strtoul(name, &p, 0);
		if (*p != '\0') {
			return -1;
		}
		if (i > 0x7f) {
			return -1;
		}
		return i;
	}

	ecu = ecu_info_by_name(name);
	if (ecu == NULL) {
		return -1;
	}
	return ecu->addr;
}

/*
 * Get an ECU's description by address.
 */
static const char *ecu_desc_by_addr(uint8_t addr) {
	const struct ecu_info *ecu;
	static char buf[7];

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (addr == ecu->addr) {
			return ecu->desc;
		}
	}

	sprintf(buf, "ECU %02X", addr);
	return buf;
}

/*
 * Get the description of the currently connected ECU.
 */
static const char *current_ecu_desc(void) {
	uint8_t addr;

	if (global_state < STATE_CONNECTED) {
		return "???";
	}

	addr = global_l2_conn->diag_l2_destaddr;

	if (addr > 0x7f) {
		return "???";
	}

	return ecu_desc_by_addr(addr);
}

/*
 * Get the printable designation (EFI-xxx, AT-xxx, etc) for a DTC by its raw
 * byte value. Optionally, also get a description of the DTC.
 * Returns a static buffer that will be reused on the next call.
 */
static char *dtc_printable_by_raw(uint8_t addr, uint8_t raw, const char **desc) {
#define PRINTABLE_LEN   8       //including 0-termination
	static char printable[PRINTABLE_LEN];
	static char *empty="";
	const struct ecu_info *ecu_entry;
	const struct dtc_table_entry *dtc_entry;
	const struct ecu_dtc_table_map_entry *ecu_dtc_entry;
	const char *prefix;
	uint16_t suffix;

	prefix = "???";
	for (ecu_entry = ecu_list; ecu_entry->name != NULL; ecu_entry++) {
		if (addr == ecu_entry->addr) {
			prefix = ecu_entry->dtc_prefix;
			break;
		}
	}

	for (ecu_dtc_entry = ecu_dtc_map; ecu_dtc_entry->ecu_addr != 0; ecu_dtc_entry++) {
		if (ecu_dtc_entry->ecu_addr == addr) {
			break;
		}
	}
	if (ecu_dtc_entry->ecu_addr != 0) {
		for (dtc_entry = ecu_dtc_entry->dtc_table; dtc_entry->dtc_suffix != 0; dtc_entry++) {
			if (dtc_entry->raw_value == raw) {
				suffix = dtc_entry->dtc_suffix;
				if (desc != NULL) {
					*desc = dtc_entry->desc;
				}
				if (suffix > 999) {
					suffix = 999;
				}
				snprintf(printable, PRINTABLE_LEN, "%s-%03d", prefix, suffix);
				return printable;
			}
		}
	}

	if (desc != NULL) {
		*desc = empty;
	}
	snprintf(printable, PRINTABLE_LEN, "%s-???", prefix);
	return printable;
}

/*
 * Get the DTC prefix for the currently connected ECU.
 */
static const char *current_dtc_prefix(void) {
	const struct ecu_info *ecu;

	if (global_state < STATE_CONNECTED) {
		return "???";
	}

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (global_l2_conn->diag_l2_destaddr == ecu->addr) {
			return ecu->dtc_prefix;
		}
	}

	return "???";
}

/*
 * Get a DTC byte value by the printable designation. Returns 0xffff on
 * failure.
 */
static uint16_t dtc_raw_by_printable(const char *printable) {
	char prefix[8];
	uint16_t suffix;
	char *p, *q, *r;
	const struct dtc_table_entry *dtc_entry;
	const struct ecu_dtc_table_map_entry *ecu_dtc_entry;
	uint8_t ecu_addr;

	/* extract prefix and suffix from string */
	if (strlen(printable) > sizeof(prefix) - 1) {
		return 0xffff; /* implausably long string */
	}
	strcpy(prefix, printable);
	p = prefix;
	while (isalpha(*p)) {
		p++;
	}
	q = p;
	if (*q == '-') {
		q++;
	}
	suffix = strtoul(q, &r, 10);
	if (*q == '\0' || *r != '\0') {
		return 0xffff; /* no valid numeric suffix */
	}
	*p = '\0';

	/* check prefix */
	if (strcasecmp(prefix, current_dtc_prefix()) != 0) {
		return 0xffff; /* doesn't match connected ecu prefix */
	}

	/* find suffix */
	ecu_addr = global_l2_conn->diag_l2_destaddr;
	for (ecu_dtc_entry = ecu_dtc_map; ecu_dtc_entry->ecu_addr != 0; ecu_dtc_entry++) {
		if (ecu_dtc_entry->ecu_addr == ecu_addr) {
			break;
		}
	}
	if (ecu_dtc_entry->ecu_addr == 0) {
		// ECU not found
		return 0xffff;
	}
	for (dtc_entry = ecu_dtc_entry->dtc_table; dtc_entry->dtc_suffix != 0; dtc_entry++) {
		if (dtc_entry->dtc_suffix == suffix) {
			return dtc_entry->raw_value;
		}
	}
	return 0xffff; /* suffix not found */
}

/*
 * Print a list of known ECUs. Not all ECUs in this list are necessarily
 * present in the vehicle.
 */
static void print_ecu_list(void) {
	const struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		printf(" %s\t%s\n", ecu->name, capitalize(ecu->desc));
	}
}

enum connection_status {
	NOT_CONNECTED,          /* Not connected */
	CONNECTED_D2,           /* Connected with D2 over K-line */
	CONNECTED_KWP71,        /* Connected with KWP71 */
	CONNECTED_EITHER,       /* Connected with either D2 or KWP71 */
	CONNECTED_OTHER         /* Connected with non-Volvo protocol */
};

/*
 * Indicates whether we're currently connected.
 */
static enum connection_status get_connection_status(void) {
	if (global_state < STATE_CONNECTED) {
		return NOT_CONNECTED;
	}
	if (global_l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_D2) {
		return CONNECTED_D2;
	}
	if (global_l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_VAG) {
		return CONNECTED_KWP71;
	}
	return CONNECTED_OTHER;
}

/*
 * Check whether the number of arguments to a command is between the specified
 * minimum and maximum. If not, print a message and return false.
 */
static bool valid_arg_count(int min, int argc, int max) {
	if (argc < min) {
		printf("Too few arguments\n");
		return false;
	}

	if (argc > max) {
		printf("Too many arguments\n");
		return false;
	}

	return true;
}

/*
 * Check whether the connection status matches the required connection status
 * for this command. If not, print a message and return false.
 */
static bool valid_connection_status(unsigned int want) {
	if (want == CONNECTED_EITHER) {
		if (get_connection_status() == CONNECTED_D2 ||
		    get_connection_status() == CONNECTED_KWP71) {
			return true;
		}
	} else if (get_connection_status() == want) {
		return true;
	}

	switch (get_connection_status()) {
	case NOT_CONNECTED:
		printf("Not connected.\n");
		return false;
	case CONNECTED_OTHER:
		if (want == NOT_CONNECTED) {
			printf("Already connected with non-Volvo protocol. Please use 'diag disconnect'.\n");
		} else {
			printf("Connected with non-Volvo protocol.\n");
		}
		return false;
	case CONNECTED_D2:
	case CONNECTED_KWP71:
		if (want == NOT_CONNECTED) {
			printf("Already connected to %s. Please disconnect first.\n", current_ecu_desc());
		} else {
			printf("This function is not available with this protocol.\n");
		}
		return false;
	default:
		printf("Unexpected connection state!\n");
		return false;
	}
}

/*
 * Send 3 pings with a delay between them to get the ELM used to the ECU's
 * response time.
 */
static void adaptive_timing_workaround(void) {
	int i;

	for (i=0; i<3; i++) {
		(void)diag_l7_d2_ping(global_l2_conn);
		diag_os_millisleep(200);
	}
}

/*
 * Callback to store the ID block upon establishing a KWP71 connection
 */
static void ecu_id_callback(void *handle, struct diag_msg *in) {
	struct diag_msg **out = (struct diag_msg **)handle;
	*out = diag_dupmsg(in);
}

/*
 * Connect to an ECU by name or address.
 */
static enum cli_retval cmd_850_connect(int argc, char **argv) {
	int addr;
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l2_data l2data;

	if (!valid_arg_count(2, argc, 2)) {
		return CMD_USAGE;
	}

	if (strcmp(argv[1], "?") == 0) {
		printf("Known ECUs are:\n");
		print_ecu_list();
		printf("Can also specify target by numeric address.\n");
		return CMD_USAGE;
	}

	if (!valid_connection_status(NOT_CONNECTED)) {
		return CMD_OK;
	}

	addr = ecu_addr_by_name(argv[1]);
	if (addr < 0) {
		printf("Unknown ECU '%s'\n", argv[1]);
		return CMD_OK;
	}

	dl0d = global_dl0d;

	if (dl0d == NULL) {
		printf("No global L0. Please select + configure L0 first\n");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (addr == 0x10) {
		global_cfg.speed = 9600;
		global_cfg.tgt = addr;
		global_cfg.L1proto = DIAG_L1_ISO9141;
		global_cfg.L2proto = DIAG_L2_PROT_VAG;
		global_cfg.initmode = DIAG_L2_TYPE_SLOWINIT;
	} else {
		global_cfg.speed = 10400;
		global_cfg.src = 0x13;
		global_cfg.tgt = addr;
		global_cfg.L1proto = DIAG_L1_ISO9141;
		global_cfg.L2proto = DIAG_L2_PROT_D2;
		global_cfg.initmode = DIAG_L2_TYPE_SLOWINIT;
	}

	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "cmd_850_connect: diag_l2_open failed\n");
		return diag_ifwderr(rv);
	}

	global_l2_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
	                                             global_cfg.initmode & DIAG_L2_TYPE_INITMASK, global_cfg.speed,
	                                             global_cfg.tgt, global_cfg.src);
	if (global_l2_conn == NULL) {
		rv = diag_geterr();
		diag_l2_close(dl0d);
		return diag_iseterr(rv);
	}

	if (global_cfg.L2proto == DIAG_L2_PROT_VAG) {
		(void)diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_GET_L2_DATA, (void *)&l2data);
		if (l2data.kb1!=0xab || l2data.kb2!=0x02) {
			fprintf(stderr, FLFMT "_connect : wrong keybytes %02X%02X, expecting AB02\n", FL, l2data.kb1, l2data.kb2);
			diag_l2_StopCommunications(global_l2_conn);
			diag_l2_close(dl0d);
			global_l2_conn = NULL;
			global_state = STATE_IDLE;
			return diag_iseterr(DIAG_ERR_WRONGKB);
		}
	}

	global_state = STATE_CONNECTED;
	printf("Connected to %s.\n", ecu_desc_by_addr(addr));
	have_read_dtcs = false;

	if (get_connection_status() == CONNECTED_D2) {
		adaptive_timing_workaround();
	} else {
		printf("Warning: KWP71 communication is not entirely reliable yet.\n");
		/*
		 * M4.4 doesn't accept ReadECUIdentification request, so save
		 * the identification block it sends at initial connection.
		 */
		if (ecu_id != NULL) {
			diag_freemsg(ecu_id);
		}
		ecu_id = NULL;
		rv = diag_l2_recv(global_l2_conn, 300, ecu_id_callback, &ecu_id);
		if (rv < 0) {
			return diag_ifwderr(rv);
		}
		if (ecu_id == NULL) {
			return diag_iseterr(DIAG_ERR_NOMEM);
		}
	}

	return CMD_OK;
}

/*
 * Close the current connection.
 */
static enum cli_retval cmd_850_disconnect(int argc, UNUSED(char **argv)) {
	const char *desc;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

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
static enum cli_retval cmd_850_sendreq(int argc, char **argv) {
	uint8_t data[MAXRBUF] = {0};
	unsigned int len;
	unsigned int i;
	int rv;

	if (!valid_arg_count(2, argc, sizeof(data) + 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	len = argc - 1;
	for (i = 0; i < len; i++) {
		data[i] = (uint8_t) htoi(argv[i+1]);
	}

	rv = l2_do_send( global_l2_conn, data, len,
	                 (void *)&_RQST_HANDLE_DECODE);

	if (rv == DIAG_ERR_TIMEOUT) {
		printf("No data received\n");
	} else if (rv != 0) {
		printf("sendreq: failed error %d\n", rv);
	}

	return CMD_OK;
}

/*
 * Verify communication with the ECU.
 */
static enum cli_retval cmd_850_ping(int argc, UNUSED(char **argv)) {
	int rv;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_ping(global_l2_conn);
	} else {
		rv = diag_l7_kwp71_ping(global_l2_conn);
	}

	if (rv == 0) {
		printf("Pong!\n");
	} else {
		printf("Ping failed.\n");
	}

	return CMD_OK;
}

#define CLAMPED_LOOKUP(table,index) table[MIN(ARRAY_SIZE(table)-1,(unsigned)(index))]

/*
 * If we know how to interpret a live data value, print out the description and
 * scaled value.
 */
static void interpret_value(enum l7_namespace ns, uint16_t addr, UNUSED(int len), uint8_t *buf) {
	static const char *mode_selector_positions[]={"Open","S","E","W","Unknown"};
	static const char *driving_modes[]={"Economy","Sport","Winter","Unknown"};
	static const char *warmup_states[]={"in progress or engine off","completed","not possible","status unknown"};
	float volts;
	int16_t deg_c;
	uint8_t ecu = global_l2_conn->diag_l2_destaddr;

	if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0200) {
		printf_livedata("Engine Coolant Temperature: %dC (%dF)", buf[1]-80, (buf[1]-80)*9/5+32);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0300) {
		/*ECU pin A27, MCU P7.1 input, divider ratio 8250/29750, 5Vref*/
		printf_livedata("Battery voltage: %.1f V", (float)buf[0]*29750/8250*5/255);
	} else if (ns==NS_MEMORY && ecu==0x10 && addr==0x36) {
		printf_livedata("Battery voltage: %.1f V", (float)buf[0]*29750/8250*5/255);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0A00) {
		printf_livedata("Warm-up %s", CLAMPED_LOOKUP(warmup_states, (buf[0]>>2)&3));
		printf_livedata("MIL %srequested by TCM", (buf[0]&0x10) ? "" : "not ");
		/* Low 2 bits supposedly indicate drive cycle and trip
		   complete, but don't make sense - can get set without the
		   car ever moving */
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1000) {
		/* ECU pin A4, MCU P7.4 input, divider ratio 8250/9460 */
		printf_livedata("MAF sensor signal: %.2f V", (float)buf[0]*9460/8250*5/255);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1800) {
		printf_livedata("Short term fuel trim: %+.1f%%", (float)buf[0]*100/128-100);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1900) {
		/* possibly in units of 0.004 milliseconds (injection time) */
		printf_livedata("Long term fuel trim, additive (unscaled): %+d", (signed int)buf[0]-128);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1A00) {
		printf_livedata("Long term fuel trim, multiplicative: %+.1f%%", (float)buf[0]*100/128-100);
	} else if (ns==NS_LIVEDATA && ecu==0x6e && addr==0x0500) {
		printf_livedata("Mode selector: MS1 %s, MS2 %s, switch position %s", (buf[0]&1) ? "low" : "high", (buf[0]&2) ? "low" : "high", CLAMPED_LOOKUP(mode_selector_positions, buf[0]));
		printf_livedata("Driving mode: %s", CLAMPED_LOOKUP(driving_modes, buf[1]));
	} else if (ns==NS_LIVEDATA && ecu==0x6e && addr==0x0C00) {
		/* Full scale should be 1023, although highest value seen in
		   bench testing was 1020 */
		volts = ((float)buf[0]*256+buf[1])*5/1023;
		printf_livedata("ATF temperature sensor voltage: %.2f V", volts);
		/* Avoid divide by zero below */
		if (5.0f-volts == 0.0f) {
			volts = 4.999;
		}
		/* Input has 1k to +5V, sensor acts as a potential divider */
		printf_livedata("ATF temperature sensor resistance: %u ohms", (unsigned)((1000.0f*volts)/(5.0f-volts)));
		/* Offs 11 (!) agrees with T vs R chart in Volvo Green Book */
		deg_c = ((int16_t)buf[2]*256)+buf[3]-11;
		printf_livedata("ATF temperature: %dC (%dF)", deg_c, deg_c*9/5+32);
	}
}

/*
 * Try to interpret all the live data values in the buffer.
 */
static void interpret_block(enum l7_namespace ns, uint16_t addr, int len, uint8_t *buf) {
	int i;

	if (ns != NS_MEMORY) {
		addr <<= 8;
	}

	for (i=0; i<len; i++) {
		interpret_value(ns, addr+i, len-i, buf+i);
	}
}

/*
 * Print one line of a hex dump, with an address followed by one or more
 * values.
 */
static int print_hexdump_line(FILE *f, uint16_t addr, int addr_chars, const uint8_t *buf, uint16_t len) {
	if (fprintf(f, "%0*X:", addr_chars, addr) < 0) {
		return 1;
	}
	while (len--) {
		if (fprintf(f, " %02X", *buf++) < 0) {
			return 1;
		}
	}
	if (live_display_running) {
		diag_os_clrtoeol();
	}
	if (fputc('\n', f) == EOF) {
		return 1;
	}
	return 0;
}

struct read_or_peek_item {
	uint16_t start;         /* starting address or identifier */
	uint16_t end;           /* ending address - for peeks only */
	enum l7_namespace ns;
};

/*
 * Parse an address argument on a peek command line.
 */
static int parse_peek_arg(const char *arg, struct read_or_peek_item *item) {
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
static int parse_read_arg(const char *arg, struct read_or_peek_item *item) {
	char *p;

	if (arg[0] == '*') {
		if (arg[1] == '\0') {
			printf("Invalid identifier '%s'\n", arg);
			return 1;
		}
		return parse_peek_arg(arg+1, item);
	}

	item->ns = NS_LIVEDATA;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on an adc command line.
 */
static int parse_adc_arg(const char *arg, struct read_or_peek_item *item) {
	char *p;

	item->ns = NS_ADC;
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
static int parse_readnv_arg(const char *arg, struct read_or_peek_item *item) {
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
 * Parse an identifier argument on a freeze command line.
 */
static int parse_freeze_arg(const char *arg, struct read_or_peek_item *item) {
	char *p;

	item->ns = NS_FREEZE;
	if (isalpha(arg[0])) {
		item->start = dtc_raw_by_printable(arg);
		if (item->start == 0xffff) {
			printf("Invalid identifier '%s'\n", arg);
			return 1;
		}
		return 0;
	}
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		if (isdigit(arg[0]) && arg[0] != '0' && *p == '\0') {
			printf("Did you mean %s-%s?\n",
			       current_dtc_prefix(), arg);
		}
		return 1;
	}
	if (isdigit(arg[0]) && arg[0]!='0') {
		if (item->start < 100) {
			printf("Warning: retrieving freeze frame by raw identifier %d (=%02X).\nDid you mean 0x%s?\n", item->start, item->start, arg);
		} else {
			printf("Warning: retrieving freeze frame by raw identifier %d (=%02X).\nDid you mean %s-%s?\n", item->start, item->start, current_dtc_prefix(), arg);
		}
	}
	return 0;
}

/*
 * Execute a read, peek or readnv command.
 */
static enum cli_retval read_family(int argc, char **argv, enum l7_namespace ns) {
	int count;
	int i, rv;
	bool continuous;
	struct read_or_peek_item *items;
	uint8_t buf[20];
	uint16_t addr, len;
	int gotbytes;

	if (!valid_arg_count(2, argc, 999)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	continuous = false;
	count = argc - 1;

	if (ns!=NS_NV && ns!=NS_FREEZE && strcasecmp(argv[argc-1], "stream")==0) {
		continuous = true;
		count--;
		if (count < 1) {
			return CMD_USAGE;
		}
	}

	if (ns!=NS_NV && ns!=NS_FREEZE && strcasecmp(argv[argc-1], "live")==0) {
		if (continuous) {
			return CMD_USAGE;
		}
		continuous = true;
		live_display_running = true;
		count--;
		if (count < 1) {
			return CMD_USAGE;
		}
	}

	rv = diag_calloc(&items, count);
	if (rv) {
		live_display_running = false;
		return diag_ifwderr(rv);
	}

	for (i=0; i<count; i++) {
		switch (ns) {
		case NS_MEMORY:
			if (parse_peek_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_LIVEDATA:
			if (parse_read_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_ADC:
			if (parse_adc_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_NV:
			if (parse_readnv_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_FREEZE:
			if (parse_freeze_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		default:
			fprintf(stderr, FLFMT "impossible ns value\n", FL);
			goto done;
		}
	}

	diag_os_ipending();
	while (1) {
		live_data_lines = 0;
		for (i=0; i<count; i++) {
			if (items[i].ns != NS_MEMORY) {
				addr = items[i].start;
				if (get_connection_status() == CONNECTED_D2) {
					gotbytes = diag_l7_d2_read(global_l2_conn, items[i].ns, addr, sizeof(buf), buf);
				} else {
					gotbytes = diag_l7_kwp71_read(global_l2_conn, items[i].ns, addr, sizeof(buf), buf);
				}
				if (gotbytes < 0) {
					printf("Error reading %02X\n", addr);
					goto done;
				}
				if (items[i].ns == NS_FREEZE) {
					printf("%s  ",
					       dtc_printable_by_raw(
						       global_l2_conn
						       ->diag_l2_destaddr,
						       addr, NULL));
				}
				if (gotbytes == 0) {
					printf_livedata("%02X: no data", addr);
				} else if ((unsigned int)gotbytes > sizeof(buf)) {
					print_hexdump_line(stdout, addr, 2, buf, sizeof(buf));
					live_data_lines++;
					printf_livedata(" (%d bytes received, only first %zu shown)", gotbytes, sizeof(buf));
					interpret_block(items[i].ns, addr, sizeof(buf), buf);
				} else {
					print_hexdump_line(stdout, addr, 2, buf, gotbytes);
					live_data_lines++;
					interpret_block(items[i].ns, addr, gotbytes, buf);
				}
			} else {
				addr = items[i].start;
				len = (items[i].end - items[i].start) + 1;
				while (len > 0) {
					if (get_connection_status() == CONNECTED_D2) {
						gotbytes = diag_l7_d2_read(global_l2_conn, NS_MEMORY, addr, (len<8) ? len : 8, buf);
					} else {
						gotbytes = diag_l7_kwp71_read(global_l2_conn, NS_MEMORY, addr, (len<8) ? len : 8, buf);
					}
					if (gotbytes == ((len<8) ? len : 8)) {
						print_hexdump_line(stdout, addr, 4, buf, (len<8) ? len : 8);
						live_data_lines++;
						interpret_block(NS_MEMORY, addr, (len<8) ? len : 8, buf);
					} else {
						printf("Error reading %s%04X\n", (ns==NS_LIVEDATA) ? "*" : "", addr);
						goto done;
					}
					len -= (len<8) ? len : 8;
					addr += 8;
				}
			}
		}
		if (!continuous || diag_os_ipending()) {
			break;
		}
		if (live_display_running) {
			diag_os_cursor_up(live_data_lines);
		}
	}

done:
	live_display_running = false;
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
 * The word "live" can be added at the end to continuously read the
 * requested addresses and update the display until interrupted, or "stream"
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_peek(int argc, char **argv) {
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
 * The word "live" can be added at the end to continuously read the
 * requested addresses and update the display until interrupted, or "stream"
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_read(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_LIVEDATA);
}

/*
 * Read and display one or more ADC readings.
 *
 * Takes a list of one-byte channel identifiers.
 *
 * The word "live" can be added at the end to continuously read the
 * requested addresses and update the display until interrupted, or "stream"
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_adc(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_KWP71)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_ADC);
}


/*
 * Read and display one or more non-volatile parameters.
 *
 * Takes a list of one-byte identifier values.
 */
static enum cli_retval cmd_850_readnv(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_NV);
}

/*
 * Read and display freeze frames for all stored DTCs.
 */
static enum cli_retval cmd_850_freeze_all(void) {
	uint8_t dtcs[12];
	int count;
	char *argbuf;
	char **argvout;
	char *p;
	int rv;
	int i;

	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	rv = diag_l7_d2_dtclist(global_l2_conn, sizeof(dtcs), dtcs);
	if (rv < 0) {
		printf("Couldn't retrieve DTCs.\n");
		return CMD_OK;
	}

	if (rv == 0) {
		printf("No stored DTCs.\n");
		return CMD_OK;
	}

	count = rv;

	rv = diag_calloc(&argbuf, 5);
	if (rv) {
		return diag_ifwderr(rv);
	}

	rv = diag_calloc(&argvout, count+1);
	if (rv) {
		return diag_ifwderr(rv);
	}

	p = argbuf;
	for (i=0; i<count; i++) {
		sprintf(p, "0x%x", dtcs[i]);
		argvout[i+1] = p;
		p += 5;
	}

	rv = read_family(count+1, argvout, NS_FREEZE);

	free(argvout);
	free(argbuf);
	return rv;
}

/*
 * Read and display one or more freeze frames.
 *
 * Takes a list of DTCs, or the option "all" to retrieve freeze frames for all
 * stored DTCs. Each DTC can be specified either as a raw byte value or by its
 * EFI-xxx, AT-xxx, etc designation.
 */
static enum cli_retval cmd_850_freeze(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	if (argc==2 && strcasecmp(argv[1], "all")==0) {
		return cmd_850_freeze_all();
	}
	return read_family(argc, argv, NS_FREEZE);
}

/*
 * Query the ECU for identification and print the result.
 */
static enum cli_retval cmd_850_id_d2(void) {
	uint8_t buf[15];
	int rv;
	int i;

	rv = diag_l7_d2_read(global_l2_conn, NS_NV, 0xf0, sizeof(buf), buf);
	if (rv < 0) {
		printf("Couldn't read identification.\n");
		return CMD_OK;
	}
	if (rv != sizeof(buf)) {
		printf("Identification response was %d bytes, expected %zu\n", rv, sizeof(buf));
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

	if (global_l2_conn->diag_l2_destaddr == 0x7a) {
		rv = diag_l7_d2_read(global_l2_conn, NS_NV, 1, sizeof(buf), buf);
		if (rv < 0) {
			return CMD_OK;
		}
		if (rv != 10) {
			printf("Identification response was %d bytes, expected %d\n", rv, 10);
			return CMD_OK;
		}
		for (i=0; i<10; i++) {
			if (!isdigit(buf[i])) {
				printf("Unexpected characters in identification block\n");
				return CMD_OK;
			}
		}
		printf("Order number: %c %.3s %.3s %.3s\n",buf[0], buf+1, buf+4, buf+7);
	}

	return CMD_OK;
}

/*
 * Print the ECU identification we received upon initial connection.
 */
static enum cli_retval cmd_850_id_kwp71(void) {
	int i;
	struct diag_msg *msg;

	if (ecu_id == NULL) {
		printf("No stored ECU identification!\n");
		return CMD_OK;
	}

	msg = ecu_id;

	if (msg->len != 10) {
		printf("Identification block was %u bytes, expected %d\n", msg->len, 10);
		return CMD_OK;
	}

	for (i=0; i<10; i++) {
		if (!isdigit(msg->data[i])) {
			printf("Unexpected characters in identification block\n");
			return CMD_OK;
		}
	}

	printf("Order number: %c %.3s %.3s %.3s\n",msg->data[0], msg->data+1, msg->data+4, msg->data+7);

	msg = msg->next;
	if (msg == NULL) {
		return CMD_OK;
	}
	/* Second block seems to be meaningless, don't print it. */
#if 0
	print_hexdump_line(stdout, msg->type, 2, msg->data, msg->len);
#endif
	msg = msg->next;
	if (msg == NULL) {
		return CMD_OK;
	}

	if (msg->len != 10) {
		printf("Identification block was %u bytes, expected %d\n", msg->len, 10);
		return CMD_OK;
	}

	for (i=0; i<7; i++) {
		if (!isdigit(msg->data[i])) {
			printf("Unexpected characters in identification block\n");
			return CMD_OK;
		}
	}
	printf("Hardware ID: P0%.7s\n", msg->data);

	/* There's a fourth block but it seems to be meaningless. */

	return CMD_OK;
}

/*
 * Display ECU identification.
 */
static enum cli_retval cmd_850_id(int argc, UNUSED(char **argv)) {
	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		return cmd_850_id_d2();
	}
	return cmd_850_id_kwp71();
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
static enum cli_retval cmd_850_dumpram(int argc, char **argv) {
	uint16_t addr;
	FILE *f;
	bool happy;
	uint8_t buf[8];
	bool fast;

	if (argc == 2) {
		fast = false;
	} else if (argc == 3 && strcasecmp(argv[2], "fast") == 0) {
		fast = true;
	} else {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	f = fopen(argv[1], "w");
	if (f == NULL) {
		perror("Can't open file");
		return CMD_OK;
	}

	printf("Dumping RAM to %s...\n", argv[1]);

	addr = 0;
	while (1) {
		if (diag_l7_d2_read(global_l2_conn, NS_MEMORY, addr, 8, buf) == 8) {
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
			printf("\r%04X %s", addr, happy ? ":)" : ":/");
			fflush(stdout);
		}
		if (addr == 0xfff8) {
			break;
		}
		addr += 8;

		if (fast && !happy && addr < 0xf000) {
			addr = 0xf000;
		}
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
static enum cli_retval cmd_850_dtc(int argc, UNUSED(char **argv)) {
	uint8_t buf[12];
	int rv;
	int i;
	int span;
	char *code;
	const char *desc;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_dtclist(global_l2_conn, sizeof(buf), buf);
		span = 1;
	} else {
		rv = diag_l7_kwp71_dtclist(global_l2_conn, sizeof(buf), buf);
		span = 5;
	}

	if (rv < 0) {
		printf("Couldn't retrieve DTCs.\n");
		return CMD_OK;
	}
	have_read_dtcs = true;

	if (rv == 0) {
		printf("No stored DTCs.\n");
		return CMD_OK;
	}

	printf("Stored DTCs:\n");
	for (i=0; i<rv; i+=span) {
		code = dtc_printable_by_raw(global_l2_conn->diag_l2_destaddr, buf[i], &desc);
		printf("%s (%02X) %s\n", code, buf[i], desc);
	}

	return CMD_OK;
}

/*
 * Clear stored DTCs.
 */
static enum cli_retval cmd_850_cleardtc(int argc, UNUSED(char **argv)) {
	char *input;
	int rv;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	input = cli_basic_get_input("Are you sure you wish to clear the Diagnostic Trouble Codes (y/n) ? ", stdin);
	if (!input) {
		return CMD_OK;
	}

	if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
		printf("Not done\n");
		goto done;
	}

	if (!have_read_dtcs) {
		free(input);
		input = cli_basic_get_input("You haven't read the DTCs yet. Are you sure you wish to clear them (y/n) ? ", stdin);
		if (!input) {
			return CMD_OK;
		}
		if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
			printf("Not done\n");
			goto done;
		}
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_cleardtc(global_l2_conn);
	} else {
		rv = diag_l7_kwp71_cleardtc(global_l2_conn);
	}

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

/*
 * Reset the Service Reminder Light.
 */
static enum cli_retval cmd_850_resetsrl(int argc, UNUSED(char **argv)) {
	char *input;
	char *argvout[] = {"connect", "combi"};
	int rv;
	bool old_car;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	input = cli_basic_get_input("Are you sure you wish to reset the Service Reminder Light (y/n) ? ", stdin);
	if (!input) {
		return CMD_OK;
	}

	if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
		printf("Not done\n");
		goto done;
	}

	/* If talking to wrong ECU, disconnect first */
	if (get_connection_status() != NOT_CONNECTED && global_l2_conn->diag_l2_destaddr!=0x51) {
		printf("Disconnecting from %s first.\n", current_ecu_desc());
		cmd_850_disconnect(1, NULL);
	}

	/* If not connected to combi, connect */
	if (get_connection_status() == NOT_CONNECTED) {
		if (cmd_850_connect(2, argvout) != CMD_OK) {
			printf("Couldn't connect to combined instrument panel.\n");
			goto done;
		}
	}

	/* '96/'97 must be unlocked first, but '98 rejects unlock command */
	old_car = (diag_l7_d2_io_control(global_l2_conn, 0x30, 0) == 0);

	rv = diag_l7_d2_run_routine(global_l2_conn, 0x30);

	if (rv == 0) {
		printf("Done\n");
	} else if (rv == DIAG_ERR_TIMEOUT && old_car) {
		/* '96/'97 either don't respond after SRL reset, or respond
		   after a long delay? */
		printf("Probably done\n");
	} else {
		printf("Failed\n");
	}

done:
	free(input);
	return CMD_OK;
}

/*
 * Try to connect to each possible ECU. Print identification and DTCs for each
 * successfully connected ECU.
 *
 * There will always be some unsuccessful connection attempts in a scan-all
 * because at least one ECU in our list will be missing from any given vehicle.
 * For example, MSA 15.7 and Motronic M4.4 will never both be present in the
 * same car.
 */
static enum cli_retval cmd_850_scan_all(int argc, UNUSED(char **argv)) {
	const struct ecu_info *ecu;
	char *argvout[2];
	char buf[4];

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(NOT_CONNECTED)) {
		return CMD_OK;
	}

	printf("Scanning all ECUs.\n");

	argvout[1] = buf;
	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		sprintf(buf, "%d", ecu->addr);
		if (ecu->addr == 0x10) {
			/* Skip Motronic M4.4 old protocol */
		} else if (cmd_850_connect(2, argvout) == CMD_OK) {
			cmd_850_id(1, NULL);
			cmd_850_dtc(1, NULL);
			cmd_850_disconnect(1, NULL);
		} else {
			printf("Couldn't connect to %s.\n", ecu->desc);
		}
	}

	printf("Scan-all done.\n");

	return CMD_OK;
}

/*
 * Test the specified vehicle component.
 */
static enum cli_retval cmd_850_test(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	if (argc==2 && strcasecmp(argv[1], "fan1")==0 && global_l2_conn->diag_l2_destaddr==0x7a) {
		if (diag_l7_d2_io_control(global_l2_conn, 0x0e, 3) == 0) {
			printf("Activating engine cooling fan.\n");
		} else {
			printf("Unable to activate fan.\n");
		}
	} else if (argc==2 && strcasecmp(argv[1], "fan2")==0 && global_l2_conn->diag_l2_destaddr==0x7a) {
		if (diag_l7_d2_io_control(global_l2_conn, 0x1f, 3) == 0) {
			printf("Activating engine cooling fan.\n");
		} else {
			printf("Unable to activate fan.\n");
		}
	} else {
		printf("Usage: test <testname>\n");
		if (global_l2_conn->diag_l2_destaddr == 0x7a) {
			printf("Available tests:\n");
			printf("fan1 - Activate engine cooling fan, half speed (please keep fingers clear)\n");
			printf("fan2 - Activate engine cooling fan, full speed (please keep fingers clear)\n");
		} else {
			printf("No available tests for this ECU.\n");
		}
	}
	return CMD_OK;
}
