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
 * CLI routines - dyno subcommand
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_l3.h"

#include "libcli.h"

#include "scantool.h"
#include "scantool_cli.h"

#include "dyno.h"

/* uncomment this to make fake dyno (to test dyno with disconnected ECU) */
/* #define DYNO_DEBUG 1 */


#ifdef DYNO_DEBUG
	#include <math.h>       //for fake_loss_measure_data()
#endif



dyno_result *dyno_results;
int dyno_nb_results;

static int cmd_dyno_help(int argc, char **argv);
static int cmd_dyno_mass(int argc, char **argv);
static int cmd_dyno_loss(int argc, char **argv);
static int cmd_dyno_setloss(int argc, char **argv);
static int cmd_dyno_run(int argc, char **argv);
static int cmd_dyno_measures(int argc, char **argv);
static int cmd_dyno_result(int argc, char **argv);
static int cmd_dyno_graph(int argc, char **argv);
static int cmd_dyno_save(int argc, char **argv);

void reset_results(void);

const struct cmd_tbl_entry dyno_cmd_table[] = {
	{ "help", "help [command]", "Gives help for a command",
	  cmd_dyno_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
	  cmd_dyno_help, CLI_CMD_HIDDEN, NULL},

	{ "mass", "mass [mass]", "Step 1 : Shows/Sets the mass of the vehicle",
	  cmd_dyno_mass, 0, NULL},
	{ "loss", "loss", "Step 2 : Determines power lost by aerodynamic and friction forces",
	  cmd_dyno_loss, 0, NULL},
	{ "setloss", "setloss [d] [f]", "Manually enter aerodynamic and friction forces parameters",
	  cmd_dyno_setloss, 0, NULL},
	{ "run", "run", "Step 3 : Run dyno",
	  cmd_dyno_run, 0, NULL},
	{ "measures", "measures", "Display run measures",
	  cmd_dyno_measures, 0, NULL},
	{ "result", "result", "Display run results",
	  cmd_dyno_result, 0, NULL},
	{ "graph", "graph", "Display run graphs",
	  cmd_dyno_graph, 0, NULL},
	{ "save", "save [filename]", "Save measures and results in a file",
	  cmd_dyno_save, 0, NULL},

	CLI_TBL_BUILTINS,
	CLI_TBL_END
};


/*
 * Show/Sets the mass of the vehicle
 */
static int cmd_dyno_mass(int argc, char **argv) {
	int mass=0;

	if (argc > 1) {
		mass = htoi(argv[1]);
	}

	if (mass > 0) {
		dyno_set_mass(mass);
	} else {
		printf("mass: %d kg\n", dyno_get_mass());
	}

	return 0;
}

/******************************************************************************
* Functions to measure data                                                  *
******************************************************************************/

#define DYNDATA_1(p, n, d)      (d[p].data[n])
#define DYNDATA_2(p, n, d)      (DYNDATA_1(p, n, d) * 256 + DYNDATA_1(p, n+1, d))
#define RPM_PID           (0x0c)
#define RPM_DATA(d)       (DYNDATA_2(RPM_PID, 2, d)*0.25)
#define SPEED_PID         (0x0d)
#define SPEED_DATA(d)     (DYNDATA_1(SPEED_PID, 2, d) * 10000./36.) /* m/s * 1000 */

#define SPEED_ISO_TO_KMH(_speed_) ((_speed_)*36/10000)

/* measure speed */
// return <0 if error
static int measure_data(uint8_t data_pid, ecu_data *ep) {
	int rv;

	if (global_l3_conn == NULL) {
		fprintf(stderr, FLFMT "Error: there must be an active L3 connection!\n", FL);
		return DIAG_ERR_GENERAL;
	}
	/* measure */
	rv = l3_do_j1979_rqst(global_l3_conn, 0x1, data_pid, 0x00,
	                      0x00, 0x00, 0x00, 0x00, (void *)&_RQST_HANDLE_NORMAL);
	if (rv < 0) {
		return rv;
	}

	/* data extraction */
	if (data_pid == RPM_PID) {
		return (int)(RPM_DATA(ep->mode1_data) + .50);
	}
	if (data_pid == SPEED_PID) {
		return (int)(SPEED_DATA(ep->mode1_data) + .50);
	}
	return DYNDATA_1(data_pid, 2, ep->mode1_data);
}


