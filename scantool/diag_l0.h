#ifndef _DIAG_L0_H_
#define _DIAG_L0_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 * (c) fenugrec 2014-2015
 * GPLv3
 */

#if defined(__cplusplus)
extern "C" {
#endif

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
 * and the interface-manufacturer dependent code (in diag_l0_<if>.c)
 * A "diag_l0_device" is a unique association between an l0 driver (diag_l0_dumb for instance)
 * and a hardware resource (serial port, file, etc.)
 */
struct diag_l0_device {
	void *l0_int;					/** Handle for internal L0 data */
	const struct diag_l0 *dl0;		/** The L0 driver's diag_l0 */

	bool opened;		/** L0 status */
};


// diag_l0 : every diag_l0_???.c "driver" fills in one of these to describe itself.
struct diag_l0 {
	const char	*longname;	/* Useful textual name, unused at the moment */
	const char	*shortname;	/* Short, unique text name for user interface */

	int 	l1proto_mask;			/** supported L1protocols, defined in  diag_l1.h */

	/** set up global/default state of driver, if applicable
	 *
	 * @return 0 if ok
	 *
	 * Note: the implementation must not do any dynamic mem operation (*alloc etc) or open handles.
	 * That way we won't need to add an _end function.
	 */
	int (*init)(void);


	/*** Private funcs, do not call directly ! ***/
	/* These are called from the "public funcs" listed below. */

	int (*_new)(struct diag_l0_device *);
	struct cfgi *(*_getcfg)(struct diag_l0_device *);
	void (*_del)(struct diag_l0_device *);
	int (*_open)(struct diag_l0_device *, int l1_proto);
	void (*_close)(struct diag_l0_device *);
	uint32_t	(*_getflags)(struct diag_l0_device *);

	int	(*_recv)(struct diag_l0_device *, void *data, size_t len, unsigned int timeout);
	int	(*_send)(struct diag_l0_device *, const void *data, size_t len);

	int (*_ioctl)(struct diag_l0_device *, unsigned cmd, void *data);
};



/***** Public funcs *****/

/** Alloc new dl0d and call L0's "_new";
 * (no open, default params, etc)
 * @return 0 if ok */
struct diag_l0_device *diag_l0_new(const char *shortname);


/** Get linked-list of config items.
 * @return NULL if no items exist */
struct cfgi *diag_l0_getcfg(struct diag_l0_device *);


/** Delete L0 driver instance.
 *
 * Caller MUST have closed it first with diag_l0_close !
 * Opposite of diag_l0_new()
 */
void diag_l0_del(struct diag_l0_device *);


/** Open an L0 device with a given L1 proto
 *
 * @return 0 if ok
 *
 * L0 device must have been created with diag_l0_new() first
 */
int diag_l0_open(struct diag_l0_device *, int l1protocol);


/** Close diag_l0_device
 *
 * Does not free the struct itself, to allow reuse.
 */
void diag_l0_close(struct diag_l0_device *);


/** Get L0 device flags
 * @return bitmask of flags defined in diag_l1.h
 */
uint32_t	diag_l0_getflags(struct diag_l0_device *);


/** Receive bytes.
 * @param timeout: in ms
 * @return # of bytes read
 */
int diag_l0_recv(struct diag_l0_device *, void *data, size_t len, unsigned int timeout);


/** Send bytes.
 * @return 0 on success
 */
int	diag_l0_send(struct diag_l0_device *, const void *data, size_t len);

/** Send IOCTL to L0
 *	@param command : IOCTL #, defined in diag.h
 *	@param data	optional, input/output
 *	@return 0 if OK, diag error num (<0) on error
 */
int diag_l0_ioctl(struct diag_l0_device *, unsigned cmd, void *data);


/*** globals ***/

extern int diag_l0_debug;	// debug flags


/*
 * l0dev_list : static-allocated list of supported L0 devices, since it can
 * be entirely determined at compile-time.
 * The last item is a NULL ptr to ease iterating.
 */
extern const struct diag_l0 *l0dev_list[];	/* defined in diag_config.c */

#if defined(__cplusplus)
}
#endif
#endif // _DIAG_L0_H_
