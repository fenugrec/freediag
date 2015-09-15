#include "diag.h"
#include "diag_l2.h"
#include "diag_l2_can.h"

const struct diag_l2_proto diag_l2_proto_can = {
	DIAG_L2_PROT_CAN,
	"CAN",
	0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};
