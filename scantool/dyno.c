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
 * Dyno functionnalities
 *
 */

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "dyno.h"


#define SQR(_x_) ((_x_)*(_x_))
#define CUB(_x_) ((_x_)*(_x_)*(_x_))
#define PI 3.141592654
#define MIN(_a_, _b_) (((_a_) < (_b_) ? (_a_) : (_b_)))

/* generic measures table */
struct dyno_measure_table {
  int size; /* allocated size */
  int nbr;  /* number of elements in the table */
  union {   /* table of measures */
    dyno_loss_measure * loss_measures;
    dyno_measure * measures;
  } meas;
};

/* measures and loss measures table */
struct dyno_measure_table loss_measure_table;
struct dyno_measure_table measure_table;

/* default size of measure table */
#define DYNO_DEFAULT_TABLE_SIZE 100


/*****************************************************************************
 * Table allocation                                                          *
 *****************************************************************************/

/* Check table allocated size before addind a new element */
static int dyno_check_allocated_table(struct dyno_measure_table *table) {
  int rv;
  if (table->meas.measures == NULL) {
    /* allocating the table */
    table->size = DYNO_DEFAULT_TABLE_SIZE;
    table->nbr  = 0;
    rv = diag_malloc(&table->meas.measures, table->size);
    if (rv != 0) {
      return rv;
    }
  } else if (table->nbr == table->size) {
    /* reallocating the table if maximum capacity is reached */
    dyno_measure *ptr;

    /* new table size */
    table->size *= 2;

    /* allocating, transfering data */
    rv = diag_malloc(&ptr, table->size);
    if (rv != 0) {
	return rv;
    }

    memcpy(ptr, table->meas.measures, table->nbr * sizeof(*(table->meas.measures)));

    /* free old memory */
    free(table->meas.measures);
    table->meas.measures = (dyno_measure *)ptr;
  }
  return 0;
}

/* reset table content */
static int dyno_table_reset(struct dyno_measure_table *table) {
  if (table->meas.measures != NULL) {
    free(table->meas.measures);
    table->meas.measures = NULL;
  }

  table->size = 0;
  table->nbr = 0;
  return 0;
}


/*****************************************************************************
 * Mass                                                                      *
 *****************************************************************************/

/* mass of vehicle */
int dyno_mass;

/*
 * Sets the mass of the vehicle
 */
int dyno_set_mass(int mass) {
  dyno_mass = mass;
  return DYNO_OK;
}

/*
 * Gets the mass of the vehicle
 */
int dyno_get_mass() {
  return dyno_mass;
}


/*****************************************************************************
 * Gear                                                                      *
 *****************************************************************************/

/* Gear accuracy */
#define DYNO_GEAR_ACCURACY 1000

/* gear of vehicle */
int dyno_gear;

/*
 * Set ratio from speed (m/s * 1000) and rpm
 */
int dyno_set_gear(int speed, int rpm) {
  dyno_gear = speed * DYNO_GEAR_ACCURACY / rpm;
  return DYNO_OK;
}

/*
 * Get speed (in m/s * 1000) from rpm
 */
int dyno_get_speed_from_rpm(int rpm) {
  int speed;
  speed = rpm * dyno_gear / DYNO_GEAR_ACCURACY; /* m/s * 1000 */
  return speed;
}


/*****************************************************************************
 * Loss measures                                                             *
 *****************************************************************************/

/*
 * How can "dyno_loss_needs_calculation" default to false?
 * How come when we reset it it still doesn't need calculation?
 */
int dyno_loss_needs_calculation=0;

/* Add loss measure */
int dyno_loss_add_measure(int millis, int speed) {
  /* check memory allocation */
  dyno_check_allocated_table(&loss_measure_table);

  /* storing measure */
  loss_measure_table.meas.loss_measures[loss_measure_table.nbr].millis = millis;
  loss_measure_table.meas.loss_measures[loss_measure_table.nbr].speed = speed;

  /* one measure added */
  loss_measure_table.nbr++;

  /* d and f need to be re-calculated */
  dyno_loss_needs_calculation = 1;

  return DYNO_OK;
}

