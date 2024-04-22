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

#endif // __SCANTOOL_850_ECU_H__
