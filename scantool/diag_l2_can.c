#include "diag.h"
#include "diag_l2.h"
#include "diag_l2_can.h"

static const struct diag_l2_proto diag_l2_proto_can = {
	DIAG_L2_PROT_CAN, 0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

int diag_l2_can_add(void) {
	return diag_l2_add_protocol(&diag_l2_proto_can);
}
