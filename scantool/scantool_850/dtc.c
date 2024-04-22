#include <stdlib.h>
#include "dtc.h"

static const struct dtc_table_entry aw50_dtc[] = {
	{0x13, 332, "Torque converter lock-up solenoid open circuit", NULL},
	{0, 0, NULL, NULL},
};

static const struct dtc_table_entry m44_old_dtc[] = {
	{0x54, 445, "Pulsed secondary air injection system pump signal", NULL},
	{0, 0, NULL, NULL},
};

static const struct dtc_table_entry m44_dtc[] = {
	{0x54, 445, "Pulsed secondary air injection system pump signal", NULL},
	{0, 0, NULL, NULL},
};

const struct ecu_dtc_table_map_entry ecu_dtc_map[] = {
	{0x6e, aw50_dtc},
	{0x10, m44_old_dtc},
	{0x7a, m44_dtc},
	{0, NULL},
};

