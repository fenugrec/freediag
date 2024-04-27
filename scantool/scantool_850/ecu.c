#include <stdlib.h>
#include <stdint.h>

#include "../diag.h"
#include "ecu.h"
#include "config.h"

const struct ecu_info *ecu_info_by_addr(uint8_t addr) {
	const struct ecu_info *ecu_entry;
	for (ecu_entry = ecu_list; ecu_entry->name != NULL; ecu_entry++) {
		if (addr == ecu_entry->addr) {
			return ecu_entry;
		}
	}
	return NULL;
}


const struct ecu_info *ecu_info_by_name(const char *name) {
	const struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (strcasecmp(name, ecu->name) == 0) {
			return ecu;
		}
	}

	return NULL;
}


#ifdef DEFAULT_850_ECU

const struct ecu_info ecu_list[] = {
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

#endif