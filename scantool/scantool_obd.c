/* Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * Copyright (c) 2016 fenugrec
 *
 * Licensed under GPLv3
 */

#include <assert.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "scantool.h"
#include "scantool_cli.h"
#include "scantool_obd.h"



//cmd_watch : this creates a diag_l3_conn
static int
cmd_watch(int argc, char **argv) {
	int rv;
	struct diag_l2_conn *d_l2_conn;
	struct diag_l3_conn *d_l3_conn=NULL;
	struct diag_l0_device *dl0d = global_dl0d;
	bool rawmode = 0;
	bool nodecode = 0;
	bool nol3 = 0;

	if (argc > 1) {
		if (strcasecmp(argv[1], "raw") == 0) {
			rawmode = 1;
		} else if (strcasecmp(argv[1], "nodecode") == 0) {
			nodecode = 1;
		} else if (strcasecmp(argv[1], "nol3") == 0) {
			nol3 = 1;
		} else {
			printf("Didn't understand \"%s\"\n", argv[1]);
			return CMD_USAGE;
		}
	}

	if (!dl0d) {
		printf("No global L0. Please select + configure L0 first\n");
		return CMD_FAILED;
	}

	if (global_l2_conn) {
		printf("L2 already connected, this won't work.\n");
		return CMD_FAILED;
	}

	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		printf("Failed to open hardware interface, ");
		if (rv == DIAG_ERR_PROTO_NOTSUPP) {
			printf("does not support requested L1 protocol\n");
		} else if (rv == DIAG_ERR_BADIFADAPTER) {
			printf("adapter probably not connected\n");
		} else {
			printf("%s\n", diag_errlookup(rv));
		}
		return CMD_FAILED;
	}
	if (rawmode) {
		d_l2_conn = diag_l2_StartCommunications(dl0d, DIAG_L2_PROT_RAW,
			0, global_cfg.speed,
			global_cfg.tgt,
			global_cfg.src);
	} else {
		d_l2_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
			DIAG_L2_TYPE_MONINIT, global_cfg.speed, global_cfg.tgt, global_cfg.src);
	}

	if (d_l2_conn == NULL) {
		printf("Failed to connect to hardware in monitor mode\n");
		diag_l2_close(dl0d);
		return CMD_FAILED;
	}
	//here we have a valid d_l2_conn over dl0d.
	(void) diag_os_ipending();

	if (!rawmode) {
		/* Put the SAE J1979 stack on top of the ISO device */

		if (!nol3) {
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

		printf("Monitoring started. Press Enter to end.\n");
		while (1) {
			if (diag_os_ipending()) {
				break;
			}

			if (d_l3_conn != NULL) {
				rv = diag_l3_recv(d_l3_conn, 10000,
					j1979_watch_rcv,
					(nodecode) ? NULL:(void *)d_l3_conn);
			} else {
				rv = diag_l2_recv(d_l2_conn, 10000,
					j1979_watch_rcv, NULL);
			}
			if (rv == 0) {
				continue;
			}
			if (rv == DIAG_ERR_TIMEOUT) {
				continue;
			}
		}
	} else {
		//rawmode
		/*
		 * And just read stuff, callback routine will print out the data
		 */
		printf("Monitoring started. Press Enter to end.\n");
		while (1) {
			if (diag_os_ipending()) {
				break;
			}

			rv = diag_l2_recv(d_l2_conn, 10000,
				j1979_data_rcv, (void *)&_RQST_HANDLE_WATCH);
			if (rv == 0) {
				continue;
			}
			if (rv == DIAG_ERR_TIMEOUT) {
				continue;
			}
			printf("recv returns %d\n", rv);
			break;
		}
	}
	if (d_l3_conn != NULL) {
		diag_l3_stop(d_l3_conn);
	}

	diag_l2_StopCommunications(d_l2_conn);
	diag_l2_close(dl0d);

	return CMD_OK;
}


