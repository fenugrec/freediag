#ifndef __SCANTOOL_850_DTC_BASIC_H__
#define __SCANTOOL_850_DTC_BASIC_H__

#include <stdlib.h>
#include "dtc.h"
#include "ecu.h"

static const struct ecu_info ecu_list[] = {
	{0x01, "abs", "antilock brakes", "ABS"},
#if 0
	/*
	 * Don't have an M4.3 ECU to test. Will probably need separate DTC and
	 * live data tables for M4.3.
	 */
	{0x10, "m43", "Motronic M4.3 engine management (DLC pin 3)", "EFI"}, /* 12700bps, KWP71 */
#endif
	{0x10, "m44old", "Motronic M4.4 engine management (old protocol)", "EFI"},
	{0x11, "msa", "MSA 15.7 engine management (diesel vehicles)","EFI"},
	/* 0x13 - Volvo Scan Tool tester address */
#if 0
	{0x15, "m18", "Motronic M1.8 engine management (960)", "EFI"}, /* 4800bps, KWP71 */
#endif
	{0x18, "add", "912-D fuel-driven heater (cold climate option)", "HEA"},
	{0x29, "ecc", "electronic climate control", "ECC"},
	{0x2d, "vgla", "alarm", "GLA"},
	{0x2e, "psl", "left power seat", "PSL"},
	{0x2f, "psr", "right power seat", "PSR"},
	/* 0x33 - J1979 OBD2 */
	{0x41, "immo", "immobilizer", "IMM"},
	{0x51, "combi", "combined instrument panel", "CI"},
	{0x58, "srs", "airbags", "SRS"},
	{0x6e, "aw50", "AW50-42 transmission", "AT"},
	{0x7a, "m44", "Motronic M4.4 engine management", "EFI"},
	{0, NULL, NULL, NULL}
};

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

static const struct ecu_dtc_table_map_entry ecu_dtc_map[] = {
	{0x6e, aw50_dtc},
	{0x10, m44_old_dtc},
	{0x7a, m44_dtc},
	{0, NULL},
};

#endif // __SCANTOOL_850_DTC_BASIC_H__
