/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * (c) fenugrec / CSB 2011-2014
 * Diag, Layer 0, interface for Scantool.net's ELM32x Interface
 * Work in progress, will not work yet.
 *
 *This is meant to support ELM323 & 327 devices; clones and originals.
 * COM speed is "autodetected" (it tries 9600 and 38400bps).
 *
 *ELM interfaces are particular in that they handle the header bytes + checksum internally.
 *Data is transferred in ASCII hex format, i.e. 0x46 0xFE is sent and received as "46FE"
 *This could be modified by enabling the "packed data" mode but I think ELM327 devices
 *don't support that, and clones are equally hopeless.
 *
 *The ELM327 has non-volatile settings; this will require special treatment. Not
 * completely implemented at the moment.

 *Note concerning non-volatile settings : it appears only these are stored to the EEPROM:
 * -327: AT SP (set protocol)
 * -327: PP 0C (custom baudrate)
 * -323: these have no EEPROM so "ATZ" should really reset all to default
  *
 *These devices handle the periodic wake-up commands on the bus. This is the only l0
 *device with that feature; upper levels (esp L2) need to be modified to take this into account. See
 *diag_l3_saej1979.c : diag_l3_j1979_timer() etc.
 *
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"

CVSID("$Id$");

#define ELM_BUFSIZE 40	//longest data to be received during init is the version string, ~ 15 bytes,
		// plus possible command echo. OBD data is 7 bytes, received as a 23-char string. 32 should be enough...

struct diag_l0_elm_device {
	int protocol;		//current L1 protocol
	struct diag_serial_settings serial;
	int elmflags; 	//see defines below
};

//flags for elmflags; set either 323_BASIC or 327_BASIC but not both;
// _CLONE can be set in addition to the basic type
#define ELM_323_BASIC	1	//device type is 323
#define ELM_327_BASIC	2	//device type is 327
#define ELM_32x_CLONE	4	 //device is a clone; some commands will not be supported
#define ELM_38400	8	//device is using 38.4kbps; default is 9600


// possible error messages returned by the ELM IC
static const char * elm323_errors[]={"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?", NULL};

static const char * elm327_errors[]={"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?",
						"ACT ALERT", "BUFFER FULL", "BUS ERROR", "CAN ERROR", "LP ALERT",
						"LV RESET", "<RX ERROR", "STOPPED", "UNABLE TO CONNECT", "ERR", NULL};

// authentic VS clone identification strings.
// I know of no elm323 clones. 327 clones may not support some commands (atfi, atsi, atkw) and thus need fallback methods
static const char * elm323_official[]={"2.0",NULL};	//authentic 323 firmware versions, possibly incomplete list
static const char * elm323_clones[]={NULL};	//known cloned versions
static const char * elm327_official[]={"1.0a", "1.0", "1.1", "1.2a", "1.2", "1.3a", "1.3", "1.4b", "2.0", NULL};
static const char * elm327_clones[]={"1.4a", "1.5a", "1.5", "2.1", NULL};


static const struct diag_l0 diag_l0_elm;

static int diag_l0_elm_send(struct diag_l0_device *dl0d,
	UNUSED(const char *subinterface), const void *data, size_t len);

static int diag_l0_elm_sendcmd(struct diag_l0_device *dl0d,
	const uint8_t *data, size_t len, int timeout, uint8_t *resp);

void elm_parse_cr(uint8_t *data, int len);	//change 0x0A to 0x0D

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int
diag_l0_elm_init(void)
{
	static int diag_l0_elm_initdone=0;

	if (diag_l0_elm_initdone)
		return 0;


	/* Do required scheduling tweaks */
	diag_os_sched();

	diag_l0_elm_initdone = 1;
	return 0;
}

//diag_l0_elm_close : free pdl0d->dl0 (dev), call diag_tty_close.
static int
diag_l0_elm_close(struct diag_l0_device **pdl0d)
{
	assert(pdl0d != NULL);
	assert(*pdl0d != NULL);
	uint8_t buf[]="ATPC\x0D";
	diag_l0_elm_sendcmd(*pdl0d, buf, 5, 500, NULL);	//close protocol. So clean !
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_elm_device *dev =
			(struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

		/* If debugging, print to strerr */
		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n",
				FL, (void *) dl0d);

		if (dev)
			free(dev);

		(void) diag_tty_close(pdl0d);
	}

	return 0;
}


