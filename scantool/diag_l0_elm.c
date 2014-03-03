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
 * Work in progress, will not work yet. 
 *
 *This is meant to support ELM323 & 327 devices. For now, only 9600 comms
 *will be supported. See diag_l0_elm_setspeed
 *
 *This interface is particular in that it handles the header bytes + checksum internally.
 *Data is transferred in ASCII hex format, i.e. 0x46 0xFE is sent and received as "46FE"
 *
 *The ELM327 has non-volatile settings; this will require special treatment. Not
 * implemented at the moment -> the _open function will reset factory settings.
 *
 *These devices handle the periodic wake-up commands on the bus. This is the only l0
 *device with that feature; upper levels (esp L2) need to be modified to take this into account. See
 *diag_l3_saej1979.c : diag_l3_j1979_timer() etc.
 *
 *For now, fast / slow init uses default addressing. This should be fixed someday
 */


#ifdef WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

CVSID("$Id$");

#define ELM_BUFSIZE 32	//longest data to be received during init is the version string, ~ 15 bytes, plus possible command echo. OBD data is 7 bytes, received as a 23-char string. 32 should be enough...

struct diag_l0_elm_device {
	int protocol;
	struct diag_serial_settings serial;
	//a couple of flags could be added here, like ELM323 / ELM327; packed_data; etc.
};

// possible error messages returned by ELM323. ELM327 messages should be defined separately ?
const char * elm_errors[]={"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?", NULL};

/* Global init flag */
static int diag_l0_elm_initdone=0;		//1=init done

extern const struct diag_l0 diag_l0_elm;

static int diag_l0_elm_send(struct diag_l0_device *dl0d,
const char *subinterface __attribute__((unused)), const void *data, size_t len);

void elm_parse_cr(char *data, int len);	//change 0x0A to 0x0D

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


//Send a command to ELM device and make sure no error occured. Data is passed on directly as a string;
//caller must make sure the string is \r-terminated - more precisely, 0x0D-terminated. 0x0A is ignored by ELM.
//This func should not be used for data that elicits a data response (i.e. all data destined to the OBD bus,
//hence not prefixed by "AT")
//returns 0 on success. Waits *at least* timeout for all data & responses to come in. Responses to commands
//are not verified in detail; only the prompt character ">" must be present at the end of the data.
//_sendcmd should not be called from outside diag_l0_elm.c;
static int
diag_l0_elm_sendcmd(struct diag_l0_device *dl0d, const char *data, size_t len, int timeout)
{
	ssize_t xferd;
	int i, rpos, rv;
	char buf[ELM_BUFSIZE];	//for receiving responses
	
	if (data[len-1] != 0x0D) {
		//Last byte is not a carriage return, this would die.
		fprintf(stderr, FLFMT "Error: attempting to send non-terminated command %.*s\n", FL, (int) len, data);
		//the %.*s is pure magic : limits the string length to len, even if the string is not null-terminated.
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "sending command to ELM: %.*s\n", FL, (int) len-1, data);
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
		//data = (const void *)((const char *)data + xferd);
		data += xferd;
	}
	
	//next, receive ELM response, after {ms} delay.
	diag_os_millisleep(timeout);

	rv=diag_tty_read(dl0d, buf, ELM_BUFSIZE-5, 100);	//rv=# bytes read
	if (diag_l0_debug & DIAG_DEBUG_WRITE || diag_l0_debug & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "sent %d bytes\n", FL, xferd);
		fprintf(stderr, FLFMT "received %d bytes\n", FL, rv);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			elm_parse_cr(buf, rv);
			fprintf(stderr, FLFMT "(got %.*s)\n", FL, rv, buf);
		}
	}
	
	
	if (rv<1) {
		//no data or error
		if (diag_l0_debug & DIAG_DEBUG_WRITE) {
			fprintf(stderr, FLFMT "ELM did not respond\n", FL);
		}
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	buf[rv]=0;	//terminate string
	if (buf[rv-1] != '>') {
		//if last character isn't the input prompt, there is a problem
		if (diag_l0_debug & DIAG_DEBUG_WRITE) {
			fprintf(stderr, FLFMT "ELM not ready (no prompt received)\n", FL);
		}
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//At this point we did get a prompt but there may have been an error message; 
	//first : search for the last 0x0D / 0x0A before the prompt.
	rpos=rv-1;
	while (rpos>0) {
		rpos--;
		if ((buf[rpos] == 0x0D) || (buf[rpos] ==0x0A))
			break;
	}
	if ((rv - rpos) > 3) {
		//there is probably a message between the last CR/LF and the prompt. Find possible error message:
		for (i=0; elm_errors[i] != NULL; i++) {
			if (strncmp(&buf[rpos], elm_errors[i], (rv - 1) - rpos) == 0) {
				//error message found :
				fprintf(stderr, FLFMT "ELM returned error : %s\n", FL, elm_errors[i]);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}
		//no match found, but there was "something" before '>'. Display this.
		fprintf(stderr, FLFMT "Warning: unrecognized response before prompt : %s\n", FL, buf);
		fprintf(stderr, FLFMT "This is probably a bug, please report !\n", FL);
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
	const char *buf;	//[ELM_BUFSIZE];

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

	if (rv=diag_tty_setup(dl0d, &sset)) {
		fprintf(stderr, FLFMT "Error setting 9600;8N1 on %s\n",
			FL, subinterface);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */
	
	//At this stage, the ELM has possibly been powered up for a while;
	//sending the command "ATZ" will cause a full reset and the chip will send a
	//welcome string like "ELM323 v2.0\n>" or "ELM327 v1.4b\n>" or "ELM327 1.5a\n" for certain clones.
	//the device string may be checked but the most important is the > character as
	//it indicates the device's readiness.
	//The following options are also set :
	//ATE0   (disable echo)
	//
	
	buf="ATZ\x0D";
	if (diag_l0_elm_sendcmd(dl0d, buf, 4, 1000)) {
		if (diag_l0_debug&DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATZ\" failed\n", FL);
		}
		diag_tty_close(&dl0d);
		free(dev);
		dev=NULL;
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}
	
	//Correct prompt received:
	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM reset success : %s\n", FL, buf);
	}
	
	//now send "ATE0\n" command to disable echo.
	buf="ATE0\x0D";
	if (diag_l0_elm_sendcmd(dl0d, buf, 5, 500)) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATE0\" failed\n", FL);
		}
		diag_l0_elm_close(&dl0d);
		free(dev);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}

	//at this point : ELM is ready for further ops
	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM ready: %s\n", FL, buf);
	}
	return dl0d;
}


