#ifndef _DIAG_L0_H_
#define _DIAG_L0_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 * (c) fenugrec 2014-2015
 * GPLv3
 */

#include "diag_cfg.h"	//for cfgi

/* Cheats for structs defined elsewhere;
 * the alternate solution of including diag_l*.h gets messy fast
 * because of inter-dependencies
 */

struct diag_l2_link;
struct diag_l1_initbus_args;
struct diag_serial_settings;
struct diag_l0;

/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_<if>.c)
 * A "diag_l0_device" is a unique association between an l0 driver (diag_l0_dumb for instance)
 * and a given serial port.
 */
struct diag_l0_device
{
	void *l0_int;					/** Handle for internal L0 data */
	const struct diag_l0 *dl0;		/** The L0 driver's diag_l0 */
	struct diag_l2_link *dl2_link;	/** The L2 link using this dl0d */
	//char *name;					/** XXX MOVED TO TTY INTERNAL device name, like /dev/ttyS0 or \\.\COM3 */
	void *tty_int;			/** generic holder for internal tty stuff */
};


// diag_l0 : every diag_l0_???.c "driver" fills in one of these to describe itself.
struct diag_l0
{
	const char	*diag_l0_textname;	/* Useful textual name, unused at the moment */
	const char	*diag_l0_name;	/* Short, unique text name for user interface */

	int 	l1proto_mask;			/** supported L1protocols, defined in  diag_l1.h */

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
	/** Close diag_l0_device
	 *
	 * Does not free the struct itself, to allow reuse.
	 */
	void	(*diag_l0_close)(struct diag_l0_device *);
	int	(*diag_l0_initbus)(struct diag_l0_device *,
		struct diag_l1_initbus_args *in);

	/** Send bytes.
	 * @return 0 on success
	 */
	int	(*diag_l0_send)(struct diag_l0_device *,
		const char *subinterface, const void *data, size_t len);

	/** Receive bytes.
	 * @param timeout: in ms
	 * @return # of bytes read
	 */
	int	(*diag_l0_recv)(struct diag_l0_device *,
		const char *subinterface, void *data, size_t len, unsigned int timeout);
	int	(*diag_l0_setspeed)(struct diag_l0_device *,
		const struct diag_serial_settings *pss);

	/** Get L0 device flags
	 * @return bitmask of flags defined in diag_l1.h
	 */
	uint32_t	(*diag_l0_getflags)(struct diag_l0_device *);
};



/** Public funcs **/
struct diag_l0_device *diag_l0_new(const struct diag_l0 *dl0, void *l0_int);
void diag_l0_del(struct diag_l0_device *dl0d);


/** globals **/

extern int diag_l0_debug;	// debug flags; defined in l1.c (TODO : move to l0 somewhere)


/*
 * l0dev_list : static-allocated list of supported L0 devices, since it can
 * be entirely determined at compile-time.
 * The last item is a NULL ptr to ease iterating.
 */
extern const struct diag_l0 *l0dev_list[];	/* defined in diag_config.c */

#endif // _DIAG_L0_H_
