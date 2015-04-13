#ifndef _DIAG_TTY_WIN_H_
#define _DIAG_TTY_WIN_H_

/* This is of course win32-exclusive. There's actually a microscopic chance that it may work on win64 but
 * it is entirely hypothetical and untested.  This should not be included by any file except diag_tty.h
 */
#if defined(__cplusplus)
extern "C" {
#endif

#include <basetsd.h>
#include <winbase.h> //maybe just <windows.h> ?

/*
 * L0 device structure
 * This is the structure to interface between the L1 code
 * and the interface-manufacturer dependent code (which is in diag_l0_if.c)
 */

//diag_l0_device : some parts of this are the same for every OS.
//TODO : move struct diag_l0_device to a new diag_l0.h; keep OS-dependant stuff hidden in here.
// the platform-specific stuff should only be used in the associated diag_ttyXXX.c file !
// A "diag_l0_device" is a unique association between an l0 driver (diag_l0_dumb for instance)
// and a given serial port.
struct diag_l0_device
{
	void *dl0_handle;					/* Handle for the L0 switch */
	const struct diag_l0 *dl0;		/* The L0 driver's diag_l0 */
	struct diag_l2_link *dl2_link;	/* The L2 link using this dl0d */
	char *name;					/* device name, like /dev/ttyS0 or \\.\COM3*/
//OS-dependant members:
	HANDLE fd;						/* File handle */
	DCB *ttystate;	/* Holds OS specific tty info; a DCB on windoze */

};



#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_TTY_WIN_H_ */