/*
 * The loss power is : Pl = d * v^3 + f * v
 * with :
 * - d = aerodynamic loss factor
 * - f = friction loss factor
 */
double dyno_loss_d;
double dyno_loss_f;

/*
 * d and f are the solutions of the equation : 0 = Pl + M * v * a
 * where a is the (negative) acceleration obtained when the output power
 * of the engine is nul (i.e. clutch pushed in).
 *
 * If we call y(i) = - M * a(i), we have :
 * d = average( (y(i+1) - y(i)) / (v(i+1)^2 - (vi)^2) )
 * f = average( y(i) - d * v(i)^2 )
 */

/* get acceleration between 2 measures (m/s^2) */
static double dyno_loss_a_inter(int i, int j) {
  double a;
  dyno_loss_measure *measure0;
  dyno_loss_measure *measure1;

  /* avoid bad use */
  if ((i < 0) || (i >= j) || (j > loss_measure_table.nbr)) {
	  return 0;
  }

  /* select measures */
  measure0 = &loss_measure_table.meas.loss_measures[i];
  measure1 = &loss_measure_table.meas.loss_measures[j];

  /* get acceleration */
  a = 1.0
    * (measure1->speed - measure0->speed)
    / (measure1->millis - measure0->millis);

  return a;
}

/* a(i) (m/s^2) */
static double dyno_loss_a(int i) {
  double a;

  if (i <= 0) {
	  a = dyno_loss_a_inter(0, 1);
  } else if (i >= loss_measure_table.nbr - 1) {
	  a = dyno_loss_a_inter(loss_measure_table.nbr - 2,
				loss_measure_table.nbr - 1);
  } else {
	  a = (dyno_loss_a_inter(i - 1, i) + dyno_loss_a_inter(i, i + 1)) / 2;
  }

  return a;
}

/* get y(i) */
static double dyno_loss_y(int i) {
  return (0 - dyno_mass * dyno_loss_a(i));
}

/* Calculate d and f factors */
static int dyno_loss_calculate(void) {
  int i, nb;
  double sum;
  dyno_loss_measure *measure0;
  dyno_loss_measure *measure1;

  /* calculate d */
  nb = loss_measure_table.nbr - 1;
  sum = 0;
  for (i=0; i<nb; i++) {
    double d;

    /* select measures */
    measure0 = &loss_measure_table.meas.loss_measures[i];
    measure1 = &loss_measure_table.meas.loss_measures[i+1];

    d = (dyno_loss_y(i+1) - dyno_loss_y(i))
        / (SQR(measure1->speed/1000.0) - SQR(measure0->speed/1000.0));
    sum += d;
  }
  dyno_loss_d = sum / nb;

  /* calculate f */
  nb = loss_measure_table.nbr;
  sum = 0;
  for (i=0; i<nb; i++) {
    double f;

    measure0 = &loss_measure_table.meas.loss_measures[i];

    f = dyno_loss_y(i) - (dyno_loss_d * SQR(measure0->speed/1000.0));
    sum += f;
  }
  dyno_loss_f = sum / nb;

  dyno_loss_needs_calculation = 0;

  return DYNO_OK;
}

/* Reset all dyno loss measures */
int dyno_loss_reset() {
  dyno_table_reset(&loss_measure_table);

  dyno_loss_d = 0;
  dyno_loss_f = 0;
  dyno_loss_needs_calculation = 0;
  return 0;
}

/* Get d value */
double dyno_loss_get_d() {
	if (dyno_loss_needs_calculation == 1) {
		dyno_loss_calculate();
	}

	return dyno_loss_d;
}

/* Get f value */
double dyno_loss_get_f() {
	if (dyno_loss_needs_calculation == 1) {
		dyno_loss_calculate();
	}

	return dyno_loss_f;
}

void dyno_loss_set_d(double d) {
  dyno_loss_d = d;
}

void dyno_loss_set_f(double f) {
  dyno_loss_f = f;
}

