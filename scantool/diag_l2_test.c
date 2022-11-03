/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * Copyright (C) 2017-2018 fenugrec
 *
 * Test L2 driver. Only intended for use by diag_test.c
 * to exercise low-level libdiag code paths.
 *
 */


#include "diag.h"
#include "diag_l2.h"
#include "diag_err.h"
#include "diag_os.h"

#define TEST_TIMER_DURATION 500 /** time (ms) before returning from timer callback func */


int dl2p_test_startcomms( struct diag_l2_conn *dl2c, flag_type flags,
                          unsigned int bitrate, target_type target, source_type source) {
	(void) dl2c;
	(void) flags;
	(void) bitrate;
	(void) target;
	(void) source;
	return 0;
}

/*
 */

int dl2p_test_stopcomms(struct diag_l2_conn *pX) {
	(void) pX;
	return 0;
}

int dl2p_test_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	(void) d_l2_conn;
	(void) msg;
	return diag_iseterr(DIAG_ERR_BADVAL);
}

int dl2p_test_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
                   void (*callback)(void *handle, struct diag_msg *msg), void *handle) {
	(void) d_l2_conn;
	(void) timeout;
	(void) callback;
	(void) handle;
	return diag_iseterr(DIAG_ERR_BADVAL);
}

struct diag_msg * dl2p_test_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg,
                                    int *errval) {
	(void) d_l2_conn;
	(void) msg;
	*errval = DIAG_ERR_GENERAL;
	return diag_pseterr(DIAG_ERR_BADVAL);
}

void dl2p_test_timer(struct diag_l2_conn *dl2c) {
	(void) dl2c;
	diag_os_millisleep(TEST_TIMER_DURATION);
	return;
}

const struct diag_l2_proto diag_l2_proto_test = {
	DIAG_L2_PROT_TEST,
	"TEST",
	0,
	dl2p_test_startcomms,
	dl2p_test_stopcomms,
	dl2p_test_send,
	dl2p_test_recv,
	dl2p_test_request,
	dl2p_test_timer
};
