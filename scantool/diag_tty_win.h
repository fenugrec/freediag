/* This is of course win32-exclusive. There's actually a microscopic chance that it may work on win64 but
 * it is entirely hypothetical and untested.  This should not be included by any file except diag_tty_win.c
 */
#ifndef _DIAG_TTY_WIN_H_
#define _DIAG_TTY_WIN_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "diag_tty.h"

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_TTY_WIN_H_ */