/* Calculate loss power from speed (m/s2 * 1000) */
static long dyno_loss_power(long speed) {
  double power;

  if (dyno_loss_needs_calculation == 1) {
	  dyno_loss_calculate();
  }

  /* Pl = d * v^3 + f * v */
  power = dyno_loss_d * CUB(speed/1000.0) + dyno_loss_f * (speed/1000.0);

  return (long)(power + .50);
}

/*****************************************************************************
 * Measures                                                                  *
 *****************************************************************************/

/* Add a measure */
int dyno_add_measure(int millis, int rpm) {
  /* check memory allocation */
  dyno_check_allocated_table(&measure_table);

  /* storing measure */
  measure_table.meas.measures[measure_table.nbr].millis = millis;
  measure_table.meas.measures[measure_table.nbr].rpm = rpm;

  /* one measure added */
  measure_table.nbr++;

  return DYNO_OK;
}

/* Reset all dyno data */
int dyno_reset() {
  return dyno_table_reset(&measure_table);
}

/* Get number of measures */
int dyno_get_nb_measures() {
  return measure_table.nbr;
}

/* Get all measures */
int dyno_get_measures(dyno_measure *measures, int size) {
  /* copy measures */
  memcpy(measures, measure_table.meas.measures, MIN(size, measure_table.nbr) * sizeof(dyno_measure));

  return DYNO_OK;
}


/*****************************************************************************
 * Results                                                                   *
 *****************************************************************************/

/* Calculate torque from power and RPM (power : W, RPM : rpm, torque : N.m) */
#define TORQUE(_power_, _rpm_) (((_power_) * 60) / ((_rpm_) * 2 * PI))

/* Convert power (W to ch DYN) */
#define POWER_CH(_power_) ((_power_) * 10 / 7355)

/*
 * Calculate power between 2 acceleration measures
 */
static void dyno_calculate_result(dyno_measure *measure0, dyno_measure *measure1, dyno_result *result) {
  /* effective power and lost power */
  int p_dyno, p_loss;

  /* calculate effective power from cinetic energy variation */
  p_dyno = SQR(dyno_get_speed_from_rpm(measure1->rpm))
         - SQR(dyno_get_speed_from_rpm(measure0->rpm)); /* m2/s2 * 1.000.000 */
  p_dyno = (p_dyno / (measure1->millis - measure0->millis)); /* m2/s3 */
  p_dyno = p_dyno * dyno_mass / 2000;

  /* calculate loss power at given speed */
  result->rpm = (measure0->rpm + measure1->rpm) / 2;
  p_loss = dyno_loss_power(dyno_get_speed_from_rpm(result->rpm));

  /* rpm, power and torque */
  result->power    = p_dyno + p_loss;
  result->power_ch = POWER_CH(result->power);
  result->torque   = (int)(TORQUE(result->power, result->rpm) + .50);

  return;
}

/*
 * Calculate results
 */
static void dyno_calculate_results(dyno_result *results) {
  int i;
  int nb = dyno_get_nb_results();

  /* for each measure, calculate result */
  for (i=0; i<nb; i++) {
    dyno_calculate_result(&measure_table.meas.measures[i], &measure_table.meas.measures[i+1], &results[i]);
  }
}

/*
 * Get number of results
 */
int dyno_get_nb_results() {
  return (measure_table.nbr - 1);
}

/*
 * Get dyno results
 */

int dyno_get_results(dyno_result *results, UNUSED(int size)) {
	if ((dyno_mass == 0) || (dyno_gear == 0)) {
		return DYNO_USAGE;
	}

	dyno_calculate_results(results);

	return DYNO_OK;
}

/*
 * Smooth results
 */
