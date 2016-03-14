/*
 *  freediag - Vehicle Diagnostic Utility
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
 * Application Interface (AIF) routines
 *
 * NOTE : a lot of the code in here duplicates functionality of some cmd_* functions...
 */

#include <time.h>
#include <ctype.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l2.h"
#include "diag_l3.h"
#include "diag_dtc.h"

#include "scantool.h"
#include "scantool_cli.h"
#include "scantool_aif.h"
#include "freediag_aif.h"
#include "utlist.h"

static void do_aif_command (void) ;
static int debugging = 0 ;

static void toApp (char command)
{
	putc(command, stdout) ;
}

static void OkToApp (void) { toApp(FREEDIAG_AIF_OK_RETURN) ; }
static void BadToApp(void) { toApp(FREEDIAG_AIF_ERROR_RETURN) ; }


/*
	Remove the 'BadToApp()' function call when you get these
	working!
*/

static void aif_watch(void *data) { (void) data; BadToApp() ; }
static void aif_clear_dtc(void *data) { (void) data; BadToApp() ; }
static void aif_ecus(void *data) {(void) data; BadToApp() ; }
static void aif_test(void *data) { (void) data; BadToApp() ; }
static void aif_diag(void *data) { (void) data; BadToApp() ; }
static void aif_vw(void *data) { (void) data; BadToApp() ; }
static void aif_dyno(void *data) { (void) data; BadToApp() ; }


static void aif_monitor (UNUSED(void *data))
{
	if (global_state < STATE_CONNECTED)
	{
		fprintf(stderr, "scantool: Can't monitor - car is not yet connected.\n");
		BadToApp() ;
		return ;
	}
	OkToApp() ;

	/*
	* Now just receive data and send it to the application
	* whenever it requests it.
	*/

	while (1)
	{
		unsigned int i ;
		int rv = do_j1979_getdata(1) ;
		struct diag_l3_conn *d_conn ;
		struct diag_msg *msg ;

		/* New request arrived. */

		if (rv)
		{
			unsigned int j ;

			for (j = 0 ; get_pid(j) != NULL ; j++)
			{
				const struct pid *p = get_pid(j) ;
				ecu_data_t   *ep ;
				char buf[24] ;

				for (i = 0, ep = ecu_info ; i < ecu_count ; i++, ep++)
				{
					if (DATA_VALID(p, ep->mode1_data) ||
					DATA_VALID(p, ep->mode2_data))
					{
						if (DATA_VALID(p, ep->mode1_data))
							p->cust_snprintf(buf, sizeof(buf), global_cfg.units, p, ep->mode1_data, 2);

						printf("%-15.15s ", buf);

						if (DATA_VALID(p, ep->mode2_data))
							p->cust_snprintf(buf, sizeof(buf), global_cfg.units, p, ep->mode2_data, 3);

						printf("%-15.15s\n", buf);
					}
				}
			}
	}

	d_conn = global_l3_conn ;

	rv = l3_do_j1979_rqst(d_conn, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL) ;

	if (rv == DIAG_ERR_TIMEOUT)
	{
	/* Didn't get a response, this is valid if there are no DTCs */
	}
	else if (rv != 0) {
		fprintf(stderr, "Failed to get test results for"
		" continuously monitored systems\n") ;
		BadToApp() ;
	}
	else
	{
		/* Currently monitored DTCs: */

		for (i = 0 ; i < ecu_count ; i++) {
			LL_FOREACH(ecu_info[i].rxmsg, msg) {
				int i, j ;

				for (i = 0, j = 1 ; i < 3 ; i++, j += 2)
				{
					char buf[256];
					uint8_t db[2];

					if ((msg->data[j]==0) && (msg->data[j+1]==0))
						continue ;

					db[0] = msg->data[j];
					db[1] = msg->data[j+1];

					diag_dtc_decode(db, 2, NULL, NULL,
						dtc_proto_j2012, buf, sizeof(buf)) ;
					//what do we do with the decoded DTC ?
					//maybe just print it for now...
					fprintf(stderr, FLFMT "decoded DTC : %s\n", FL, buf);
				}
			}
		}
	}	//if DIAG_ERR_TIMEOUT
	}	//while 1

	OkToApp() ;
}


static void aif_set (void *data)
{
	int sub_command = ((unsigned char *) data)[0] ;

	switch (sub_command) {
		case FREEDIAG_AIF_SET_UNITS :
		{
			int units = ((unsigned char *) data)[1] ;

			if (debugging)
				fprintf(stderr, "Setting units to %d\n", units) ;

			switch (units)
			{
				case FREEDIAG_AIF_SET_UNITS_US     : global_cfg.units = 1 ; break ;
				case FREEDIAG_AIF_SET_UNITS_METRIC : global_cfg.units = 0 ; break ;
				default        					: BadToApp() ; return ;
			}
			break ;
		}
		case FREEDIAG_AIF_SET_PORT :
		{
			int port = ((unsigned char *) data)[1] ;

			if (debugging)
				fprintf(stderr, "Setting port to %d\n", port) ;

			if (port < 0 || port > 9)
			{
				BadToApp() ;
				return ;
			}

			fprintf(stderr, "ERROR - code not complete for CFG rework");
			break ;
		}
		default :
			if (debugging)
				fprintf(stderr, "Illegal 'Set' command: %d\n", sub_command) ;

			BadToApp() ;
			return ;
	}

	OkToApp() ;
}


