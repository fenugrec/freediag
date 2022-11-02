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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "libcli.h"


/** Find either a $home/.<progname>.rc or ./<progname>.ini (in order of preference)
 *
 * @return a filename (must be free'd by caller) if either was found, otherwise NULL.
 */
char *find_rcfile(void);


/** start CLI
 *
 * @param inistcript: optional
 */
void scantool_cli(const char *prompt, const char *initscript, const struct cmd_tbl_entry *cmdtable);

void wait_enter(const char *message);
int pressed_enter(void);


/**
 * Decimal/Octal/Hex to integer routine
 * formats:
 * [-]0[0-7] : octal
 * [-]0x[0-9,A-F,a-f] : hex
 * [-]$[0-9,A-F,a-f] : hex
 * [-][0-9] : dec
 * Returns 0 if unable to decode.
 */
int htoi(char *buf);


extern int diag_cli_debug;	/* debug level */
extern FILE		*global_logfp;		/* Monitor log output file pointer */
void log_timestamp(const char *prefix);



/** Global parameters set by user interface **/

/* struct global_cfg contains all global parameters */
extern struct globcfg {
	bool units;	/* English(1) or Metric(0)  display */

	uint8_t	tgt;	/* u8; target address */
	uint8_t	src;	/* u8: source addr / tester ID */
	bool	addrtype;	/* Address type, 1 = functional */
	unsigned int speed;	/* ECU comms speed */

	int	initmode;	/* Type of bus init (ISO9141/14230 only) */
	int	L1proto;	/* L1 (H/W) Protocol type */
	int	L2proto;	/* L2 (S/W) Protocol type; value of ->diag_l2_protocol. */
	int	L2idx;		/* index of that L2 proto in struct l2proto_list[] */

	const char *l0name;	/* L0 interface name to use */
	//struct diag_l0_device *dl0d;	/* L0 device to use */
} global_cfg;


enum globstate {
	//specify numbers because some code checks (for global_state >= X) etc.
	STATE_IDLE=0,		/* Idle */
	STATE_WATCH=1,		/* Watch mode */
	STATE_CONNECTED=2,	/* Connected to ECU */
	STATE_L3ADDED=3,	/* Layer 3 protocol added on Layer 2 */
	STATE_SCANDONE=4,	/* J1978/9 Scan Done, so got J1979 PID list */
};	//only for global_state !
extern enum globstate global_state;

extern struct diag_l0_device *global_dl0d;

extern const char *progname;


/* Sub menus */

extern const struct cmd_tbl_entry set_cmd_table[];
extern int set_init(void);
extern void set_close(void);

extern const struct cmd_tbl_entry debug_cmd_table[];
extern const struct cmd_tbl_entry test_cmd_table[];
extern const struct cmd_tbl_entry diag_cmd_table[];
extern const struct cmd_tbl_entry vag_cmd_table[];
extern const struct cmd_tbl_entry v850_cmd_table[];
extern const struct cmd_tbl_entry dyno_cmd_table[];

#if defined(__cplusplus)
}
#endif
#endif /* _SCANTOOL_CLI_H_ */