int dyno_smooth_results(dyno_result *results, int size) {
  dyno_result *rawresults;
  int i, rv;

  /* smooth results */
  rv = diag_malloc(&rawresults, size);
  if (rv != 0) {
    return rv;
  }
  memcpy(rawresults, results, size * sizeof(dyno_result));

  for (i=1; i<size-1; i++) {
    /* smooth using nearby values */
    results[i].power    = (rawresults[i-1].power + rawresults[i].power + rawresults[i+1].power) / 3;
    results[i].power_ch = POWER_CH(results[i].power);
    results[i].torque   = (int)(TORQUE(results[i].power, results[i].rpm) + .50);
  }

  free(rawresults);

  return DYNO_OK;
}


/*
 * Save measures and results to a file
 */
void dyno_save(char *filename, dyno_result *results, int size) {
  char buffer[MAXRBUF];
  int i;

  /* open file */
  FILE *fd = fopen (filename, "w");
  if (fd == NULL) {
    printf("Failed opening %s for writing\n", filename);
    return;
  }

  printf("Saving dyno to file %s\n", filename);


  /* saving mass */
  printf("-> saving mass...\n");

  sprintf(buffer, "Mass (kg)\t%d\n", dyno_mass);
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  sprintf(buffer, "\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);


  /* saving results */
  printf("-> saving results...\n");

  sprintf(buffer, "Run results\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  sprintf(buffer, "RPM\tPower (W)\tPower (ch)\tTorque (N.m)\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  for (i=0; i<size; i++) {
    sprintf(buffer, "%d\t%d\t%d\t%d\n",
      results[i].rpm, results[i].power, results[i].power_ch, results[i].torque);
    fwrite(buffer, strlen(buffer), sizeof(char), fd);
  }

  sprintf(buffer, "\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);


  /* saving run measures */
  if (measure_table.nbr > 0) {
    printf("-> saving run measures...\n");

    sprintf(buffer, "Run measures\n");
    fwrite(buffer, strlen(buffer), sizeof(char), fd);

    sprintf(buffer, "Time (ms)\tRPM\tSpeed (m/s)\tSpeed (km/h)\n");
    fwrite(buffer, strlen(buffer), sizeof(char), fd);

    for (i=0; i<measure_table.nbr; i++) {
      sprintf(buffer, "%d\t%d\t%7.3f\t%7.3f\n",
        measure_table.meas.measures[i].millis,
        measure_table.meas.measures[i].rpm,
        dyno_get_speed_from_rpm(measure_table.meas.measures[i].rpm)/1000.0,
        dyno_get_speed_from_rpm(measure_table.meas.measures[i].rpm)*3.6/1000.0);
      fwrite(buffer, strlen(buffer), sizeof(char), fd);
    }

    sprintf(buffer, "\n");
    fwrite(buffer, strlen(buffer), sizeof(char), fd);
  }


  /* saving d and f loss parameters */
  printf("-> saving friction and aerodynamic parameters...\n");

  sprintf(buffer, "d and f loss parameters\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  sprintf(buffer, "d\t%8.5f\n", dyno_loss_d);
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  sprintf(buffer, "f\t%8.2f\n", dyno_loss_f);
  fwrite(buffer, strlen(buffer), sizeof(char), fd);

  sprintf(buffer, "\n");
  fwrite(buffer, strlen(buffer), sizeof(char), fd);


  /* saving loss measures, if any */
  if (loss_measure_table.nbr > 0) {

    printf("-> saving loss measures...\n");

    sprintf(buffer, "Loss measures\n");
    fwrite(buffer, strlen(buffer), sizeof(char), fd);

    sprintf(buffer, "Time (ms)\tSpeed (m/s)\tSpeed (km/h)\n");
    fwrite(buffer, strlen(buffer), sizeof(char), fd);

    for (i=0; i<loss_measure_table.nbr; i++) {
      sprintf(buffer, "%d\t%7.3f\t%7.3f\n",
        loss_measure_table.meas.loss_measures[i].millis,
        loss_measure_table.meas.loss_measures[i].speed/1000.0,
        loss_measure_table.meas.loss_measures[i].speed*3.6/1000.0);
      fwrite(buffer, strlen(buffer), sizeof(char), fd);
    }
  }

  /* flush file to disk */
  fflush(fd);

  /* close file */
  fclose(fd);

  printf("Done.\n");
}