#ifdef DYNO_DEBUG
int counter;
unsigned long tv0d;     /* measuring time */

/* fake loss measures */
int fake_loss_measure_data() {
	unsigned long tv;
	unsigned long elapsed; /* elapsed time */
	int speed;

	if (counter == 0) {
		tv0d=diag_os_getms();
	}

	diag_os_millisleep(250);

	/* get elapsed time */
	tv=diag_os_getms();
	elapsed = tv - tv0d;


	//I think we're supposed to simulate an exponential decreasing
	//function ; speed = b * e^(elapsed / a).
	float a= -62810;
	float b= 25027;

	counter++;

	return (int) (b * exp(elapsed / a));
}

int counter2;
/* fake run measures */
int fake_run_measure_data(int data_pid) {
	int rpm;

	diag_os_millisleep(250);

	rpm = 1000 + counter2 * 200;
	if (counter2 < 5500/200) {
		counter2++;
	} else {
		counter2--;
	}

	if (data_pid == RPM_PID) {
		return rpm;
	} else if (data_pid == SPEED_PID) {
		return rpm * (9000*100/6000) / 36;
	}

	return 0;
}

#define LOSS_MEASURE_DATA(_pid_, _ep_) fake_loss_measure_data()
#define RUN_MEASURE_DATA(_pid_, _ep_)  fake_run_measure_data(_pid_)
#else /* DYNO_DEBUG */
#define LOSS_MEASURE_DATA(_pid_, _ep_) measure_data(_pid_, _ep_)
#define RUN_MEASURE_DATA(_pid_, _ep_)  measure_data(_pid_, _ep_)

#endif


/*****************************************************************************
* Measuring loss function                                                   *
*****************************************************************************/

/* Indicate whether loss determination has been done */
int dyno_loss_done;

/*
 * Determine power lost by aerodynamic and friction forces
 */

static int cmd_dyno_loss(UNUSED(int argc), UNUSED(char **argv)) {
	ecu_data *ep;

	int speed;              /* measured speed */
	int speed_previous = 0; /* previous speed */

	unsigned long tv0, tv;
	int elapsed; /* elapsed time */

	int i, length; /* length of printed string */
	int nb = 0; /* number of measures */

	//make sure we have an L3 connection first !
	if (global_l3_conn == NULL) {
		fprintf(stderr, FLFMT "Error: there must be an active L3 connection!\n", FL);
		return CMD_FAILED;
	}

	/* Check mass */
	if (dyno_get_mass() <= 0) {
		printf("The mass of the vehicle has not been set, please set the mass first\n");
		return CMD_OK;
	}

	/* Show instructions */
	printf("To proceed loss determination, reach the maximum speed you will reach during\n");
	printf("dyno, then push in the clutch, leaving the car in gear. Allow the car to coast\n");
	printf("down to the lowest possible speed. Press ENTER when finished.\n");
	printf("\n");
	wait_enter("Press ENTER when ready... ");
	printf("\n");

	/* Reset data */
	dyno_loss_reset(); /* dyno data */
	reset_results();
	tv0=diag_os_getms(); /* initial time */
	ep = ecu_info; /* ECU data */

	/* exclude 1st measure */
	speed_previous = LOSS_MEASURE_DATA(SPEED_PID, ep); /* m/s * 1000 */
	if (speed_previous < 0) {
		printf("invalid speed !\n");
		return CMD_FAILED;
	}

	printf("Starting loss determination (max speed=%d km/h)\n", SPEED_ISO_TO_KMH(speed_previous));
	printf("Number of measures : 0");
	length = 1;

	/* loss measures */
	while (1) {
		/* measure speed */
		speed = LOSS_MEASURE_DATA(SPEED_PID, ep); /* m/s * 1000 */
		if (speed < 0) {
			printf("invalid speed !\n");
			break;
		}


		/* get elapsed time */

		tv=diag_os_getms();
		elapsed = (int) (tv - tv0);

		if (speed < speed_previous) {
			/* Add measure */
			dyno_loss_add_measure(elapsed, speed);
			nb++;

			speed_previous = speed;
		}

		if (pressed_enter() != 0) {
			/* ENTER pressed : stops */
			printf("Number of measures : %d (min speed=%d km/h)\n", nb, SPEED_ISO_TO_KMH(speed));
			break;
		}
		if (speed_previous == speed) { /* measure added: update display */
			/* erase previous measure */
			for (i = 0; i < length; i++) {
				printf("");
			}

			/* Display new measure */
			length = printf("%d (speed=%d km/h, d=%5.5f, f=%4.2f)\t ",
			                nb, SPEED_ISO_TO_KMH(speed), dyno_loss_get_d(), dyno_loss_get_f());
			fflush(stdout); /* force displaying now (may slow down dyno...) */
		}
	}

	/* display dyno time */
	//elapsed = MILLIS(tv) - MILLIS(tv0);
	tv=diag_os_getms();
	elapsed= (int) (tv - tv0);
	printf("d=%5.5f, f=%4.2f\n", dyno_loss_get_d(), dyno_loss_get_f());
	printf("Loss determination time : %ds.\n", (elapsed/1000));

	printf("\n");

	/* now dyno loss has been done */
	dyno_loss_done = 1;

	return CMD_OK;
}

