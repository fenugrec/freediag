#ifndef _DIAG_L0_H_
#define _DIAG_L0_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 * (c) fenugrec 2014-2015
 * GPLv3
 */

#include "diag_cfg.h"	//for cfgi
#include "diag_l1.h"	//for initbus args
#include "diag_tty.h"	//for serial_settings


// diag_l0 : every diag_l0_???.c "driver" fills in one of these to describe itself.
struct diag_l0
{
	const char	*diag_l0_textname;	/* Useful textual name, unused at the moment */
	const char	*diag_l0_name;	/* Short, unique text name for user interface */

	int 	l1proto_mask;			/* supported L1protocols, defined above*/

	/* function pointers to L0 code */
	/* diag_l0_new() : create new driver instance (no open, default params, etc) */
	struct diag_l0_device *(*diag_l0_new)(void);
	/* diag_l0_getcfg() : get linked-list of config items. */
	struct cfgi* (*diag_l0_getcfg)(struct diag_l0_device *dl0d);
	/* diag_l0_del() : delete driver instance (XXX forces close ?) */
	void (*diag_l0_del)(struct diag_l0_device *);
	/* diag_l0_init() : set up global/default state of driver */
	int	(*diag_l0_init)(void);

	struct diag_l0_device *(*diag_l0_open)(const char *subinterface,
		int l1_proto);
	int	(*diag_l0_close)(struct diag_l0_device **);
	int	(*diag_l0_initbus)(struct diag_l0_device *,
		struct diag_l1_initbus_args *in);
	//diag_l0_send: return 0 on success
	int	(*diag_l0_send)(struct diag_l0_device *,
		const char *subinterface, const void *data, size_t len);
	//diag_l0_recv: ret # of bytes read
	int	(*diag_l0_recv)(struct diag_l0_device *,
		const char *subinterface, void *data, size_t len, unsigned int timeout);
	int	(*diag_l0_setspeed)(struct diag_l0_device *,
		const struct diag_serial_settings *pss);
	uint32_t	(*diag_l0_getflags)(struct diag_l0_device *);
};


/*
 * l0dev_list : static-allocated list of supported L0 devices, since it can
 * be entirely determined at compile-time.
 * The last item is a NULL ptr to ease iterating.
 */
extern const struct diag_l0 *l0dev_list[];	/* defined in diag_config.c */

#endif // _DIAG_L0_H_
