/* freediag
 * (c) fenugrec 2015
 * GPLv3
 */

/* common L0 code */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_l0.h"

int diag_l0_debug;	//debug flags for l0

int diag_l0_open(struct diag_l0_device *dl0d, int l1proto) {
	return dl0d->dl0->_open(dl0d, l1proto);
}

void diag_l0_close(struct diag_l0_device *dl0d) {
	dl0d->dl0->_close(dl0d);
}

struct diag_l0_device *diag_l0_new(const char *shortname) {
	struct diag_l0_device *dl0d;
	int rv;
	const struct diag_l0 *l0dev;
	int i;

	for (i=0; l0dev_list[i]; i++) {
		l0dev = l0dev_list[i];
		if (strcmp(shortname, l0dev->shortname) == 0) {
			/* Found it */
			break;
		}
	}

	if (!l0dev_list[i]) {
		//not found !
		return NULL;
	}

	rv=diag_calloc(&dl0d, 1);
	if (rv) {
		return diag_pfwderr(rv);
	}

	dl0d->dl0 = l0dev;
	rv = l0dev->_new(dl0d);
	if (rv != 0) {
		free(dl0d);
		return diag_pfwderr(rv);
	}

	return dl0d;
}


void diag_l0_del(struct diag_l0_device *dl0d) {
	if (!dl0d) {
		return;
	}

	assert(!dl0d->opened);

	dl0d->dl0->_del(dl0d);
	free(dl0d);
	return;
}

struct cfgi *diag_l0_getcfg(struct diag_l0_device *dl0d) {
	if (!dl0d) {
		return NULL;
	}

	if (dl0d->dl0->_getcfg) {
		return dl0d->dl0->_getcfg(dl0d);
	}
	return NULL;
}

uint32_t diag_l0_getflags(struct diag_l0_device *dl0d) {
	assert(dl0d);
	return dl0d->dl0->_getflags(dl0d);
}

int diag_l0_recv(struct diag_l0_device *dl0d, void *data, size_t len, unsigned int timeout) {
	assert(dl0d);
	return dl0d->dl0->_recv(dl0d, data, len, timeout);
}

int	diag_l0_send(struct diag_l0_device *dl0d, const void *data, size_t len) {
	assert(dl0d);
	return dl0d->dl0->_send(dl0d, data, len);
}

int diag_l0_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	assert(dl0d);

	return dl0d->dl0->_ioctl(dl0d, cmd, data);
}