/*
 * Manually enter aerodynamic and friction forces d and f parameters
 */
static int cmd_dyno_setloss(int argc, char **argv) {
	if (argc > 1) {
		int assigned;
		double d;
		assigned = sscanf(argv[1], "%le", &d);
		if (assigned > 0) {
			dyno_loss_set_d(d);
		}
	}

	if (argc > 2) {
		int assigned;
		double f;
		assigned = sscanf(argv[2], "%le", &f);
		if (assigned > 0) {
			dyno_loss_set_f(f);
		}
	}

	printf("d=%5.5f, f=%4.2f\n", dyno_loss_get_d(), dyno_loss_get_f());
	printf("\n");

	if (argc > 2) {
		/* now consider that dyno loss has been done */
		dyno_loss_done = 1;
	}

	return CMD_OK;
}

/*****************************************************************************
* Dyno functions                                                            *
*****************************************************************************/

/*
 * Run dyno
 */

static int cmd_dyno_run(UNUSED(int argc), UNUSED(char **argv)) {
	ecu_data *ep;

	int speed;                                              /* measured speed */
	int rpm;                                                        /* measured rpm */
	int rpm_previous = 0; /* previous rpm */

	unsigned long tv0, tv;  /* measuring time */
	int elapsed; /* elapsed time (ms) */

	int i, length = 0; /* length of printed string */
	int nb = 0; /* number of measures */

	//make sure we're connected !
	if (global_l3_conn == NULL) {
		fprintf(stderr, FLFMT "No active L3 connection !\n", FL);
		return CMD_FAILED;
	}

	/* Check mass */
	if (dyno_get_mass() <= 0) {
		printf("The mass of the vehicle has not been set, please set the mass first\n");
		return CMD_OK;
	}

	/* Check mass */
	if (dyno_loss_done <= 0) {
		printf("The loss determination has not been done, please use command loss or setloss first\n");
		return CMD_OK;
	}

	/* Show instructions */
	printf("To proceed dyno, do a full-throttle acceleration run\n");
	printf("in a single gear from a rolling start.\n");
	printf("The run ends automatically when RPM begins to decrease.\n");
	printf("\n");
	wait_enter("Press ENTER when ready... ");
	printf("\n");

	/* Reset data */
	dyno_reset(); /* dyno data */
	reset_results();

	tv0=diag_os_getms();    /* initial time */
	ep = ecu_info; /* ECU data */

	/* Measures */
	while (1) {
		/* measure RPM */
		rpm = RUN_MEASURE_DATA(RPM_PID, ep);

		if (rpm < 0) {
			printf("invalid RPM !\n");
			break;
		}


		if (rpm_previous == 0) {
			/* this is the first measure */
			printf("Starting dyno (min rpm=%d)\n", rpm);
			printf("Number of measures : ");
		}

		/* if RPM starts decreasing, stop run */
		if (rpm < rpm_previous) {
			printf(" (max rpm=%d)\n", rpm_previous);
			break;
		}

		/* get elapsed time */
		tv=diag_os_getms();
		elapsed = (int) (tv - tv0);

		/* Add measure */
		dyno_add_measure(elapsed, rpm);

		/* Display number of measures */
		nb++;
		for (i = 0; i < length; i++) {
			printf("");
		}

		length = printf("%d (%d RPM) ", nb, rpm);
		fflush(stdout); /* force displaying now (may slow down dyno...) */

		rpm_previous = rpm;
	}

	/* measure gear ratio */
	rpm_previous = rpm;
	speed = RUN_MEASURE_DATA(SPEED_PID, ep); /* m/s * 1000 */
	rpm      = RUN_MEASURE_DATA(RPM_PID, ep);

	if ((speed < 0) || (rpm < 0)) {
		printf("invalid RUN_MEASURE_DATA result !\n");
		return CMD_FAILED;
	}
	dyno_set_gear(speed, (rpm_previous + rpm) / 2);

	/* display dyno time */
	tv=diag_os_getms();
	elapsed = (int) (tv - tv0);
	printf("Dyno time : %ds.\n", (elapsed/1000));

	printf("\n");

	return CMD_OK;
}


