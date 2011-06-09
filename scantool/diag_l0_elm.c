/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * Diag, Layer 0, interface for Scantool.net's ELM32x Interface
 * Ken Bantoft <ken@bantoft.org>
 *
 * This is currently just cut/copy/pasted from the sileng driver, and
 * will not work yet.
 *
 *This is meant to support ELM323 & 327 devices. For now, only 9600 comms
 *will be supported. See diag_l0_elm_setspeed
 *
 *This interface is particular in that it handles the header bytes + checksum internally.
 *Data is transferred in ASCII hex format, i.e. 0x46 0xFE is sent and received as "46FE"
 *The ELM327 has non-volatile settings; this will require special treatment. Not
 * implemented at the moment: the _open function will reset factory settings.
 *These devices handle the periodic wake-up commands on the bus. This is the only l0
 *device with that feature; upper levels (esp L2) need to be modified to take this into account. See
 *diag_l3_saej1979.c : diag_l3_j1979_timer() etc.
 */


#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <errno.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

CVSID("$Id$");

#define ELM_BUFSIZE 25	//longest data to be received during init is the version string, ~ 15 bytes. 25 should be plenty for all other situations

struct diag_l0_elm_device {
	int protocol;
	struct diag_serial_settings serial;
	//a couple of flags could be added here, like ELM323 / ELM327; packed_data; etc.
};

const char * elm_errors[]={"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?"};

/* Global init flag */
static int diag_l0_elm_initdone;

extern const struct diag_l0 diag_l0_elm;

static int diag_l0_elm_send(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)), const void *data, size_t len);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_elm_init(void)
{
	if (diag_l0_elm_initdone)
		return 0;
	diag_l0_elm_initdone = 1;

	/* Do required scheduling tweeks */
	diag_os_sched();

	return 0;
}

static int
diag_l0_elm_close(struct diag_l0_device **pdl0d)
{
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_elm_device *dev =
			(struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

		/* If debugging, print to strerr */
		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n",
				FL, dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

	return 0;
}


//Send a command to ELM device and make sure no error occured. Data is passed on directly as a string; caller must make sure the string is \n-terminated.
//Or rather, 0x0D-terminated. 0x0A is not required with ELM
//Currently the code is dup'ed from _elm_send , but this will be changed so that _elm_send calls _sendcmd smartly.
//returns 0 on success
//_sendcmd should not be called from outside diag_l0_elm.c
static int
diag_l0_elm_sendcmd(struct diag_l0_device *dl0d, const void *data, size_t len, int timeout)
{
	ssize_t xferd;

	if (((char *) data)[len-1] != 0x0D) {
		//Last byte is not a carriage return, this would die.
		fprintf(stderr, FLFMT "Error: trying to send non-terminated command %.*s\n", FL, len, data);
		//the %.*s is pure magic : limits the string length to len, even if the string is not null-terminated.
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "sending command to ELM:\n\"%.*s\"", FL, len, data);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			diag_data_dump(stderr, data, len);
		}
	}

	while ((size_t)(xferd = diag_tty_write(dl0d, data, len)) != len) {
		/* Partial write */
		if (xferd <  0) {	//write error
			/* error */
			if (errno != EINTR) {	//not an interruption
				perror("write");
				fprintf(stderr, FLFMT "write returned error %d\n", FL, errno);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			xferd = 0; /* Interrupted write, nothing transferred. */
		}
		/*
		 * Successfully wrote xferd bytes (or 0 && EINTR),
		 * so inc pointers and continue
		 */
		len -= xferd;
		data = (const void *)((const char *)data + xferd);
	}
	

	return 0;
	
}
/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 */
static struct diag_l0_device *
diag_l0_elm_open(const char *subinterface, int iProtocol) 
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_elm_device *dev;
	struct diag_serial_settings sset;
	char * buf[ELM_BUFSIZE];

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_elm_init();

	if (rv=diag_calloc(&dev, 1))
		return (struct diag_l0_device *)diag_pseterr(rv);

	dev->protocol = iProtocol;

	if (rv=diag_tty_open(&dl0d, subinterface, &diag_l0_elm, (void *)dev))
		return (struct diag_l0_device *)diag_pseterr(rv);
	
	//set speed to 9600;8n1. XXX Perhaps this could be included with the diag_tty_open call above ?
	sset.speed=9600;
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;

	dev->serial = sset;

	if (rv=diag_iseterr(diag_tty_setup(dl0d, &sset))) {
		fprintf(stderr, FLFMT "Error setting 9600;8N1 on %s\n",
			FL, subinterface);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */
	
	//At this stage, the ELM has possibly been powered up for a while;
	//sending the command "ATZ" will cause a full reset and the chip will send a
	//welcome string like "ELM323 v2.0\n>" or "ELM327 v1.4b\n>"
	//the device string may be checked but the most important is the > character as
	//it indicates the device's readiness.
	//The following options are also set :
	//ATE0   (disable echo)
	//
	
	*buf="ATZ\n";
	if (diag_l0_elm_send(dl0d, subinterface, buf, 4)) {
		if (diag_l0_debug&DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATZ\" failed", FL);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
	}
	//next : read back welcome string. Max 250ms to complete... tentative guess
	rv=diag_tty_read(dl0d, buf, 20, 250);
	if (rv<1) {
		//no data or error
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "ELM reset readback failed\n", FL);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_TIMEOUT);
	}
	*buf[rv]=0;	//terminate string
	if (*buf[rv-1] != '>') {
		//if last character isn't the input prompt, there is a problem
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "ELM not ready; received %s", FL, buf);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_TIMEOUT);
	}
	//else : correct prompt received:
	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM reset success : %s\n", FL, buf);
	}
	//now send "ATE0\n" command to disable echo.
	*buf="ATE0\n";
	if (diag_l0_elm_send(dl0d, subinterface, &buf, 5)) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATE0\" failed", FL);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
	}
	//again wait for prompt
	rv=diag_tty_read(dl0d, &buf, 20, 250);
	if (rv<1) {
		//no data or error
		rv=1;
		buf[0]=NULL;
		//this is just to fall in the next "if"... testing for (rv<1 || buf[rv-1] != '>') could
		//have unpredictable results if rv is negative
	}
	*buf[rv]=0;		//terminate string
	if (*buf[rv-1] != '>') {
		//if last character isn't the input prompt, there is a problem
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "ELM setup failed; received %s\n", FL, &buf);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
	}
	//at this point : ELM is ready for further ops
	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM ready: %s\n", FL, buf);
	}
	return dl0d;
}


