/* freediag
 * (c) fenugrec 2015
 * GPLv3
 */

/* common L0 code */

#include <stdlib.h>

#include "diag.h"
#include "diag_l0.h"

/** Alloc new dl0d and set ->dl0 and ->l0_int members
 */
struct diag_l0_device *diag_l0_new(const struct diag_l0 *dl0, void *l0_int) {
	struct diag_l0_device *dl0d;
	int rv;

	rv=diag_calloc(&dl0d, 1);
	if (rv)
		return diag_pseterr(rv);

	dl0d->dl0 = dl0;
	dl0d->l0_int = l0_int;
	return dl0d;
}

/** Free a dl0d, a bit pointless
*/
void diag_l0_del(struct diag_l0_device *dl0d) {
	if (dl0d) free(dl0d);
	return;
}
