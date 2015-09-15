#ifndef _DIAG_L0_H_
#define _DIAG_L0_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 * (c) fenugrec 2014-2015
 * GPLv3
 */


/*
 * l0dev_list : static-allocated list of supported L0 devices, since it can
 * be entirely determined at compile-time.
 * The last item is a NULL ptr to ease iterating.
 */
extern const struct diag_l0 *l0dev_list[];	/* defined in diag_config.c */

#endif // _DIAG_L0_H_
