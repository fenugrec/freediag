#ifndef _DIAG_L0_H_
#define _DIAG_L0_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 * (c) fenugrec 2014-2015
 * GPLv3
 */

/** FuglyBlock **/
extern struct diag_l0_node {
	const struct diag_l0 *l0dev;
	struct diag_l0_node *next;
} *l0dev_list;
/** **/

#endif // _DIAG_L0_H_