static void aif_noop (UNUSED(void *data))
{
	OkToApp() ;
}


static void aif_exit (UNUSED(void *data))
{
	OkToApp() ;
	fprintf(stderr, "scantool: Exiting.\n") ;
	set_close();
	exit (0) ;
}


static void aif_disconnect (UNUSED(void *data))
{
	if (global_state < STATE_CONNECTED)
	{
		OkToApp() ;
		return ;
	}

	if (global_state >= STATE_L3ADDED)
	{
		/* Close L3 protocol */
		diag_l3_stop(global_l3_conn);
	}

	diag_l2_StopCommunications(global_l2_conn);
	diag_l2_close(global_dl0d);

	global_l2_conn = NULL;
	global_state = STATE_IDLE;

	OkToApp() ;
}



static void aif_scan (UNUSED(void *data))
{
	if (global_state >= STATE_CONNECTED)
	{
		OkToApp() ;
		return ;
	}

	if (ecu_connect() == 0)
	{
		do_j1979_basics () ; /* Ask basic info from ECU */
		do_j1979_cms	() ; /* Get test results for monitored systems */
		do_j1979_ncms  (0) ; /* And non-continuously monitored tests   */

		OkToApp() ;
	}
	else
	{
		fprintf(stderr, "Connection to ECU failed\n") ;
		fprintf(stderr, "Please check :\n") ;
		fprintf(stderr, "\tAdapter is connected to PC\n") ;
		fprintf(stderr, "\tCable is connected to Vehicle\n") ;
		fprintf(stderr, "\tVehicle is switched on\n") ;
		fprintf(stderr, "\tVehicle is OBDII compliant\n") ;

		BadToApp() ;
	}
}


static void aif_debug (void *data)
{
	debugging = ((char *) data)[0] ;

	OkToApp() ;

	fprintf(stderr, "AIF: Debugging is %sabled\n",
	debugging ? "En" : "Dis") ;
}


typedef void (*aif_func) (void *) ;

struct AIFcommand
{
	const int	   code   ;
	const int	   length ;
	const char	 *name   ;
	const aif_func  func   ;
} ;


const struct AIFcommand aif_commands[] =
{
	{ FREEDIAG_AIF_NO_OP	, 0, "Do Nothing"			, aif_noop	  },
	{ FREEDIAG_AIF_EXIT	 , 0, "Exit ScanTool"		 , aif_exit	  },
	{ FREEDIAG_AIF_MONITOR  , 0, "Monitor"			   , aif_monitor   },
	{ FREEDIAG_AIF_WATCH	, 0, "Watch diagnostic bus"  , aif_watch	 },
	{ FREEDIAG_AIF_CLEAR_DTC, 0, "Clear DTC's from ECU"  , aif_clear_dtc },
	{ FREEDIAG_AIF_ECUS	 , 0, "Show ECU information"  , aif_ecus	  },
	{ FREEDIAG_AIF_SET	  , 2, "Set various options"   , aif_set	   },
	{ FREEDIAG_AIF_TEST	 , 0, "Perform various tests" , aif_test	  },
	{ FREEDIAG_AIF_SCAN	 , 0, "Scan for Connection"   , aif_scan	  },
	{ FREEDIAG_AIF_DIAG	 , 0, "Extended diagnostics"  , aif_diag	  },
	{ FREEDIAG_AIF_VW	   , 0, "VW diagnostic protocol", aif_vw		},
	{ FREEDIAG_AIF_DYNO	 , 0, "Dyno functions"		, aif_dyno	  },
	{ FREEDIAG_AIF_DEBUG	, 1, "Set/Unset debug"	   , aif_debug	 },
	{ FREEDIAG_AIF_DISCONNECT,0, "Disconnect from car"   , aif_disconnect},
	{ 0, 0, NULL, NULL }
} ;


static void do_aif_command (void)
{
	char data_buffer[FREEDIAG_AIF_INPUT_MAX] ;
	int i, j ;

	const struct AIFcommand *command=NULL ;
	int cmd = fgetc(stdin) ;

	if (cmd == -1)
	{
		fprintf (stderr,
		"scantool: Unexpected EOF from Application Interface\n") ;
		BadToApp() ;
		exit (1) ;
	}

	for (i = 0 ; aif_commands[i] . name != NULL ; i++)
	{
		command = & (aif_commands[i]) ;

		if (command->code == cmd)
		{
			if (debugging)
			fprintf(stderr, "CMD: %d %s\n", cmd, command->name) ;

			break ;
		}
	}
	if (command->name == NULL)
	{
		fprintf(stderr,
		"scantool: Application sent AIF an illegal command '%d'\n",
		cmd) ;
		BadToApp() ;
		exit (1) ;
	}

	for (j = 0 ; j < command->length &&
	j < FREEDIAG_AIF_INPUT_MAX &&
	! feof (stdin) ; j++)
		data_buffer[j] = getc(stdin) ;

	command->func(data_buffer) ;

	fflush(stdout) ;
}


void enter_aif (const char *name)
{
	fprintf(stderr, "%s AIF: version %s\n", name, PACKAGE_VERSION) ;
	set_init() ;

	while (1)
	do_aif_command () ;
}