/*
 * Fastinit:
 * ELM claims it will deal with slow/fast init automatically. Until the code from levels L1 and up can deal with this,
 * the bus will manually be initialized.
 */
#ifdef WIN32
static int
diag_l0_elm_fastinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in)
#else
static int
diag_l0_elm_fastinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in __attribute__((unused)))
#endif
{
	int rv;
	char * buf="ATFI\n";
	
	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p fastinit\n", FL, dl0d);

	//send command with 1000ms timeout (guess). This should be enough for 14230 inits
	if (diag_l0_elm_sendcmd(dl0d, &buf, 5, 1000)) {
		fprintf(stderr, FLFMT "sending ATFI failed", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}

/*
 * Slowinit:
 * ELM takes care of the details & timing.
 */
static int
diag_l0_elm_slowinit(struct diag_l0_device *dl0d,
struct diag_l1_initbus_args *in, struct diag_l0_elm_device *dev)
{
	char cbuf[MAXRBUF];
	int xferd, rv;
	int tout;
	struct diag_serial_settings set;

	if (diag_l0_debug & DIAG_DEBUG_PROTO) {
		fprintf(stderr, FLFMT "slowinit link %p address 0x%x\n",
			FL, dl0d, in->addr);
	}

	//XXX insert ELM commands here
	return 0;

}

/*
 * Do wakeup on the bus
 */
static int
diag_l0_elm_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct diag_l0_elm_device *dev;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d\n",
			FL, dl0d, dev, in->type);

	if (!dev)
		return -1;

	
	/* Wait the idle time (Tidle > 300ms) */
	diag_tty_iflush(dl0d);	/* Flush unread input */
	diag_os_millisleep(300);

	switch (in->type) 	{
	case DIAG_L1_INITBUS_FAST:
		rv = diag_l0_elm_fastinit(dl0d, in);
		break;
	case DIAG_L1_INITBUS_5BAUD:
		rv = diag_l0_elm_slowinit(dl0d, in, dev);
		break;
	default:
		rv = DIAG_ERR_INIT_NOTSUPP;
		break;
	}

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "initbus device link %p returning %d\n",
			FL, dl0d, rv);
	
	return rv;

}



/*
 * Send a load of data
 *
 * Returns 0 on success, -1 on failure
 */
