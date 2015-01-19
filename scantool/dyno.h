#ifndef _DYNO_H_
#define _DYNO_H_

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

#if defined(__cplusplus)
extern "C" {
#endif

/* Return codes */
#define DYNO_OK     0   /* OK */
#define DYNO_USAGE  -1  /* Bad usage */

/* structure for loss measure tables */
typedef struct dyno_loss_measure
{
  int millis; /* number of milliseconds */
	int	speed;  /* m/s * 1000 */
} dyno_loss_measure;

/* structure for measure tables */
typedef struct dyno_measure
{
  int millis; /* number of milliseconds */
	int	rpm;    /* rpm */
} dyno_measure;

/* structure for dyno results */
typedef struct dyno_result
{
  int rpm; /* rev per minute */
  int power; /* power (W) */
  int power_ch; /* power (ch DYN) */
  int torque; /* torque (N.m) */
} dyno_result;


/* Mass of the vehicle (kg) */
int dyno_set_mass(int mass);
int dyno_get_mass(void);

/* Set ratio between speed (m/s * 1000) and rpm */
int dyno_set_gear(int speed, int rpm);
int dyno_get_speed_from_rpm(int rpm);


/*
 * Add loss measure
 * - millis : time in milliseconds
 * - speed  : speed in m/s * 1000 (mm/s)
 */
int dyno_loss_add_measure(int millis, int speed);

/* Get loss measures results */
double dyno_loss_get_d(void);
double dyno_loss_get_f(void);

/* Set d and f values */
void dyno_loss_set_d(double d);
void dyno_loss_set_f(double f);

/* Reset all dyno loss measures */
int dyno_loss_reset(void);


/*
 * Add a measure
 * - millis : time in milliseconds
 * - rpm    : rpm
 */
int dyno_add_measure(int millis, int rpm);

/* Reset all dyno data */
int dyno_reset(void);


/* Get number of measures */
int dyno_get_nb_measures(void);

/* Get all measures */
int dyno_get_measures(dyno_measure * measures, int size);


/* Get number of results */
int dyno_get_nb_results(void);

/* Get power and torque results */
int dyno_get_results(dyno_result * results, int size);

/* smooth results */
int dyno_smooth_results(dyno_result * results, int size);


/* save measures and results to a file */
void dyno_save(char * filename, dyno_result * results, int size);

#if defined(__cplusplus)
}
#endif
#endif /* _DYNO_H_ */
