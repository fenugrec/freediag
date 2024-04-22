#ifndef __SCANTOOL_850_ECU_H__
#define __SCANTOOL_850_ECU_H__

#include <stdint.h>

struct ecu_info {
	uint8_t addr;
	const char *name;
	const char *desc;
	const char *dtc_prefix;
};

extern const struct ecu_info ecu_list[];

/* find ECU info by address; NULL if not found */
const struct ecu_info *ecu_info_by_addr(uint8_t addr);

/** Look up an ECU by name; NULL if not found
 */
const struct ecu_info *ecu_info_by_name(const char *name);


#endif // __SCANTOOL_850_ECU_H__