/*
 * Print the monitorable data out, use SI units by default, or "english"
 * units
 */
static void
print_current_data(bool english) {
	char buf[24];
	ecu_data *ep;
	unsigned int i;
	unsigned int j;

	printf("%-30.30s %-15.15s FreezeFrame\n",
		"Parameter", "Current");

	for (j = 0 ; get_pid(j) != NULL ; j++) {
		const struct pid *p = get_pid(j) ;

		for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
			if (DATA_VALID(p, ep->mode1_data) ||
				DATA_VALID(p, ep->mode2_data)) {
				printf("%-30.30s ", p->desc);

				if (DATA_VALID(p, ep->mode1_data)) {
					p->cust_snprintf(buf, sizeof(buf), english, p,
						ep->mode1_data, 2);
				} else {
					snprintf(buf, sizeof(buf), "-----");
				}

				printf("%-15.15s ", buf);

				if (DATA_VALID(p, ep->mode2_data)) {
					p->cust_snprintf(buf, sizeof(buf), english, p,
						ep->mode2_data, 3);
				} else {
					snprintf(buf, sizeof(buf), "-----");
				}

				printf("%-15.15s\n", buf);
			}
		}
	}
}

static void
log_response(int ecu, response *r) {
	assert(global_logfp != NULL);

	/* Only print good records */
	if (r->type != TYPE_GOOD) {
		return;
	}

	printf("%d: ", ecu);
	diag_data_dump(global_logfp, r->data,r->len);
	fprintf(global_logfp, "\n");
}

static void
log_current_data(void) {
	response *r;
	ecu_data *ep;
	unsigned int i;

	if (!global_logfp) {
		return;
	}

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
cmd_monitor(int argc, char **argv) {
	int rv;
	bool english = 0;

	if ((argc > 1) && (strcmp(argv[1], "?") == 0)) {
		return CMD_USAGE;
	}

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_FAILED;
	}

	// If user states English or Metric, use that, else use config item
	if (argc > 1) {
		if (strcasecmp(argv[1], "english") == 0) {
			english = 1;
		} else if (strcasecmp(argv[1], "metric") == 0) {
			english = 0;
		} else {
			return CMD_USAGE;
		}
	} else {
		english = global_cfg.units;
	}

	printf("Monitoring. Press <enter> to stop.\n");

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
cmd_scan(UNUSED(int argc), UNUSED(char **argv)) {
	int rv=DIAG_ERR_GENERAL;
	if (argc > 1) {
		return CMD_USAGE;
	}

	if (global_state == STATE_SCANDONE) {
		printf("scan already done !\n");
		return CMD_OK;
	}
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
		printf("Please check :\n");
		printf("\tAdapter is connected to PC\n");
		printf("\tCable is connected to Vehicle\n");
		printf("\tVehicle is switched on\n");
		printf("\tVehicle is OBDII compliant\n");
		return CMD_FAILED;
	}
	return CMD_OK;
}



static int
cmd_cleardtc(UNUSED(int argc), UNUSED(char **argv)) {
	char *input;

	if (global_state < STATE_CONNECTED) {
		printf("Not connected to ECU\n");
		return CMD_OK;
	}

	input = basic_get_input("Are you sure you wish to clear the Diagnostic "
			"Trouble Codes (y/n) ? ", stdin);
	if (!input) {
		return CMD_OK;
	}

	if ((strcasecmp(input, "yes") == 0) || (strcasecmp(input, "y")==0)) {
		if (diag_cleardtc() == 0) {
			printf("Done\n");
		} else {
			printf("Failed\n");
		}
	} else {
		printf("Not done\n");
	}

	free(input);
	return CMD_OK;
}