//Send a command to ELM device and make sure no error occured. Data is passed on directly as a string;
//caller must make sure the string is \r-terminated , i.e. 0x0D-terminated.
//Sending 0A after 0D will cause ELM to interrupt what it's doing to "process" 0A, resulting in a
//failure.
//This func should not be used for commands that elicit a data response (i.e. all data destined to the OBD bus,
//hence not prefixed by "AT"). Response is dumped in *resp (0-terminated) for optional analysis by caller; *resp must be ELM_BUFSIZE long.
//returns 0 on success if "OK" is found in the response, and if no known error message was present.
//_sendcmd should not be called from outside diag_l0_elm.c.
static int
diag_l0_elm_sendcmd(struct diag_l0_device *dl0d, const uint8_t *data, size_t len, int timeout, uint8_t *resp)
{
	//note : we better not request (len == (size_t) -1) bytes ! The casts between ssize_t and size_t are
	// "muddy" in here
	ssize_t xferd;
	int i, rv;
	uint8_t ebuf[ELM_BUFSIZE];	//local buffer if caller provides none.
	uint8_t *buf;
	struct diag_l0_elm_device *dev;
	const char ** elm_errors;	//just used to select between the 2 error lists

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);
	//we need access to diag_l0_elm_device to access .elmflags

	if (resp==NULL)
		buf=ebuf;	//use local buffer
	else
		buf=resp;	//or caller-provided buffer

	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);
	if (!dev)
		return diag_iseterr(DIAG_ERR_BADFD);


	if (data[len-1] != 0x0D) {
		//Last byte is not a carriage return, this would die.
		fprintf(stderr, FLFMT "elm_sendcmd: non-terminated command : %.*s\n", FL, (int) len, (char *) data);
		//the %.*s is pure magic : limits the string length to len, even if the string is not null-terminated.
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "sending command to ELM: %.*s\n", FL, (int) len-1, (char *)data);
	}

	while ((xferd = diag_tty_write(dl0d, data, len)) != (ssize_t) len) {
		/* Partial write */
		if (xferd < 0) {	//write error
			/* error */
			if (errno != EINTR) {	//not an interruption
				//warning : errno probably doesn't work on Win...
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
	//diag_os_millisleep(timeout);

	//TODO : this depends on diag_tty_read to be blocking ?
	rv=diag_tty_read(dl0d, buf, ELM_BUFSIZE-5, timeout);	//rv=# bytes read
	if (diag_l0_debug & (DIAG_DEBUG_WRITE | DIAG_DEBUG_READ)) {
		fprintf(stderr, FLFMT "sent %d bytes\n", FL, (int) xferd);
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
		fprintf(stderr, FLFMT "ELM not ready (no prompt received): %s\n", FL, buf);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	//At this point we got a prompt but there may have been an error message.
	//There is some ambiguity in the ELM datasheets on the exact format of the replies.
	//1) the prompt character '>' is always alone on its line;
	//2) depending on some parameters, it may be preceded by 0D or 0D 0A
	//3) some errors ( "<DATA ERROR" and "<RX ERROR" ) can be appended after
	//a reply; others should be at the beginning of the response (alone on a line)

	//ex. of good reply : "41 00 00 \r>", bad reply :"NO DATA\r>"
	//let's just use strstr() to find occurences for each known error.
	//it'll take a while but speed isn't normally critical when sendind commands

	//let's null-terminate the reply first;
	buf[rv]=0;

	//pick the right set of error messages. Although we could just systematically
	//use the vaster ELM327 set of error messages...

	if (dev->elmflags & ELM_323_BASIC)
		elm_errors=elm323_errors;
	else
		elm_errors=elm327_errors;

	for (i=0; elm_errors[i]; i++) {
			if (strstr((char *)buf, elm_errors[i])) {
					//we found an error msg : print the whole buffer
					fprintf(stderr, FLFMT "ELM returned error : %s\n", FL, elm_errors[i]);
					return diag_iseterr(DIAG_ERR_GENERAL);
			}
	}
	//and make sure we got a positive response "OK".
	if (strstr((char *)buf, "OK") == NULL) {
		fprintf(stderr, FLFMT "Response not recognized ! Report this ! Reply was:\n%s\n", FL, buf);
		//problem : some commands (maybe just ATZ) don't send "OK", despite the success of the commmand.
		//elm_open depends on this returning DIAG_ERR_INCDATA to ignore the error. Hax
		//TODO : don't return an error if OK wasn't found ?
		return diag_iseterr(DIAG_ERR_INCDATA);
	}

	return 0;

}
/*
 * Open the diagnostic device, returns a file descriptor
 * records original state of term interface so we can restore later
 * ELM settings used : no echo (E0), headers on (H1), linefeeds off (L0), mem off (M0)
 */
static struct diag_l0_device *
diag_l0_elm_open(const char *subinterface, int iProtocol)
{
	int i,rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_elm_device *dev;
	struct diag_serial_settings sset;
	const uint8_t *buf;
	uint8_t rxbuf[ELM_BUFSIZE];

	const char ** elm_official;
	const char ** elm_clones;	//point to elm323_ or elm327_ clone and official version string lists

	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n",
			FL, subinterface, iProtocol);
	}

	diag_l0_elm_init();

	if ((rv=diag_calloc(&dev, 1)))
		return diag_pseterr(rv);

	dev->protocol = iProtocol;

	if ((rv=diag_tty_open(&dl0d, subinterface, &diag_l0_elm, (void *)dev))) {
		fprintf(stderr, FLFMT "Problem opening %s.\n", FL, subinterface);
		return diag_pseterr(rv);
	}

	//set speed to 9600;8n1.
	sset.speed=9600;
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;

	dev->serial = sset;

	if ((rv=diag_tty_setup(dl0d, &sset))) {
		fprintf(stderr, FLFMT "Error setting 9600;8N1 on %s\n",
			FL, subinterface);
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(rv);
	}

	diag_tty_iflush(dl0d);	/* Flush unread input */

	//At this stage, the ELM has possibly been powered up for a while;
	//sending the command "ATZ" will cause a full reset and the chip will send a
	//welcome string like "ELM32x vX.Xx\n>" ;
	//note : by default the ELM sends an echo of our command until we ATE0.
	//We try 9600 bps first, then 38.4k if necessary.

	dev->elmflags=0;	//we know nothing about it yet
	if (diag_l0_debug&DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "elm_open : sending ATZ @ 9600..\n", FL);
	}

	buf=(uint8_t *)"ATZ\x0D";
	rv=diag_l0_elm_sendcmd(dl0d, buf, 4, 2000, rxbuf);
	//it seems ELMs don't say "OK" after ATZ, this confuses elm_sendcmd().
	if ((rv!=DIAG_ERR_INCDATA) && (rv < 0)) {
		//failed @ 9600:
		fprintf(stderr, FLFMT "sending ATZ @ 9600 failed; trying @ 38400...\n", FL);

		sset.speed=38400;
		dev->serial = sset;

		if ((rv=diag_tty_setup(dl0d, &sset))) {
			fprintf(stderr, FLFMT "Error setting 38400;8N1 on %s\n",
				FL, subinterface);
			diag_l0_elm_close(&dl0d);
			return diag_pseterr(rv);
		}

		diag_tty_iflush(dl0d);	/* Flush unread input */

		rv=diag_l0_elm_sendcmd(dl0d, buf, 4, 2000, rxbuf);

		if ((rv!=DIAG_ERR_INCDATA) && (rv < 0)) {
			diag_l0_elm_close(&dl0d);
			fprintf(stderr, FLFMT "sending ATZ @ 38400 failed.\n", FL);
			return diag_pseterr(DIAG_ERR_BADIFADAPTER);
		}
		dev->elmflags |= ELM_38400;
	}

	//Correct prompt received; try to identify device. rv: "device identified" flag
	rv=0;
	// 1) guess 323 vs 327
	if (strstr((char *)rxbuf, "ELM323")!=NULL) {
		dev->elmflags |= ELM_323_BASIC;
		elm_official=elm323_official;
		elm_clones=elm323_clones;
	} else if (strstr((char *)rxbuf, "ELM327")!=NULL) {
		dev->elmflags |= ELM_327_BASIC;
		elm_official=elm327_official;
		elm_clones=elm327_clones;
	} else {
		fprintf(stderr, FLFMT "no valid version string !! Report this !. Got:\n%s\n", FL, rxbuf);
		//no point in continuing...
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}
	// 2) identify valid VS clone devices.
	for (i=0; elm_clones[i]; i++) {
		if (strstr((char *)rxbuf, elm_clones[i])) {
			printf("Clone ELM found, v%s\n", elm_clones[i]);
			dev->elmflags |= ELM_32x_CLONE;
			rv=1;
			break;
		}
	}

	if (rv==0) {
		for (i=0; elm_official[i]; i++) {
				if (strstr((char *)rxbuf, elm_official[i])) {
				printf("Official ELM found, v%s\n", elm_official[i]);
				rv=1;
				break;
			}
		}
	}

	if (rv==0) {
		//still not identified : assume clone.
		dev->elmflags |= ELM_32x_CLONE;
		printf("ELM version not recognized! Please report this ! Resp=%s\n", rxbuf);
	}


	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM reset success, elmflags=%#x\n", FL, dev->elmflags);
	}

	//now send "ATE0\n" command to disable echo.
	buf=(uint8_t *)"ATE0\x0D";
	if (diag_l0_elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATE0\" failed\n", FL);
		}
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}

	//ATL0 : disable linefeeds
	buf=(uint8_t *)"ATL0\x0D";
	if (diag_l0_elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATL0\" failed\n", FL);
		}
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}

	//ATH1 : always show header bytes
	buf=(uint8_t *)"ATH1\x0D";
	if (diag_l0_elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		if (diag_l0_debug & DIAG_DEBUG_OPEN) {
			fprintf(stderr, FLFMT "sending \"ATH1\" failed\n", FL);
		}
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
	}

	//for elm327 only: disable memory
	if (dev->elmflags & ELM_327_BASIC) {
		buf=(uint8_t *)"ATM0\x0D";
		if (diag_l0_elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
			if (diag_l0_debug & DIAG_DEBUG_OPEN) {
				fprintf(stderr, FLFMT "sending \"ATM0\" failed\n", FL);
			}
			diag_l0_elm_close(&dl0d);
			return diag_pseterr(DIAG_ERR_BADIFADAPTER);
		}
	}

	//check if proto is really supported (323 supports only 9141 and 14230)
	if ((dev->elmflags & ELM_323_BASIC) &&
		((iProtocol != DIAG_L1_ISO9141) || (iProtocol != DIAG_L1_ISO14230)))
			return diag_pseterr(DIAG_ERR_PROTO_NOTSUPP);

	//if 327, set proto when possible; we won't if 14230, because init type (fast vs slow) isn't decided yet
	// CAN is also not implemented here
	buf=NULL;
	if (dev->elmflags & ELM_327_BASIC) {
		switch (iProtocol) {
		case DIAG_L1_J1850_PWM:
			buf=(uint8_t *)"ATTP1\x0D";
			break;
		case DIAG_L1_J1850_VPW:
			buf=(uint8_t *)"ATTP2\x0D";
			break;
		case DIAG_L1_ISO9141:
			buf=(uint8_t *)"ATTP3\x0D";
			break;
		default:
			buf=NULL;
		}
	}

	if (buf != NULL) {
		if (diag_l0_elm_sendcmd(dl0d, buf, 6, 500, NULL)) {
			if (diag_l0_debug & DIAG_DEBUG_OPEN) {
				fprintf(stderr, FLFMT "sending \"ATTPx\" failed\n", FL);
			}
		diag_l0_elm_close(&dl0d);
		return diag_pseterr(DIAG_ERR_BADIFADAPTER);
		}
	}


	//at this point : ELM is ready for further ops
	if (diag_l0_debug & DIAG_DEBUG_OPEN) {
		fprintf(stderr, FLFMT "ELM ready.\n", FL);
	}
	return dl0d;
}