/*****************************************************************************
* Displaying measures and results functions                                 *
*****************************************************************************/

/* Get measures for specified type */
static void get_measures(dyno_measure **measures, int *nb_measures) {
	/* allocate memory */
	(*nb_measures) = dyno_get_nb_measures();
	if ((*nb_measures) == 0) {
		return;
	}
	if (diag_malloc(measures, (*nb_measures))) {
		return;
	}

	/* get measures */
	dyno_get_measures((*measures), (*nb_measures));
}

/* Display given measures */
static void display_measures(dyno_measure *measures, int nb_measures) {
	int i;

	for (i=0; i<nb_measures; i++) {
		printf("measure %d:\t%3.3f s. \tRPM: %d\t%3.3f m/s\t%3.2f km/h\n", (i+1),
		       measures[i].millis/1000.0,
		       measures[i].rpm,
		       dyno_get_speed_from_rpm(measures[i].rpm)/1000.0,
		       dyno_get_speed_from_rpm(measures[i].rpm)*3.6/1000.0);

		if (((i+1) % 22) == 0) {
			wait_enter("Press ENTER to continue... ");
		}
	}
}

/* Display all measures */

static int cmd_dyno_measures(UNUSED(int argc), UNUSED(char **argv)) {
	dyno_measure *measures = NULL;
	int nb_measures = 0;

	printf("Dyno measures :\n");
	get_measures(&measures, &nb_measures);
	display_measures(measures, nb_measures);
	free(measures);

	printf("%d measures.\n", nb_measures);
	printf("\n");

	return CMD_OK;
}


/* Display results */
static void display_results(dyno_result *results, int nb) {
	int i;

	int max_power_i = 0;
	int max_power = 0;
	int max_torque_i = 0;
	int max_torque = 0;

	for (i=0; i<nb; i++) {
		printf("%d:\tRPM=%d\t\tpower=%d W (%d ch)\ttorque=%d Nm\n", i,
		       results[i].rpm, results[i].power, results[i].power_ch, results[i].torque);

		if (results[i].power > max_power) {
			max_power = results[i].power;
			max_power_i = i;
		}
		if (results[i].torque > max_torque) {
			max_torque = results[i].torque;
			max_torque_i = i;
		}

		if (((i+1) % 22) == 0) {
			wait_enter("Press ENTER to continue... ");
		}
	}
	printf("\n");
	printf("Max power : %d ch (at %d RPM)\n",
	       results[max_power_i].power_ch, results[max_power_i].rpm);
	printf("Max torque : %d Nm (at %d RPM)\n",
	       results[max_torque_i].torque, results[max_torque_i].rpm);
	printf("\n");
}

#define DYNO_GRAPH_HEIGHT 21

