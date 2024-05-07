#include <stdlib.h>
#include "dtc.h"

const struct dtc_table_entry *dtctable_by_addr(uint8_t addr) {
	const struct ecu_dtc_table_map_entry *ecu_dtc_entry;
	for (ecu_dtc_entry = ecu_dtc_map; ecu_dtc_entry->ecu_addr != 0; ecu_dtc_entry++) {
		if (ecu_dtc_entry->ecu_addr == addr) {
			return ecu_dtc_entry->dtc_table;
		}
	}
	return NULL;
}

static const struct dtc_table_entry aw50_dtc[] = {
	{0x02, 122, "Shift Solenoid S1 circuit, open", NULL},
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