/*
 * Fastinit & slowinit:
 * ELM claims it will deal with slow/fast init automatically. Until the code from levels L1 and up can deal with
 * this, the bus will manually be initialized.
 * Only callable internally. Not for export !!
 * Unsupported by some clones (they handle the init internally, on demand)
 */
static int
diag_l0_elm_fastinit(struct diag_l0_device *dl0d)
{
	char * cmds="ATFI\x0D";
	
	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr, FLFMT "ELM forced fastinit...\n", FL);

	//send command with 1000ms timeout (guessing)
	if (diag_l0_elm_sendcmd(dl0d, cmds, 5, 1000)) {
		fprintf(stderr, FLFMT "Command ATFI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}


static int
diag_l0_elm_slowinit(struct diag_l0_device *dl0d)
{
	char * cmds="ATSI\x0D";

	if (diag_l0_debug & DIAG_DEBUG_PROTO) {
		fprintf(stderr, FLFMT "ELM forced slowinit...\n", FL);
	}

	//huge timeout of 3.5s. Not sure if this is adequate
	if (diag_l0_elm_sendcmd(dl0d, cmds, 5, 3500)) {
		fprintf(stderr, FLFMT "Command ATSI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;

}

/*
 * Do wakeup on the bus
 * For now, the address specified in initbus_args is ignored. XXX This should be fixed with a ATSR command, probably ?
 * ELM luckily has adequate default addresses for most cases.
 */
static int
diag_l0_elm_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	int rv = DIAG_ERR_INIT_NOTSUPP;
	int is_clone=1;		//eventually configurable ? Or auto-detected?

	struct diag_l0_elm_device *dev;

	fprintf(stderr, FLFMT "Note : ELM clones do not support explicit bus initialization.\n", FL);
	fprintf(stderr, FLFMT "Errors are therefore ignored.\n", FL);

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "ELM initbus type %d\n",
			FL, in->type);

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	if (!dev)
		return diag_iseterr(rv);
	
	/* Wait the idle time (Tidle > 300ms) */
	/* Although this is probably already taken care of in ELM's firmware*/
	diag_os_millisleep(300);

	switch (in->type) {
		case DIAG_L1_INITBUS_FAST:
			rv = diag_l0_elm_fastinit(dl0d);
			break;
		case DIAG_L1_INITBUS_5BAUD:
			rv = diag_l0_elm_slowinit(dl0d);
			break;
		default:
			rv = DIAG_ERR_INIT_NOTSUPP;
			break;
	}

	if (rv && !is_clone)
	//XXX ignore errors if device is a clone...
	//if (rv)
		return diag_iseterr(rv);
	//XXX Hax ! XXX
	//global_state = STATE_CONNECTED;
	//since ELM handles the keybytes and the rest of the formalities, some flags will be needed to skip the checks
	//carried out by the upper levels (diag_l2_iso9141.c:_startcomms, etc.)
	//If a correct prompt was received, the ELM is almost certainly in ready state.
	return 0;

}



/*
 * Send a load of data
 *
 * Directly send hex-ASCII; exit without receiving response.
 * Upper levels don't append 0x0D / 0x0A at the end, we take care of adding the required 0x0D.
 * "ATx" commands sould not be sent with this function.
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
	char buf[ELM_BUFSIZE];
	size_t xferd;
	int i;
	
	if ((2*len)>(ELM_BUFSIZE-1)) {
		//too much data for buffer size
		fprintf(stderr, FLFMT "ELM: too much data for buffer (report this bug please!)\n", FL);
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "ELM: sending %d bytes \n", FL, (int) len);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			diag_data_dump(stderr, data, len);
		}
	}

	for (i=0; i<len; i++) {
		//fill buffer with ascii-fied hex data
		snprintf(&buf[2*i], 3, "%02x", (size_t) ((char *)data)[i]);
	}
	i=2*len;
	buf[i]=0x0D;
	buf[i+1]=0x00;	//terminate string
	
	if ((diag_l0_debug & DIAG_DEBUG_WRITE) && (diag_l0_debug & DIAG_DEBUG_DATA)) {
		fprintf(stderr, FLFMT "ELM: sending %s\n", FL, buf);
	}
	
	while ((size_t)(xferd = diag_tty_write(dl0d, buf, i+1)) != i+1) {
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
		data = (const void *)((const char *)buf + xferd);
	}

	return 0;
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 * ELM returns a string with format "%02x %02x %02x[...]\n" . 
 * We convert this received ascii string to hex before returning.
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
	char rxbuf[ELM_BUFSIZE];		//need max (7 digits *2chars) + (6 space *1) + 1(CR) + 1(>) + (NUL) bytes.

	//possibly not useful until ELM323 vs 327 distinction is made :
	//struct diag_l0_elm_device *dev;
	//dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "Expecting %d bytes from ELM, %d ms timeout\n", FL, (int) len, timeout);

	while ( (xferd = diag_tty_read(dl0d, rxbuf, len, timeout)) <= 0) {
		if (xferd == DIAG_ERR_TIMEOUT) {
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
		diag_data_dump(stderr, &rxbuf, (size_t)xferd);
		fprintf(stderr, "\n");
	}
	
	//Here, rxbuf contains the string received from ELM. Parse it to get hex digits
	char *rptr, *bp;
	unsigned int rbyte;
	xferd=0;
	rptr=rxbuf+strspn(rxbuf, " \n\r");	//skip all leading spaces and linefeeds
	while ((bp=strtok(rptr, " >\n\r")) !=NULL) {
		//process token delimited by spaces or prompt character
		//this is very sketchy and deserves to be tested more...
		sscanf(bp, "%02x", &rbyte);
		((char *)data)[xferd]=(char) rbyte;
		xferd++;
		if (xferd==len)
			break;	
		//printf("%s\t0x%02x\n", bp, i);
		rptr=NULL;
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
	int rv;
	fprintf(stderr, FLFMT "Warning: attempted to override serial settings. 9600;8N1 maintained\n", FL);
	struct diag_serial_settings sset;
	sset.speed=9600;
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;
	struct diag_l0_elm_device *dev;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = sset;

	if (rv=diag_tty_setup(dl0d, &sset))
		return diag_iseterr(rv);

	return 0;
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

//elm_parse_cr :  change 0x0A to 0x0D in datastream.
void elm_parse_cr(char *data, int len) {
	int i=0;
	for (;i<len; i++) {
		if (*data==0x0D)
			*data=0x0A;
		data++;
	}
	return;
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