/* Display graphs */
static void display_graphs(dyno_result *results, int nb) {
	int row, col, step;

	int max_power_i = 0;
	int max_power = 0;
	int max_torque_i = 0;
	int max_torque = 0;

	/* Detect maximums */
	for (col=0; col<nb; col++) {
		if (results[col].power > max_power) {
			max_power = results[col].power;
			max_power_i = col;
		}
		if (results[col].torque > max_torque) {
			max_torque = results[col].torque;
			max_torque_i = col;
		}
	}

	/* 80 columns max */
	step = (nb / 80) + 1;

	/* Displaying torque */
	printf("Torque :\n");
	for (row=DYNO_GRAPH_HEIGHT-1; row>=0; row--) {
		for (col=0; col<nb; col+=step) {
			if (results[col].torque * DYNO_GRAPH_HEIGHT >
			    results[max_torque_i].torque * row) {
				printf("*");
			} else {
				printf(" ");
			}
		}
		printf("\n");
	}

	printf("\n");

	/* Pause */
	wait_enter("Press ENTER to continue... ");

	printf("\n");

	/* Displaying power */
	printf("Power :\n");
	for (row=DYNO_GRAPH_HEIGHT-1; row>=0; row--) {
		for (col=0; col<nb; col+=step) {
			if (results[col].power * DYNO_GRAPH_HEIGHT >
			    results[max_power_i].power * row) {
				printf("*");
			} else {
				printf(" ");
			}
		}
		printf("\n");
	}
	printf("\n");
}

/* Get the results in global vars */
static void get_results(void) {
	/* allocating memory for the results table */
	if (dyno_results == NULL) {
		dyno_nb_results = dyno_get_nb_results();
		if (dyno_nb_results == 0) {
			return;
		}
		if (diag_malloc(&dyno_results, dyno_nb_results)) {
			return;
		}
	}

	/* raw results */
	dyno_get_results(dyno_results, dyno_nb_results);

	/* smooth results */
	dyno_smooth_results(dyno_results, dyno_nb_results);
}

/* Reset results */
void reset_results(void) {
	if (dyno_results != NULL) {
		free(dyno_results);
	}
	dyno_results = NULL;
	dyno_nb_results = 0;
}

/*
 * Display dyno results
 */

static int cmd_dyno_result(UNUSED(int argc), UNUSED(char **argv)) {
	get_results();

	/* Check data */
	if (dyno_nb_results <= 0) {
		printf("Dyno run has not been done, please do a run first\n");
		return CMD_OK;
	}

	display_results(dyno_results, dyno_nb_results);
	return CMD_OK;
}

/*
 * Display dyno graphs
 */

static int cmd_dyno_graph(UNUSED(int argc), UNUSED(char **argv)) {
	get_results();

	/* Check data */
	if (dyno_nb_results <= 0) {
		printf("Dyno run has not been done, please do a run first\n");
		return CMD_OK;
	}

	display_graphs(dyno_results, dyno_nb_results);
	return CMD_OK;
}


/*****************************************************************************
* Saving functions                                                          *
*****************************************************************************/

/*
 * Save dyno measures and results to a file
 */
static int cmd_dyno_save(int argc, char **argv) {
	char *filename;
	int rv;

	get_results();

	/* Check data */
	if (dyno_nb_results <= 0) {
		printf("Dyno run has not been done, please do a run first\n");
		return CMD_OK;
	}


	if (argc > 1) {
		/* Get filename from command arguments */
		size_t length = strlen(argv[1]);
		rv = diag_malloc(&filename, length + 1);
		if (rv != 0) {
			return rv;
		}
		strcpy(filename, argv[1]);
	} else {
		/* Get filename from user input */
		size_t nbytes = 256;
		rv = diag_malloc(&filename, nbytes + 1);
		if (rv != 0) {
			return rv;
		}

		printf("Enter filename: ");
		if (fgets(filename, (int)nbytes, stdin) == 0) {
			return CMD_OK;
		}

		/* Remove pending "\n" and "\r", if any */
		while ((filename[strlen(filename)-1] == '\n') ||
		       (filename[strlen(filename)-1] == '\r')) {
			filename[strlen(filename)-1] = '\0';
		}
	}

	dyno_save(filename, dyno_results, dyno_nb_results);
	printf("\n");

	free(filename);
	return CMD_OK;
}


/* Display help */
static int cmd_dyno_help(int argc, char **argv) {
	return help_common(argc, argv, dyno_cmd_table);
}