#ifdef WIN32
static int
diag_l0_elm_send(struct diag_l0_device *dl0d,
const char *subinterface,
const void *data, size_t len)
#else
static int
diag_l0_elm_send(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
const void *data, size_t len)
#endif
{
	ssize_t xferd;

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "device link %p send %d bytes ",
			FL, dl0d, (int)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			diag_data_dump(stderr, data, len);
		}
	}

	while ((size_t)(xferd = diag_tty_write(dl0d, data, len)) != len) {
		/* Partial write */
		if (xferd <  0) {	//write error
			/* error */
			if (errno != EINTR) {	//not an interruption
				perror("write");
				fprintf(stderr, FLFMT "write returned error %d !!\n", FL, errno);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
			xferd = 0; /* Interrupted write, nothing transferred. */
		}
		/*
		 * Successfully wrote xferd bytes (or 0 && EINTR),
		 * so inc pointers and continue
		 */
		len -= xferd;
		data = (const void *)((const char *)data + xferd);
	}
	if ( (diag_l0_debug & (DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA)) ==
			(DIAG_DEBUG_WRITE|DIAG_DEBUG_DATA) ) {
		fprintf(stderr, "\n");
	}

	return 0;
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 */
#ifdef WIN32
static int
diag_l0_elm_recv(struct diag_l0_device *dl0d,
const char *subinterface,
void *data, size_t len, int timeout)
#else
static int
diag_l0_elm_recv(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)),
void *data, size_t len, int timeout)
#endif
{
	int xferd;

	struct diag_l0_elm_device *dev;
	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %d bytes timeout %d",
			FL, dl0d, (int)len, timeout);

	while ( (xferd = diag_tty_read(dl0d, data, len, timeout)) <= 0) {
		if (xferd == DIAG_ERR_TIMEOUT) {
			if (diag_l0_debug & DIAG_DEBUG_READ)
				fprintf(stderr, "\n");
			return diag_iseterr(DIAG_ERR_TIMEOUT);
		}
		if (xferd == 0) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return -1;
		}
		if (errno != EINTR) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return -1;
		}
	}
	if (diag_l0_debug & DIAG_DEBUG_READ) {
		diag_data_dump(stderr, data, (size_t)xferd);
		fprintf(stderr, "\n");
	}
	return xferd;
}

/*
 * Set speed/parity
* but upper levels shouldn't be doing this. This function will force 9600;8N1
 */
static int
diag_l0_elm_setspeed(struct diag_l0_device *dl0d,
const struct diag_serial_settings *pss)
{
	fprintf(stderr, FLFMT "Warning: attempted to override serial settings. 9600;8N1 maintained\n", FL);
	struct diag_serial_settings sset;
	sset.speed=9600;
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;
	struct diag_l0_elm_device *dev;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = sset;

	return diag_iseterr(diag_tty_setup(dl0d, &sset));
}

static int
diag_l0_elm_getflags(struct diag_l0_device *dl0d)
{

	struct diag_l0_elm_device *dev;
	int flags=0;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	//XXX will have to add specific 323 vs 327 handling. For now, only 323 features are included.
	switch (dev->protocol) {
	case DIAG_L1_J1850_VPW:
	case DIAG_L1_J1850_PWM:
			break;
	case DIAG_L1_ISO9141:
			flags = DIAG_L1_SLOW | DIAG_L1_DOESL2FRAME | DIAG_L1_DOESL2CKSUM;
			flags |= DIAG_L1_DOESP4WAIT | DIAG_L1_STRIPSL2CKSUM;
			break;
	case DIAG_L1_ISO14230:
			flags = DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
			flags |= DIAG_L1_DOESL2FRAME | DIAG_L1_DOESL2CKSUM;
			flags |= DIAG_L1_DOESP4WAIT | DIAG_L1_STRIPSL2CKSUM;
			break;
	}

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr,
			FLFMT "getflags link %p proto %d flags 0x%x\n",
			FL, dl0d, dev->protocol, flags);

	return flags;

}

const struct diag_l0 diag_l0_elm = {
	"Scantool.net ELM32x Chipset Device",
	"ELM",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	diag_l0_elm_init,
	diag_l0_elm_open,
	diag_l0_elm_close,
	diag_l0_elm_initbus,
	diag_l0_elm_send,
	diag_l0_elm_recv,
	diag_l0_elm_setspeed,
	diag_l0_elm_getflags
};

#if defined(__cplusplus)
extern "C" {
#endif
extern int diag_l0_elm_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_elm_add(void)
{
	return diag_l1_add_l0dev(&diag_l0_elm);
}
