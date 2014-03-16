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

//struct diag_ttystate	//OS-dependant !
//TODO : define struct diag_ttystate to include DCB and COMMTIMEOUTS

#define DL0D_INVALIDHANDLE INVALID_HANDLE_VALUE
typedef HANDLE dl0d_handletype;	//just used for casts

//diag_l0_device : some parts of this are the same for every OS.
// the platform-specific stuff should probably only be used in the associated diag_ttyXXX.c file
// diag_l0_sim.c is an offender of this : it uses "fd" for some nefarious purpose
struct diag_l0_device
{
	void *dl0_handle;					/* Handle for the L0 switch */
	const struct diag_l0 *dl0;		/* The L0 switch */
	struct diag_l2_link *dl2_link;	/* The L2 link */

	HANDLE fd;						/* File handle */
	char *name;					/* device name */
	DCB *ttystate;	/* Holds OS specific tty info; a DCB on windoze */

};



#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_TTY_WIN_H_ */