static int
cmd_ecus(UNUSED(int argc), UNUSED(char **argv)) {
	ecu_data *ep;
	unsigned int i;

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_OK;
	}

	printf("%d ECUs found\n", ecu_count);

	for (i=0, ep=ecu_info; i<ecu_count; i++, ep++) {
		printf("ECU %d: Address 0x%02X ", i, ep->ecu_addr & 0xff);
		if (ep->supress) {
			printf("output supressed for monitor mode\n");
		} else {
			printf("\n");
		}
	}
	return CMD_OK;
}

static void
print_resp_info(UNUSED(int mode), response *data) {

	int i;
	for (i=0; i<256; i++) {
		if (data->type != TYPE_UNTESTED) {
			if (data->type == TYPE_GOOD) {
				printf("0x%02X: ", i );
				diag_data_dump(stdout, data->data, data->len);
				printf("\n");
			} else {
				printf("0x%02X: Failed 0x%X\n",
					i, data->data[1]);
			}
		}
		data++;
	}
}


static int
cmd_dumpdata(UNUSED(int argc), UNUSED(char **argv)) {
	ecu_data *ep;
	int i;

	printf("Current Data\n");
	for (i=0, ep=ecu_info; i<MAX_ECU; i++,ep++) {
		if (ep->valid) {
			printf("ECU 0x%02X:\n", ep->ecu_addr & 0xff);
			print_resp_info(1, ep->mode1_data);
		}
	}

	printf("Freezeframe Data\n");
	for (i=0,ep=ecu_info; i<MAX_ECU; i++,ep++) {
		if (ep->valid) {
			printf("ECU 0x%02X:\n", ep->ecu_addr & 0xff);
			print_resp_info(2, ep->mode2_data);
		}
	}

	return CMD_OK;
}




/*print_pidinfo() : print supported PIDs (0 to 0x60) */
static void
print_pidinfo(int mode, uint8_t *pid_data) {
	int i,j;	/* j : # pid per line */

	printf(" Mode %d:", mode);
	for (i=0, j=0; i<=0x60; i++) {
		if (j == 8) {
			j = 0;
		}

		if (pid_data[i]) {
			if (j == 0) {
				printf("\n \t"); // once per line
			}
			printf("0x%02X ", i);
			j++;
		}
	}
	printf("\n");
}


static int cmd_pids(UNUSED(int argc), UNUSED(char **argv)) {
	ecu_data *ep;
	int i;

	if (global_state < STATE_SCANDONE) {
		printf("SCAN has not been done, please do a scan\n");
		return CMD_OK;
	}

	for (i=0,ep=ecu_info; i<MAX_ECU; i++,ep++) {
		if (ep->valid) {
			printf("ECU %d address 0x%02X: Supported PIDs:\n",
				i, ep->ecu_addr & 0xff);
			print_pidinfo(1, ep->mode1_info);
			print_pidinfo(2, ep->mode2_info);
			print_pidinfo(5, ep->mode5_info);
			print_pidinfo(6, ep->mode6_info);
			print_pidinfo(8, ep->mode8_info);
			print_pidinfo(9, ep->mode9_info);
		}
	}
	printf("\n");

	return CMD_OK;
}

const struct cmd_tbl_entry scantool_cmd_table[] = {
	{ "scan", "scan", "Start SCAN process", cmd_scan, 0, NULL},
	{ "monitor", "monitor [english/metric]", "Continuously monitor rpm etc",
		cmd_monitor, 0, NULL},
	{ "cleardtc", "cleardtc", "Clear DTCs from ECU", cmd_cleardtc, 0, NULL},
	{ "ecus", "ecus", "Show ECU information", cmd_ecus, 0, NULL},
	{ "watch", "watch [raw/nodecode/nol3]",
		"Watch the diagnostic bus and, if not in raw/nol3 mode, decode data",
		cmd_watch, 0, NULL},
	{ "dumpdata", "dumpdata", "Show Mode1 Pid1/2 responses",
		cmd_dumpdata, 0, NULL},
	{ "pids", "pids", "Shows PIDs supported by ECU",
		cmd_pids, 0, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL}
};

