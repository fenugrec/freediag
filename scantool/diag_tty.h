#ifndef _DIAG_TTY_H_
#define _DIAG_TTY_H_

// Included the correct os-specific diag_tty.h file
#ifdef WIN32
	#include "diag_tty_win.h"
#else
	#include "diag_tty_unix.h"
#endif //WIN32

#endif /* _DIAG_TTY_H_ */