/*
 * Fastinit & slowinit:
 * ELM will manage slow/fast init automatically... Until the code from levels L1 and up can deal with
 * this, the bus will manually be initialized.
 * Only callable internally. Not for export !!
 * Unsupported by some (all?) clones !! (they handle the init internally, on demand)
 */
static int
diag_l0_elm_fastinit(struct diag_l0_device *dl0d)
{
	uint8_t * cmds= (uint8_t *) "ATFI\x0D";

	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr, FLFMT "ELM forced fastinit...\n", FL);

	//send command with 1000ms timeout (guessing)
	if (diag_l0_elm_sendcmd(dl0d, cmds, 5, 1000, NULL)) {
		fprintf(stderr, FLFMT "Command ATFI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}


static int
diag_l0_elm_slowinit(struct diag_l0_device *dl0d)
{
	uint8_t * cmds=(uint8_t *)"ATSI\x0D";

	if (diag_l0_debug & DIAG_DEBUG_PROTO) {
		fprintf(stderr, FLFMT "ELM forced slowinit...\n", FL);
	}

	//huge timeout of 2.5s. Not sure if this is adequate
	if (diag_l0_elm_sendcmd(dl0d, cmds, 5, 2500, NULL)) {
		fprintf(stderr, FLFMT "Command ATSI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;

}

/*
 * Do wakeup on the bus
 * For now, the address specified in initbus_args is ignored.
 */
static int
diag_l0_elm_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	const uint8_t *buf;
	int rv = DIAG_ERR_INIT_NOTSUPP;

	struct diag_l0_elm_device *dev;

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT "ELM initbus type %d\n",
			FL, in->type);

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	if (!dev)
		return diag_iseterr(rv);


	if (dev->elmflags & ELM_32x_CLONE) {
		printf("Note : ELM clones do not support explicit bus initialization. Errors here are ignored.\n");
	}

	switch (in->type) {
		case DIAG_L1_INITBUS_FAST: {
			uint8_t fmt, src, tgt;
			uint8_t setproto[]="ATTP5\x0D";
			uint8_t sethdr[15];	//format: "ATSH xx yy zz\x0D"

			fmt=(in->physaddr)? 0x81:0xC1;
			src=in->testerid;
			tgt=in->addr;
			if (dev->elmflags & ELM_327_BASIC) {
				//only needed for 327s (ATTP not available on ELM323)
				//set proto; We assume iso14230 since it's a fast init
				rv=diag_l0_elm_sendcmd(dl0d, setproto, 6, 500, NULL);
				if (rv<0)
					break;
			}

			sprintf((char *) sethdr, "ATSH %02X %02X %02X\x0D", fmt, tgt, src);
			rv=diag_l0_elm_sendcmd(dl0d, sethdr, 14, 500, NULL);

			//explicit init is not supported by clones, but they should work anyway...
			if (dev->elmflags & ELM_32x_CLONE)
				rv=0;
			else
				rv = diag_l0_elm_fastinit(dl0d);
			}
			break;	//case fastinit
		case DIAG_L1_INITBUS_5BAUD:
			if ((dev->elmflags & ELM_323_BASIC) && (dev->elmflags & ELM_32x_CLONE)) {
				printf("A 323 clone ? Report this !\n");
			}

			//if 327 : set proto first
			if (dev->elmflags & ELM_327_BASIC) {
				if (dev->protocol & DIAG_L1_ISO9141)
					buf=(uint8_t *) "ATTP3\x0D";
				else if (dev->protocol & DIAG_L1_ISO14230)
					buf=(uint8_t *) "ATTP4\x0D";

				rv=diag_l0_elm_sendcmd(dl0d, buf, 6, 500, NULL);
			}

			//explicit init is not supported by clones, but they should work anyway...
			if (dev->elmflags & ELM_32x_CLONE)
				rv=0;
			else
				rv = diag_l0_elm_slowinit(dl0d);
			break;
		default:
			rv = DIAG_ERR_INIT_NOTSUPP;
			break;
	}

	return rv? diag_iseterr(rv):0;

}



/*
 * Send a load of data
 *
 * Directly send hex-ASCII; exit without receiving response.
 * Upper levels don't append 0x0D / 0x0A at the end, we take care of adding the required 0x0D.
 * "AT"* commands sould not be sent with this function.
 * Returns 0 on success
 */

static int
diag_l0_elm_send(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
const void *data, size_t len)

{
	uint8_t buf[ELM_BUFSIZE];
	ssize_t xferd;
	unsigned int i;

	if (len <= 0)
		return diag_iseterr(DIAG_ERR_BADLEN);

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
		snprintf((char *) &buf[2*i], 3, "%02X", (unsigned int)((uint8_t *)data)[i] );
	}
	i=2*len;
	buf[i]=0x0D;
	buf[i+1]=0x00;	//terminate string

	if ((diag_l0_debug & DIAG_DEBUG_WRITE) && (diag_l0_debug & DIAG_DEBUG_DATA)) {
		fprintf(stderr, FLFMT "ELM: sending %s\n", FL, (char *) buf);
	}

	while ((xferd = diag_tty_write(dl0d, buf, i+1)) != (ssize_t)(i+1)) {
		/* Partial write */
		if (xferd < 0) {	//write error
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
		data = (void *)((uint8_t *)data + xferd);
	}

	return 0;
}

/*
 * Get data (blocking), returns number of chars read, between 1 and len
 * If timeout is set to 0, this becomes non-blocking
 * ELM returns a string with format "%02X %02X %02X[...]\n" .
 * We convert this received ascii string to hex before returning.
 */
static int
diag_l0_elm_recv(struct diag_l0_device *dl0d,
UNUSED(const char *subinterface),
void *data, size_t len, int timeout)
{
	int xferd;
	uint8_t rxbuf[ELM_BUFSIZE];		//need max (7 digits *2chars) + (6 space *1) + 1(CR) + 1(>) + (NUL) bytes.
	char *rptr, *bp;
	unsigned int rbyte;

	if (!len)
		return diag_iseterr(DIAG_ERR_BADLEN);
	//possibly not useful until ELM323 vs 327 distinction is made :

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr, FLFMT "Expecting %d bytes from ELM, %d ms timeout\n", FL, (int) len, timeout);

	while ( (xferd = diag_tty_read(dl0d, rxbuf, len, timeout)) <= 0) {
		if (xferd == DIAG_ERR_TIMEOUT) {
			return DIAG_ERR_TIMEOUT;
		}
		if (xferd == 0) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned EOF !!\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		if (errno != EINTR) {
			/* Error, EOF */
			fprintf(stderr, FLFMT "read returned error %d !!\n", FL, errno);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
	}
	if (diag_l0_debug & DIAG_DEBUG_READ) {
		diag_data_dump(stderr, &rxbuf, (size_t)xferd);
		fprintf(stderr, "\n");
	}

	//Here, rxbuf contains the string received from ELM. Parse it to get hex digits
	xferd=0;
	rptr=(char *)(rxbuf + strspn((char *)rxbuf, " \n\r"));	//skip all leading spaces and linefeeds
	while ((bp=strtok(rptr, " >\n\r")) !=NULL) {
		//process token delimited by spaces or prompt character
		//this is very sketchy and deserves to be tested more...
		sscanf(bp, "%02X", &rbyte);
		((uint8_t *)data)[xferd]=(uint8_t) rbyte;
		xferd++;
		if ( (size_t)xferd==len)
			break;
		//printf("%s\t0x%02X\n", bp, i);
		rptr=NULL;
	}
	return xferd;
}

/*
 * Set speed/parity
* but upper levels shouldn't be doing this.
* This function will do nothing
 */
static int
diag_l0_elm_setspeed(UNUSED(struct diag_l0_device *dl0d),
	const struct diag_serial_settings *pss)
{
	fprintf(stderr, FLFMT "Warning: attempted to override ELM com speed (%d)! Report this !\n", FL,pss->speed);
	return 0;
}

static int
diag_l0_elm_getflags(struct diag_l0_device *dl0d)
{

	struct diag_l0_elm_device *dev;
	int flags=0;

	dev = (struct diag_l0_elm_device *)diag_l0_dl0_handle(dl0d);

	flags = DIAG_L1_DATAONLY | DIAG_L1_AUTOSPEED | DIAG_L1_STRIPSL2CKSUM | DIAG_L1_DOESP4WAIT;
	flags |= DIAG_L1_DOESL2FRAME | DIAG_L1_DOESL2CKSUM | DIAG_L1_DOESFULLINIT;

	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		flags |= DIAG_L1_SLOW;
		//flags |= DIAG_L1_NOHDRS;	//probably not needed since we send ATH1 on init (enable headers)
		break;
	case DIAG_L1_ISO14230:
		flags |= DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
		//flags |= DIAG_L1_NOHDRS;
		break;
	default:
		break;
	}


	if (diag_l0_debug & DIAG_DEBUG_PROTO)
		fprintf(stderr,
			FLFMT "getflags link %p proto %d flags 0x%X\n",
			FL, (void *)dl0d, dev->protocol, flags);

	return flags;

}

//elm_parse_cr : change 0x0A to 0x0D in datastream.
void elm_parse_cr(uint8_t *data, int len) {
	int i=0;
	for (;i<len; i++) {
		if (*data==0x0D)
			*data=0x0A;
		data++;
	}
	return;
}

static const struct diag_l0 diag_l0_elm = {
	"Scantool.net ELM32x Chipset Device",
	"ELM",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_J1850_PWM | DIAG_L1_J1850_VPW | DIAG_L1_CAN,
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
