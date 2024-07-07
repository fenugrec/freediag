#include <stdint.h>

#ifndef __SCANTOOL_850_DTC_H__
#define __SCANTOOL_850_DTC_H__

struct dtc_table_entry {
	uint8_t raw_value;
	uint16_t dtc_suffix;
	const char *desc;
	const char *tips;
};

struct ecu_dtc_table_map_entry {
	uint8_t ecu_addr;
	// NULL terminated
	const struct dtc_table_entry *dtc_table;
};

extern const struct ecu_dtc_table_map_entry ecu_dtc_map[];

/** find DTC table by ECU address. NULL if not found */
const struct dtc_table_entry *dtctable_by_addr(uint8_t addr);

#endif // __SCANTOOL_850_DTC_H__

