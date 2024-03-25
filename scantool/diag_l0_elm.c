/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * (c) 2011-2016 fenugrec
 * Diag, Layer 0, interface for Scantool.net's ELM32x Interface
 *
 * This is meant to support ELM323 & 327 devices; clones and originals.
 * COM speed is "autodetected" (it tries 38400, 9600 and 115200bps) but can also be
 * manually specified.
 *
 * ELM interfaces are particular in that they handle the header bytes + checksum internally.
 * Data is transferred in ASCII hex format, i.e. 0x46 0xFE is sent and received as "46FE"
 * This could be modified by enabling the "packed data" mode but I think ELM327 devices
 * don't support that, and clones are equally hopeless.
 *
 * The ELM327 has non-volatile settings; this will require special treatment. Not
 * completely implemented at the moment.

 * Note concerning non-volatile settings : it appears only these are stored to the EEPROM:
 * -327: AT SP (set protocol)
 * -327: PP 0C (custom baudrate)
 * -323: these have no EEPROM so "ATZ" should really reset all to default
 *
 *
 * Problems :
 * * _recv() doesn't check if a good prompt '>' was received at the end
 * * every time _open() is called it goes through the whole (long) init sequence
 * * responses from multiple ECUs probably won't work
 *
 * Incomplete list of commands not supported by clones : {FI, SI, BD, KW}
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_cfg.h"
#include "diag_os.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"


#define ELM_BUFSIZE 1000        //fit even max-length iso14230 frames, at 3 ASCII chars per byte.
#define ELM_SLOWNESS    100     //Add this many ms to read timeouts, because ELMs are sloooow
#define ELM_PURGETIME	465	//Time to wait (ms) for a response to "ATI" command.
				// - On 2017-11-23, jonesrh increased delay after ATI to precise ms delayed in volvo850diag for AT commands,
				//   in hopes of avoiding OBDLink LX BT lockups which only occur 
				//   sometime after freediag has done a disconnect and begun repeating the 3 ATI commands for the next ECU.
				// - That OBDLink LX Bluetooth lockup behavior has not manifested as a pattern for any other software
				//   that I've used during the past several years.  So the problem likely involves some sort of freediag timing issue.
				// - As of 2017-12-11 it is unknown if this increase from 400 to 465 has helped anything.

struct elm_device {
	int protocol;           //current L1 protocol

	int elmflags;   //see defines below

	struct  cfgi port;
	struct  cfgi speed;     //Host <-> ELM comms

	struct diag_serial_settings serial;
	ttyp *tty_int;                  /** handle for tty stuff */

	uint8_t kb1, kb2;       // key bytes from 5 baud init
	uint8_t atsh[3];        // current header setting for ISO9141
	struct diag_msg *wm;    // custom wakeup message, if set
};

#define CFGSPEED_DESCR "Host <-> ELM comm speed (bps)"
#define CFGSPEED_SHORTN "elmspeed"


//flags for elmflags; set either 323_BASIC or 327_BASIC but not both;
// _CLONE can be set in addition to the basic type
#define ELM_323_BASIC   1       //device type is 323
#define ELM_327_BASIC   2       //device type is 327
#define ELM_32x_CLONE   4       //device is a clone; some commands will not be supported
#define ELM_INITDONE    0x10    //set when "BUS INIT" has happened. This is important for clones.


// possible error messages returned by the ELM IC
static const char *elm323_errors[] = {"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?", NULL};

static const char *elm327_errors[] = {"BUS BUSY", "FB ERROR", "DATA ERROR", "<DATA ERROR", "NO DATA", "?",
	                              "ACT ALERT", "BUFFER FULL", "BUS ERROR", "CAN ERROR", "LP ALERT",
	                              "LV RESET", "<RX ERROR", "STOPPED", "UNABLE TO CONNECT", "ERR", NULL};

// authentic VS clone identification strings.
// I know of no elm323 clones. 327 clones may not support some commands (atfi, atsi, atkw) and thus need fallback methods
static const char *elm323_official[] = {"2.0",NULL};    //authentic 323 firmware versions, possibly incomplete list
static const char *elm323_clones[] = {NULL};    //known cloned versions
static const char *elm327_official[] = {"1.0a", "1.0", "1.1", "1.2a", "1.2", "1.3a", "1.3", "1.4b", "2.0", NULL};
static const char *elm327_clones[] = {"1.4", "1.4a", "1.5a", "1.5", "2.1", NULL};

// baud rates for host to elm32x communication. Start with user-specified speed, then try common values
#define ELM_CUSTOMSPEED ((unsigned) -1)
static const unsigned elm_speeds[] = {ELM_CUSTOMSPEED, 38400, 9600, 115200, 0};


extern const struct diag_l0 diag_l0_elm;

static int elm_send(struct diag_l0_device *dl0d, const void *data, size_t len);

static int elm_sendcmd(struct diag_l0_device *dl0d,
                       const uint8_t *data, size_t len, unsigned int timeout, uint8_t *resp);

static int elm_purge(struct diag_l0_device *dl0d);

static void elm_parse_cr(uint8_t *data, int len);       //change 0x0A to 0x0D
static void elm_close(struct diag_l0_device *dl0d);

/*
 * Init must be callable even if no physical interface is
 * present, it's just here for the code here to initialise its
 * variables etc
 */
static int elm_init(void) {
	static int elm_initdone=0;

	if (elm_initdone) {
		return 0;
	}

	elm_initdone = 1;
	return 0;
}

int elm_new(struct diag_l0_device *dl0d) {
	struct elm_device *dev;
	int rv;

	assert(dl0d);

	rv = diag_calloc(&dev, 1);
	if (rv != 0) {
		return diag_ifwderr(rv);
	}

	dl0d->l0_int = dev;

	rv = diag_cfgn_tty(&dev->port);
	if (rv != 0) {
		free(dev);
		return diag_ifwderr(rv);
	}

	rv = diag_cfgn_int(&dev->speed, 38400, 38400);
	if (rv != 0) {
		diag_cfg_clear(&dev->port);
		free(dev);
		return diag_ifwderr(rv);
	}
	dev->speed.descr = CFGSPEED_DESCR;
	dev->speed.shortname = CFGSPEED_SHORTN;

	dev->port.next = &dev->speed;
	dev->speed.next = NULL;

	return 0;
}

struct cfgi *elm_getcfg(struct diag_l0_device *dl0d) {
	struct elm_device *dev;
	if (dl0d == NULL) {
		return diag_pseterr(DIAG_ERR_BADCFG);
	}

	dev = dl0d->l0_int;
	return &dev->port;
}

void elm_del(struct diag_l0_device *dl0d) {
	struct elm_device *dev;

	assert(dl0d);

	dev = dl0d->l0_int;
	if (!dev) {
		return;
	}

	diag_cfg_clear(&dev->port);
	diag_cfg_clear(&dev->speed);
	if (dev->wm != NULL) {
		diag_freemsg(dev->wm);
	}
	free(dev);
	return;
}

void delay_after_rsp_before_next_rqst( struct diag_l0_device *dl0d, int try, int min_delay_after_1st_fail, int delay_after_try_1_ok_but_not_115200, int delay_after_try_1_ok_but_is_clone)
{
	// In D2 situations involving ELM327 where retries were needed, there's a greater likelihood that a delay is needed
	// so subsequent actions will more likely work on the first attempt and/or will require fewer repetitions.
	// - Hopefully, this will cut down on STOPPED, BUS ERROR, and FB ERROR errors:
	//   * especially for the COMBI NV requests, and
	//   * especially for slower devices (eg, those at 38400 baud), and
	//   * especially for clones.
	// - One or more "switch"es might need to be used to limit the delay to an even smaller class of situations.
	// - This is a situation which has been eliminated in volvo850diag,
	//   so the volvo850diag experience is used to set min_delay_after_1st_fail delays.
	// - The delay after a successful first try is a brand new experimentation.
	if (try > 1) {
		// At least 1 retry was required for the previous request.
		// - This should be fed delay times which volvo850diag has experienced do not produce any ELM errors >99% of the time,
		//   thereby automatically throttling the next freediag request so it can more likely succeed on the first try.
		diag_os_millisleep(min_delay_after_1st_fail);
	}
	else if (try == 1) {
		// Previous request worked on the first try.
		// - Put these in separate clauses in case need to change their values to different values
		//   depending on the particular cases.
		// - Behavior (as of 2017-11-24), before addition of this "try #1 OK" finetuning, is:
		//   * OBDLink SX USB (115200 baud) -- 0 STOPPED responses encountered on subsequent request after the previous request worked on its first try.
		//                                     Consequently, there should *not* be any added delay for that tool in this situation.
		//   * ELM327 v1.5 USB clone (38400 baud) -- Almost every first try produces a STOPPED response, indicating freediag is responding too quickly.
		//   * OBDLink LX BT (either 115200 or 38400 baud) -- More similar to OBDLink SX USB, but then OBDLink LX BT "locks up" and doesn't respond to any command/rqst.
		// In first implementation, delay a fixed amount after the "success on 1st try" situation,
		// **if** elmspeed is slower than 115200, or if device is clone.  In those two situations:
		// - First tried a minimum of 60 ms, in case the ELM327 compatible is not enforcing the >= 55 ms constraint
		//   between end of response reception and beginning of next request.
		// - We will see how that lessens generation of STOPPED responses on my:
		//   * ELM327 v1.5 USB clone, and
		//   * OBDLink LX BT (when used at 38400 baud).
		// - The 60 ms works fine almost always, but not always, so was increased on 2017-12-02 to 70 ms.
		//   * Actually the caller gets to control that value, so look in scantool_850.c to see the increase to 70 ms (or whatever the latest value is).
		// - A delay was added for the non-clone, 115200 baud devices on 2017-12-06, and it was initially set to only 10 ms.
		//   It is rarely needed, so we'll start very low and increase it as the need dictates.
		//   I'm inclined for the time being to *not* add it as another parameter.
		//   * The error situation which necessitated this newly introduced 10 ms delay was a 2017-12-02 instance of COMBI A507 try #1 success,
		//     but the following COMBI A509 failed with STOPPED on try #1 / NO DATA on try #2 / then had success on try #3.
		//   * Note that if this newly introduced non-clone, 115200 baud, try #1 success delay eventually 
		//     has to be increased to the clone delay or <115200 baud delay, then we should just:
		//     - eliminate the delays passed in as parameters, 
		//     - remove the clone and 115200 oriented code,
		//     - move this routine from diag_l0_elm.c to scantool_850.c
		//	 (where it was intended to go when originally conceived).
		//     However, as of 2017-12-11, after multiple tests over 5 days, it appears to so far not need increasing above 10 ms.
		// - The 70 ms was increased to 80 ms on 2017-12-07 after encountering ELM327 v1.5 USB clone STOPPED errors when "id" tried
		//   after successful 1st try "dtc", where the clone had connected via ATSI (instead of via elm_bogusinit)).
		// - The 80 ms was increased to 110 ms (in scantool_850.c) on 2017-12-10 after encountering ELM327 v1.5 USB clone STOPPED errors when "sendreq" tried
		//   after successful 1st try "dtc", where the clone had connected via ATSI (instead of via elm_bogusinit)).
		//   * That eliminated almost all the STOPPED errors, but they still show up sometimes
		//     on the second scanned item after an ECU connects.
		// - The 110 ms was increased to 140 ms later on 2017-12-11 after encountering ELM327 v1.5 USB clone STOPPED errors when combi live 04 tried
		//   after successful 1st try combi live 03, where the clone had connected via ATSI (instead of via elm_bogusinit)).
		if (dl0d == NULL)
			return;
		struct elm_device *dev;
		dev = (struct elm_device *)dl0d->l0_int;
		if (!dev)
			return;
		if (!(dev->elmflags & ELM_327_BASIC))
			return;
		if (dev->elmflags & ELM_32x_CLONE) {
			if ((0) || ((diag_l0_debug & (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)) == (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)))
				fprintf(stderr, FLFMT "delaying %d ms due to try #1 ok, but elm is clone\n", FL, (int) delay_after_try_1_ok_but_is_clone);
			diag_os_millisleep(delay_after_try_1_ok_but_is_clone);
		}
		else if (dev->speed.val.i < 115200) {
			if ((0) || ((diag_l0_debug & (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)) == (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)))
				fprintf(stderr, FLFMT "delaying %d ms due to try #1 ok, but elm speed < 115200\n", FL, (int) delay_after_try_1_ok_but_not_115200);
			diag_os_millisleep(delay_after_try_1_ok_but_not_115200);
		}
		else {
			if ((0) || ((diag_l0_debug & (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)) == (DIAG_DEBUG_TIMER | DIAG_DEBUG_DATA)))
				fprintf(stderr, FLFMT "delaying %d ms due to try #1 ok, when elm speed >= 115200 and is *not* a clone\n", FL, (int) 10);
			diag_os_millisleep(10);
		}
	}
	return;
}

static void elm_close(struct diag_l0_device *dl0d) {
	uint8_t buf[]="ATPC\x0D";
	struct elm_device *dev;

	assert(dl0d != NULL);

	if (dl0d->opened) {
		elm_sendcmd(dl0d, buf, 5, 500, NULL);   //close protocol. So clean !
	}

	dev = (struct elm_device *)dl0d->l0_int;

	/* If debugging, print to stderr */
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
	          FLFMT "link %p closing\n", FL, (void *)dl0d);

	diag_tty_close(dev->tty_int);
	dev->tty_int = NULL;
	dl0d->opened = 0;

	return;
}


//elm_parse_errors : look for known error messages in the reply.
// return any match or NULL if nothing found.
// data[] must be \0-terminated !
static const char *elm_parse_errors(struct diag_l0_device *dl0d, uint8_t *data) {
	struct elm_device *dev;
	const char **elm_errors;        //just used to select between the 2 error lists
	int i;

	dev = (struct elm_device *) dl0d->l0_int;

	//pick the right set of error messages. Although we could just systematically
	//use the vaster ELM327 set of error messages...

	if (dev->elmflags & ELM_323_BASIC) {
		elm_errors=elm323_errors;
	} else {
		elm_errors = elm327_errors;
	}

	for (i=0; elm_errors[i]; i++) {
		if (strstr((char *)data, elm_errors[i])) {
			//we found an error msg, return it.
			return elm_errors[i];
		}
	}
	return NULL;
}

//Send a command to ELM device and make sure no error occured. Data is passed on directly as a string;
//caller must make sure the string is \r-terminated , i.e. 0x0D-terminated.
//Sending 0A after 0D will cause ELM to interrupt what it's doing to "process" 0A, resulting in a
//failure.
//This func should not be used for commands that elicit a data response (i.e. all data destined to the OBD bus,
//hence not prefixed by "AT"). Response is dumped in *resp (0-terminated) for optional analysis by caller; *resp must be ELM_BUFSIZE long.
//returns 0 if (prompt_good) && ("OK" found anywhere in the response) && (no known error message was present) ||
//		(prompt_good && ATZ or ATKW command was sent) , since response doesn't contain "OK" for ATZ or ATKW.
//elm_sendcmd should not be called from outside diag_l0_elm.c.
static int elm_sendcmd(struct diag_l0_device *dl0d, const uint8_t *data, size_t len, unsigned int timeout, uint8_t *resp) {
	//note : we better not request (len == (size_t) -1) bytes ! The casts between ssize_t and size_t are
	// "muddy" in here
	//ssize_t xferd;
	int rv;
	uint8_t ebuf[ELM_BUFSIZE];      //local buffer if caller provides none.
	uint8_t *buf;
	struct elm_device *dev;
	const char *err_str;    //hold a possible error message

	dev = (struct elm_device *)dl0d->l0_int;
	//we need access to elm_device to access .elmflags

	if (resp == NULL) {
		buf=ebuf;       //use local buffer
	} else {
		buf = resp; // or caller-provided buffer
	}

	if (!len) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}
	if (!dev) {
		return diag_iseterr(DIAG_ERR_BADFD);
	}

	if (data[len-1] != 0x0D) {
		//Last byte is not a carriage return, this would die.
		fprintf(stderr, FLFMT "elm_sendcmd: non-terminated command : %.*s\n", FL, (int) len, (char *) data);
		//the %.*s is pure magic : limits the string length to len, even if the string is not null-terminated.
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	diag_tty_iflush(dev->tty_int);  //currently the code often "forgets" data in the input buffer, especially if the previous
	                                //transaction failed. Flushing the input increases the odds of not crashing soon

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
	          FLFMT "elm_sendcmd: %.*s\n", FL, (int) len-1, (char *)data);

	rv = diag_tty_write(dev->tty_int, data, len);
	if (rv != (int) len) {  //XXX danger ! evil cast
		fprintf(stderr, FLFMT "elm_sendcmd: write error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//next, receive ELM response, within {ms} delay.

	rv=diag_tty_read(dev->tty_int, buf, ELM_BUFSIZE-1, timeout);    //rv=# bytes read

	if (rv<1) {
		//no data or error
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		          FLFMT "ELM did not respond\n", FL);

		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	elm_parse_cr(buf, rv);  //debug output is prettier with this
	DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V, buf, rv,
	              FLFMT "received %d bytes (%.*s\n); hex: ", FL, rv, rv, (char *)buf);

	buf[rv]=0;      //terminate string
	if (buf[rv-1] != '>') {
		//if last character isn't the input prompt, there is a problem
		fprintf(stderr, FLFMT "ELM not ready (no prompt received): %s\nhex: ", FL, buf);
		// In some situations, the hex is an unnecessary clutter,
		// so it is conditional (as of 2017-12-07) -- jonesrh.
		if (strcmp((char *)buf, "BUS INIT: ...ERROR") != 0) {
			fprintf(stderr, "\nhex: ");
			diag_data_dump(stderr, buf, rv);
		}
		diag_data_dump(stderr, buf, rv);
		fprintf(stderr, "\n");
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
	//it'll take a while but speed isn't normally critical when sending commands

	err_str = elm_parse_errors(dl0d, buf);

	if (err_str != NULL) {
		fprintf(stderr, FLFMT "ELM returned error : %s\n", FL, err_str);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	//check if we either 1)got a positive response "OK"
	//2)were sending ATZ (special case hack, it doesn't answer "OK")
	if ((strstr((char *)buf, "OK") != NULL) ||
	    (strstr((char *)data, "ATKW") != NULL) ||
	    (strstr((char *)data, "ATZ") != NULL)) {
		return 0;
	}

	fprintf(stderr, FLFMT "Response not recognized ! Report this ! Got: \n", FL);
	diag_data_dump(stderr, buf, rv);
	fprintf(stderr, "\n");
	return diag_iseterr(DIAG_ERR_GENERAL);

}
/*
 * Open the diagnostic device
 * ELM settings used : no echo (E0), headers on (H1), linefeeds off (L0), mem off (M0)
 */
static int elm_open(struct diag_l0_device *dl0d, int iProtocol) {
	int i,rv;
	struct elm_device *dev;
	struct diag_serial_settings sset;
	const uint8_t *buf;
	uint8_t rxbuf[ELM_BUFSIZE];

	const char **elm_official;
	const char **elm_clones;        //point to elm323_ or elm327_ clone and official version string lists

	assert(dl0d);
	dev = dl0d->l0_int;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
	          FLFMT "open port %s L1proto %d\n",
	          FL, dev->port.val.str, iProtocol);

	elm_init();

	//sending ATZ to elm will wipe out current header setting
	dev->atsh[0]=0; dev->atsh[1]=0; dev->atsh[2]=0;

	//throw away previous wakeup message setting, if any
	if (dev->wm != NULL) {
		diag_freemsg(dev->wm);
	}
	dev->wm = NULL;

	/* try to open TTY */
	dev->tty_int = diag_tty_open(dev->port.val.str);
	if (dev->tty_int == NULL) {
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dev->protocol = iProtocol;

	//try sending a command to elm327 at each possible speed until we get a response
	sset.databits = diag_databits_8;
	sset.stopbits = diag_stopbits_1;
	sset.parflag = diag_par_n;
	for (i=0; elm_speeds[i]; i++) {
		sset.speed = elm_speeds[i];

		// skip if custom speed was already tried:
		if (sset.speed == (unsigned) dev->speed.val.i) {
			continue;
		}

		if (sset.speed == ELM_CUSTOMSPEED) {
			//magic flag to retrieve custom speed
			sset.speed = (unsigned) dev->speed.val.i;
		}
		fprintf(stderr, FLFMT "Sending ATI to ELM32x at %u...\n", FL, sset.speed);

		dev->serial = sset;

		if ((rv=diag_tty_setup(dev->tty_int, &sset))) {
			fprintf(stderr, FLFMT "Error setting %u;8N1 on %s\n",
			        FL, sset.speed, dev->port.val.str);
			elm_close(dl0d);
			return diag_ifwderr(rv);
		}

		diag_tty_iflush(dev->tty_int);  /* Flush unread input */

		//At this stage, the ELM has possibly been powered up for a while;
		//it may have an unfinished command / garbage in its input buffer. We
		//need to clear that before sending the real ATZ ==> ATI is quick and safe; elm_purge does this.

		dev->elmflags=0;        //we know nothing yet

		rv=elm_purge(dl0d);
		//if rv=0, we got a prompt so we know speed is set properly.
		if (rv == 0) {
			break;
		}
	}
	if (elm_speeds[i]==0) {
		fprintf(stderr, FLFMT "No response from ELM323/ELM327. Verify connection to ELM\n", FL);
		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
	          FLFMT "elm_open : sending ATZ...\n", FL);

	//the command "ATZ" causes a full reset and the ELM replies with
	//a string like "ELM32x vX.Xx\n>"

	buf=(uint8_t *)"ATZ\x0D";
	rv=elm_sendcmd(dl0d, buf, 4, 2000, rxbuf);
	if (rv) {
		fprintf(stderr, FLFMT "elm_open : ATZ failed !\n", FL);
		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}

	//Correct prompt received; try to identify device.
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
		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}
	// 2) identify valid VS clone devices.
	rv=0;   // temp "device identified" flag
	for (i=0; elm_clones[i]; i++) {
		if (strstr((char *)rxbuf, elm_clones[i])) {
			printf("Clone ELM found, v%s. Expect inferior performance\n", elm_clones[i]);
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
		printf("ELM version not recognized! Please report this ! Response was:\n%s\n", rxbuf);
	}

	if ((dev->elmflags & ELM_323_BASIC) && (dev->elmflags & ELM_32x_CLONE)) {
		printf("A 323 clone ? Report this !\n");
	}


	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
	          FLFMT "ELM reset success, elmflags=%#x\n", FL, dev->elmflags);

	dl0d->opened = 1;               //TODO : use this flag to skip the clone detection next time

	//now send "ATE0\n" command to disable echo.
	buf=(uint8_t *)"ATE0\x0D";
	if (elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		          FLFMT "sending \"ATE0\" failed\n", FL);

		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}

	//ATL0 : disable linefeeds
	buf=(uint8_t *)"ATL0\x0D";
	if (elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		          FLFMT "sending \"ATL0\" failed\n", FL);

		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}

	//ATH1 : always show header bytes
	buf=(uint8_t *)"ATH1\x0D";
	if (elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		          FLFMT "sending \"ATH1\" failed\n", FL);

		elm_close(dl0d);
		return diag_iseterr(DIAG_ERR_BADIFADAPTER);
	}

	//for elm327 only: disable memory
	if (dev->elmflags & ELM_327_BASIC) {
		buf=(uint8_t *)"ATM0\x0D";
		if (elm_sendcmd(dl0d, buf, 5, 500, NULL)) {
			DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			          FLFMT "sending \"ATM0\" failed\n", FL);

			elm_close(dl0d);
			return diag_iseterr(DIAG_ERR_BADIFADAPTER);
		}
	}

	//check if proto is really supported (323 supports only 9141 and 14230)
	if ((dev->elmflags & ELM_323_BASIC) &&
	    ((iProtocol != DIAG_L1_ISO9141) &&
	     (iProtocol != DIAG_L1_ISO14230))) {
		return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
	}

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
		if (elm_sendcmd(dl0d, buf, 6, 500, NULL)) {
			DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
			          FLFMT "sending \"ATTPx\" failed\n", FL);

			elm_close(dl0d);
			return diag_iseterr(DIAG_ERR_BADIFADAPTER);
		}
	}


	//at this point : ELM is ready for further ops
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
	          FLFMT "ELM ready.\n", FL);

	return 0;
}

// Determine if is D2 ECU (based on the WM message contents).
//
// There's probably a better name for this and/or a better place to put it
// and/or a better way to check if the L2 protocol is D2.
// That's an exercise for the reviewer.
//
static int
is_d2( struct elm_device *dev, struct diag_l1_initbus_args *in)
{
	int d2 = 0;	// Assume is not D2.

	if ((dev != NULL) && (dev->protocol & DIAG_L1_ISO9141) &&
	    (dev->wm != NULL) && (dev->wm->data[0] == 0x82) &&
	    ((dev->wm->data[1] & 0x80) == 0x00) && 
	    (dev->wm->data[2] == 0x13) && (dev->wm->data[3] == 0xA1)) {
		if ((in == NULL) || (dev->wm->data[1] == (in->addr & 0x7F)))
			d2 = 1;	// Is D2.
	}
	return d2;
}

/*
 * Fastinit & slowinit:
 * ELM should manage slow/fast init automatically... Until the code from levels L1 and up can deal with
 * this, the bus will manually be initialized.
 * Only callable internally. Not for export !!
 * Unsupported by some (all?) clones !! (they handle the init internally, on demand)
 */
static int elm_fastinit(struct diag_l0_device *dl0d) {
	uint8_t *cmds= (uint8_t *) "ATFI\x0D";

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
	          FLFMT "ELM forced fastinit...\n", FL);

	//send command with 1000ms timeout (guessing)
	if (elm_sendcmd(dl0d, cmds, 5, 1000, NULL)) {
		fprintf(stderr, FLFMT "Command ATFI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}


static int elm_slowinit(struct diag_l0_device *dl0d) {
	uint8_t *cmds=(uint8_t *)"ATSI\x0D";

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
	          FLFMT "ELM forced slowinit...\n", FL);

	//huge timeout of 2.8s. Not sure if this is adequate
	//
	// - jonesrh increased to 2.9 sec on 2017-12-07 so "ERROR" appeared after "BUS INIT: ..."
	//   when ignition off and ELM327 v1.5 USB clone is allowed to ATSI.
	// - Users of other clone tools may need to increase slightly further to 3.0, 3.1, etc.
	//
	if (elm_sendcmd(dl0d, cmds, 5, 2900, NULL)) {	// On 2017-12-07, jonesrh increased from 2800 to 2900.
		fprintf(stderr, FLFMT "Command ATSI failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;

}

//elm_bogusinit : send a 01 00 request to force ELM to init bus.
//Only used to force clones to establish a connection... hack-grade because it assumes all ECUs support this.
// non-OBD ECUs may not work with this. ELM clones suck...
// TODO : add argument to allow either a 01 00 request (SID1 PID0, J1979) or 3E (iso14230 TesterPresent) request to force init.
//
// 2017-11-07  jonesrh	Init clones properly in elm_bogusinit when L2 protocol = "D2".
//
// 2017-12-02  jonesrh  Correct problem with not issuing D2 WM for ECUs whose parity forces
//			their ATIIA init address to be different from the target address
//			used by WM and ATSH (eg, VGLA and PSL were not init by my ELM327 v1.5 USB clone).
//
static int elm_bogusinit(struct diag_l0_device *dl0d, unsigned int timeout) {
	int rv;
	struct elm_device *dev;
	uint8_t buf[MAXRBUF];
	uint8_t generic_data[] = {0x01,0x00}; // J1979 request only
	uint8_t iso9141_data[] = {0x68,0x6a,0xf1,0x01,0x00}; // with ISO9141 hdr
	uint8_t d2wm_data[4];	// D2 WM must use this, since D2 use of iso9141_data fails the init.
	const char *err_str;

	dev = dl0d->l0_int;
	if (dev->protocol & DIAG_L1_ISO9141) {
		if (is_d2(dev,NULL)) {
			// Is assumed to be D2 ECU.

			// Use physical ECU-specific D2 WM = {0x82,0x??,0x13,0xA1}, eg, M44 WM = {0x82,0x7A,0x13,0xA1} followed by auto-generated checksum.
			d2wm_data[0] = dev->wm->data[0]; 
			d2wm_data[1] = dev->wm->data[1] & 0x7F;	// Must ensure both WM and ATSH have target addresses that do *not* have the high bit set --
								// - At least that's the case for PSL (ECU 2E), 
								//   and it is probably the case for most ECUs which require high bit set in ATIIA
								//   (in order to avoid triggering the ABS light to blink).
								// - However, the VGLA (ECU 2D) has been seen to be able to communicate in the following situations:
								//   a) ATIIA 2D / ATWM 82 2D 13 A1 / ATSH xx 2D 13 -- volvo850diag (up thru v0.8), or
								//   b) ATIIA AD / ATWM 82 2D 13 A1 / ATSH xx 2D 13 -- freediag, and volvo850diag (v0.9alpha??), or
								//   c) ATIIA AD / ATWM 82 AD 13 A1 / ATSH xx AD 13 -- volvo850diag (v0.9alpha??) temporary VGLA Full Scan test.
								// - I realize that the high bit should already be clear,
								//   assuming a "connect vgla" or "connect 0x2D" command was issued.
								//   But this C statement will ensure it is cleared for certain.
								//   And the statement serves as a good place to document how VGLA can also work as cases a) and c) above.
								// - These 3 stmts can be folded into a memcpy (after having generalized and folded the if into is_d2(),
								//   but I've chosen to keep them here for extreme clarity of what is needed in the WM,
								//   and as a check against someone futzing incorrectly with the is_d2() routine.
			d2wm_data[2] = dev->wm->data[2]; d2wm_data[3] = dev->wm->data[3];

			// The following elm_send() should first issue "ATSH 82 ?? 13" that it derives from the d2wm_data buffer, if necessary.
			// Otherwise the scenario similar to b) above -- high bit set in ATIIA, yet high bit clear in both ATWM and ATSH -- will not apply for VGLA and PSL.

			rv = elm_send(dl0d, d2wm_data, 4);
	    } else {
			// Use 0100 with ISO9141 hdr.
			rv = elm_send(dl0d, iso9141_data, 5);
		}
	} else {
		rv = elm_send(dl0d, generic_data, 2);
	}
	if (rv) {
		return diag_ifwderr(rv);
	}

	// receive everything; we're hoping for a prompt at the end and no error message.
	rv=diag_tty_read(dev->tty_int, buf, MAXRBUF-5, timeout);        //rv=# bytes read
	elm_parse_cr(buf, rv);
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
	          FLFMT "received %d bytes: %.*s: ",
	          FL, rv, rv, buf);

	if (rv<1) {
		//no data or error
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		          FLFMT "ELM did not respond\n", FL);

		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	buf[rv]=0;      //terminate string
	if (buf[rv-1] != '>') {
		//if last character isn't the input prompt, there is a problem
		fprintf(stderr, FLFMT "ELM not ready (no prompt received): %s\n", FL, buf);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	err_str = elm_parse_errors(dl0d, buf);

	if (err_str != NULL) {
		fprintf(stderr, FLFMT "got error while forcing init: %s\n", FL, err_str);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	return 0;

}

// set wakeup message
static int elm_updatewm(struct diag_l0_device *dl0d) {
	struct elm_device *dev;
	uint8_t wm[18];

	dev = (struct elm_device *)dl0d->l0_int;
	sprintf((char *) wm, "ATWM %02X %02X %02X %02X\x0D", dev->wm->data[0], dev->wm->data[1], dev->wm->data[2], dev->wm->data[3]);
	if (elm_sendcmd(dl0d, wm, 17, 500, NULL) < 0) {
		fprintf(stderr, FLFMT "elm_updatewm: ATWM failed\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}

/*
 * Do manual wakeup on the bus, if supported
 * ret 0 if ok
 */
static int elm_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in) {
	const uint8_t *buf;
	int rv = DIAG_ERR_INIT_NOTSUPP;
	int rv_slow_init = 0;		// 2017-12-06  Assists deciding if delay needed before elm_bogusinit() to ensure 2nd init attempt is more likely to succeed.
	int allow_clones_to_ATSI = 1;	// 2017-12-06  Allow users to fairly easily enable/disable the "clone attempts ATSI" behavior.
					//	       1 = Allow clones to attempt ATSI, then fallback to using elm_bogusinit() when necessary.
					//	       0 = Do *not* allow clones to attempt ATSI.  Only use elm_bogusinit().
					//		   This was the only possible clone behavior prior to 2017-12-06.
					//	       * Ideally, there should be a CLI switch for this and it should be flagged in the ELM flags byte
					//	         where ELM_32x_CLONE resides.
					//	       * It is moot if the default setting for production is =0 to mimic the pre 2017-12-06 behavior.
					//		 At least the user can easily change it.
					//	       * I'll leave the value at =1 in any jonesrh-enhanced version of freediag that I make available,
					//	         since that is what works best for my single ELM clone.
					//	       * At least for the time being, this flag will only apply to DIAG_L1_INITBUS_5BAUD,
					//		 until such time that it is known to make sense using it for DIAG_L1_INITBUS_FAST
					//		 (and any other cases that might be applicable).
	unsigned int timeout=0; //for bogus init

	struct elm_device *dev;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
	          FLFMT "ELM initbus type %d\n", FL, in->type);

	dev = (struct elm_device *)dl0d->l0_int;

	if (!dev) {
		return diag_iseterr(rv);
	}

	if (dev->elmflags & ELM_32x_CLONE) {
		if (allow_clones_to_ATSI && in->type == DIAG_L1_INITBUS_5BAUD) {  /* 2017-12-07  Turn into 2 different messages (to tell if flag is set, and so message is more factual. */
			printf("Note : attempting explicit bus init (even though ATSI might not be available on this clone). Errors here are oftentimes ignored.\n");  // 2017-12-07 jonesrh adds different, more factual msg.
		} else {
			printf("Note : explicit bus init assumed not available on clones. Errors here are oftentimes ignored.\n");  // 2017-11-23 jonesrh rewords to be more factual.
		}
		dev->elmflags &= ~ELM_INITDONE; //clear flag
	}

	switch (in->type) {
	case DIAG_L1_INITBUS_FAST: {
		uint8_t fmt, src, tgt;
		uint8_t setproto[]="ATTP5\x0D";
		uint8_t sethdr[15];             //format: "ATSH xx yy zz\x0D"

		fmt=(in->physaddr)? 0x81:0xC1;
		src=in->testerid;
		tgt=in->addr;
		if (dev->elmflags & ELM_327_BASIC) {
			//only needed for 327s (ATTP not available on ELM323)
			//set proto; We assume iso14230 since it's a fast init
			rv=elm_sendcmd(dl0d, setproto, 6, 500, NULL);
			if (rv < 0) {
				break;
			}
		}

		// set wakeup message if applicable
		if (dev->wm != NULL) {
			rv=elm_updatewm(dl0d);
			if (rv < 0) {
				return rv;
			}
		}

		sprintf((char *) sethdr, "ATSH %02X %02X %02X\x0D", fmt, tgt, src);
		rv=elm_sendcmd(dl0d, sethdr, 14, 500, NULL);

		//explicit init is not supported by clones, they wait for the first OBD request...
		if ((dev->elmflags & ELM_32x_CLONE)==0) {
			rv = elm_fastinit(dl0d);
			if (!rv) {
				dev->elmflags |= ELM_INITDONE;
			}
		}               //if explicit init failed we'll try a bogus init anyway
		timeout=1500;
	}                       //case fastinit
	break;                  //case fastinit
	case DIAG_L1_INITBUS_5BAUD:
		//if 327 : set proto first
		if (dev->elmflags & ELM_327_BASIC) {
			if (dev->protocol & DIAG_L1_ISO9141) {
				buf=(uint8_t *) "ATTP3\x0D";
			} else if (dev->protocol & DIAG_L1_ISO14230) {
				buf=(uint8_t *) "ATTP4\x0D";
			} else {         // illegal combination !
				return diag_iseterr(
					DIAG_ERR_INIT_NOTSUPP);
			}

			rv=elm_sendcmd(dl0d, buf, 6, 500, NULL);
			if (rv < 0) {
				break;
			}
		}

		// set wakeup message if applicable
		if (dev->wm != NULL) {
			rv=elm_updatewm(dl0d);
			if (rv < 0) {
				return rv;
			}
		}

		//set init address
		if ((dev->elmflags & ELM_323_BASIC) && (in->addr != 0x33)) {
			fprintf(stderr, FLFMT "elm_initbus: ELM323 doesn't support target address %02X", FL, in->addr);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		if (in->addr != 0x33) {
			uint8_t iia[15];
			sprintf((char *) iia, "ATIIA %02X\x0D", in->addr);
			rv=elm_sendcmd(dl0d, iia, 9, 500, NULL);
			if (rv < 0) {
				fprintf(stderr, FLFMT "elm_initbus: ATIIA %02X failed\n", FL, in->addr);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}

		//accept nonstandard keybytes during init
		if (dev->elmflags & ELM_327_BASIC) {
			buf=(uint8_t *) "ATKW0\x0D";
			rv=elm_sendcmd(dl0d, buf, 6, 500, NULL);
			if (rv < 0) {
				fprintf(stderr,
				        FLFMT
				        "elm_initbus: ATKW0 failed, "
				        "continuing anyway\n",
				        FL);
			}
		}

		//explicit init is not supported by clones
		// Notes by jonesrh: 
		//   1.	I beg to differ.  Explicit init is supported by at least some clones.
		//	The most common clone version that kwpd3b0_interpreter.html has encountered, the ELM327 v1.5,
		//	whether USB or Bluetooth, usually will connect successfully via ATSI to:
		//	a) '96-'98 Volvo 850 D2 ECUs, and
		//	b) to those cars' ECM/TCM for standard OBDII emission diagnostics (0100, 0101, 03, 07).
		//	So the most common clone version *does* (very often) support explicit (L1 = ISO9141) slow init for the cars mentioned.
		//   2.	The easiest way to test that is to move "1.5" temporarily into the elm327_official table.
		//   3.	But I've agreed to retain this test in perpetuity,
		//	because it forces me to "beef up" elm_bogusinit() as much as possible,
		//	so the clone's D2 connection have the highest probability of succeeding (even when ATSI is not used),
		//	and the clone's D2 communication exchange is as successful as possible when only the elm_bogusinit() call is used to connect.
		//   4. However, on 2017-12-06, the allow_clones_to_ATSI flag was introduced so user can optionally allow a clone to attempt an ATSI,
		//      before falling back if necessary on the elm_bogusinit() forced init via D2 WM.
		//      * This way we have the best of both worlds:
		//	- users whose ELM clone *does* indeed work with ATSI, ATKW, ATAL, etc.
		//	  can set allow_clones_to_ATSI = 1 to
		//	  use the ATSI, have a greater likelihood of connection success (since 2 attempts can be made),			//	  see fewer error messages, and consequently see more readable freediag output;
		//	- users whose ELM clone cannot utilize ATSI, can set allow_clones_to_ATSI = 0 
		//	  and revert to pre-2017-12-06 clone init behavior,
		//	  **BUT** still be able to connect for a number of different clones 
		//	  (due to jonesrh enhancements in 2017-11 and 2017-12 to both elm_initbus() and elm_bogusinit()).
		if ((dev->elmflags & ELM_32x_CLONE)==0) {
			rv = elm_slowinit(dl0d);
			if (!rv) {
				dev->elmflags |= ELM_INITDONE;
			}
			rv_slow_init = rv;  // 2017-12-06  Assists deciding if delay needed before elm_bogusinit() to ensure 2nd init attempt is more likely to succeed.
		}
		timeout=4200;           //slow init is slow !

		//query elm for keybytes
		dev->kb1 = 0; dev->kb2 = 0;
		if (dev->elmflags & ELM_INITDONE) {
			uint8_t rxbuf[ELM_BUFSIZE];
			unsigned int kb1, kb2;

			buf=(uint8_t *) "ATKW\x0D";
			rv=elm_sendcmd(dl0d, buf, 5, 500, rxbuf);
			if (rv < 0) {
				fprintf(stderr, FLFMT "elm_initbus: ATKW failed, continuing anyway\n", FL);
				// TODO: for clones without ATKW, can
				// fall back to ATBD. If ATBD response
				// starts with 06 addr 55 xx yy aa bb
				// and aa=yy^0xff and bb=addr^0xff then
				// xx and yy should be key bytes
				rv = 0;
			} else if (sscanf((char *)rxbuf, "1:%02X 2:%02X", &kb1, &kb2) == 2) {
				dev->kb1 = kb1;
				dev->kb2 = kb2;
				in->kb1 = kb1;
				in->kb2 = kb2;
			} else {
				fprintf(stderr, FLFMT "elm_initbus: ATKW failed, continuing anyway\n", FL);
				rv = 0;
			}
		}

		// enable receipt of >7 byte messages, if possible
		//
		// On 2017-11-23, jonesrh ensured ATAL is issued even for clones 
		// (for which elm_slowinit() was not called above)
		// when those clones are to be used with the D2 protocol
		// and therefore will oftentimes require > 7 bytes of data.
		//
		// To Adam G and fenugrec: 
		// The added (in->addr != 0x33) clearly is necessary for clones
		// used by '96-'98 850/S70/V70/C70/XC70 using 5-baud D2 protocol.
		// But is there some other more selective test that would not erroneously affect
		// DIAG_L1_INITBUS_FAST devices or some other DIAG_L1_INITBUS_5BAUD devices
		// that might not need or want ATAL?
		//
		if ((dev->elmflags & ELM_INITDONE) || (in->addr != 0x33)) {
			buf=(uint8_t *) "ATAL\x0D";
			rv=elm_sendcmd(dl0d, buf, 5, 500, NULL);
			if (rv < 0) {
				fprintf(stderr, FLFMT "elm_initbus: ATAL failed, continuing anyway\n", FL);
				rv = 0;
			}
		}

		break;
	default:
		rv = DIAG_ERR_INIT_NOTSUPP;
		break;
	}

	if ((dev->elmflags & ELM_INITDONE)==0) {
		// init not done, either because it's a clone (explicit init unsupported), or another reason.
		// two choices : A) assume comms will be OBD compliant, so request "01 00" has to be supported
		// B) tweak _recv() to skip the "BUS INIT: " line that ELM will send at the next request.
		// A is easier for now...
		// Note that since we don't check for "DIAG_ERR_INIT_NOTSUPP" here, we allow the ELM to (possibly)
		// carry out an incorrect init. I can't see when this can be a problem.
		// Note : clones suck
			//
		// Comments by jonesrh on 2017-12-02:
		// - There's actually more than the 2 choices mentioned above --
		//   eg, can send the D2 keepalive (watchdog message) instead of 0100.
		// - Furthermore, _recv() is already generalized well enough to ignore the "BUS INIT: " line
		//   as long as it does not have errors.  For example, "BUS INIT: ...OK" is handled just fine.
		// - See jonesrh changes in 2017-11 and 2017-12 
		//   to allow his ELM327 v1.5 USB clone to work with elm_bogusinit().
		// - Some clones behave acceptably well and *do* allow ATSI, ATAL, ATKW, etc.
		// - As of 2017-12-02, for clones, I'm still bypassing the call to elm_slowinit()
		//   (and thereby bypassing the ATKW for those clones) 
		//   so that the elm_bogusinit() routine can be made more robust
		//   to work with clones that *do* have D2 connect and communicate capabilities.
		//
		// On 2017-12-06, force 5.5 sec delay before calling bogusinit to increase chances of success
		// (eg, in case ATSI has just failed, as far as the ELM is concerned, but the vehicle's ECU
		// thinks the init worked).
		//
		if (rv_slow_init < 0) {
			// Attempted elm_slowinit(), but it produced an error, so wait plenty of time for any "half-way" connections to disappear.
			fprintf(stderr, FLFMT "Waiting 5.5 sec after ATSI failure before attempting to force init", FL);
			if (is_d2(dev,in)) {
				fprintf(stderr, " (using a D2 WM)");
			}
			fprintf(stderr, "...\n");
			diag_os_millisleep(5500);
		}
		rv=elm_bogusinit(dl0d, timeout);
		if (rv == 0) {
			dev->elmflags |= ELM_INITDONE;
		}
	}

	return rv? diag_iseterr(rv):0;

}

//elm_purge : sends ATI command and checks for a valid prompt. This is faster than ATZ.
//use : if the ELM received garbage before ATI, ex.: "\xFF\xFFATI\r" it will just reject
//the invalid command but still give a valid prompt.
//Return 0 only if a valid prompt was received.
static int elm_purge(struct diag_l0_device *dl0d) {
	uint8_t buf[ELM_BUFSIZE] = "ATI\x0D";
	int rv;
	struct elm_device *dev = dl0d->l0_int;

	if (diag_tty_write(dev->tty_int, buf, 4) != 4) {
		fprintf(stderr, FLFMT "elm_purge : write error\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	rv = diag_tty_read(dev->tty_int, buf, sizeof(buf), ELM_PURGETIME);
	if (rv < 1) {
		return DIAG_ERR_GENERAL;
	}

	if (buf[rv-1] != '>') {
		DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		              buf, rv, FLFMT "elm_purge: got ", FL);

		return DIAG_ERR_GENERAL;
	}
	return 0;
}

/*
 * Send a load of data
 *
 * Directly send hex-ASCII; exit without receiving response.
 * Upper levels don't append 0x0D / 0x0A at the end, we take care of adding the required 0x0D.
 * "AT"* commands sould not be sent with this function.
 * Returns 0 on success
 */

static int elm_send(struct diag_l0_device *dl0d, const void *data, size_t len) {
	uint8_t buf[ELM_BUFSIZE];
	struct elm_device *dev = dl0d->l0_int;
	//ssize_t xferd;
	int rv;
	unsigned int i;

	if (len == 0) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if ((dev->protocol & DIAG_L1_ISO9141) && len <= 3) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if ((2*len)>(ELM_BUFSIZE-1)) {
		//too much data for buffer size
		fprintf(stderr, FLFMT "ELM: too much data for buffer (report this bug please!)\n", FL);
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
	          FLFMT "ELM: sending %d bytes\n", FL, (int) len);

	if ((dev->protocol & DIAG_L1_ISO9141) && memcmp(dev->atsh, data, 3)) {
		sprintf((char *)buf, "ATSH %02X %02X %02X\x0D",
		        (unsigned int)((uint8_t *)data)[0],
		        (unsigned int)((uint8_t *)data)[1],
		        (unsigned int)((uint8_t *)data)[2]);
		rv=elm_sendcmd(dl0d, buf, 14, 500, NULL);
		if (rv < 0) {
			fprintf(stderr, FLFMT "elm_send: ATSH failed\n",FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

		// if ISO9141 protocol setting with KWP message format,
		// adjust receive filter
		if ((dev->atsh[0] & 0x80) &&
		    (dev->atsh[2] == (unsigned int)((uint8_t *)data)[2])) {
			// already sent ATSR for this address
		} else if ((unsigned int)((uint8_t *)data)[0] & 0x80) {
			sprintf((char *)buf, "ATSR %02X\x0D",
			        (unsigned int)((uint8_t *)data)[2]);
			rv=elm_sendcmd(dl0d, buf, 8, 500, NULL);
			if (rv < 0) {
				fprintf(stderr, FLFMT "elm_send: ATSR failed\n",FL);
				return diag_iseterr(DIAG_ERR_GENERAL);
			}
		}

		memcpy(dev->atsh, data, 3);
	}

	for (i=0; i<len; i++) {
		//fill buffer with ascii-fied hex data
		snprintf((char *) &buf[2*i], 3, "%02X", (unsigned int)((uint8_t *)data)[i] );
	}
	i=2*len;
	buf[i]=0x0D;
	buf[i+1]=0x00;  //terminate string

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
	          FLFMT "ELM: (sending string %s)\n", FL, (char *) buf);

	if (dev->protocol & DIAG_L1_ISO9141) {
		i -= 6;
		rv=diag_tty_write(dev->tty_int, buf+6, i+1); // skip header
	} else {
		rv=diag_tty_write(dev->tty_int, buf, i+1);
	}
	if (rv != (int) (i+1)) {        //XXX danger ! evil cast !
		fprintf(stderr, FLFMT "elm_send:write error\n",FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	return 0;
}

/*
 * Get data (blocking), returns number of bytes read, between 1 and len
 * ELM returns a string with format "%02X %02X %02X[...]\n" . But it's slow so we add ELM_SLOWNESS ms to the specified timeout.
 * We convert this received ascii string to hex before returning.
 * note : "len" is the number of bytes read on the OBD bus, *NOT* the number of ASCII chars received on the serial link !
 * TODO decode possible error strings ! Essential because ELM can reply
 * 	"FB ERROR" (FB) is a valid hex number
 * 	"ACT ALARM" (AC) is a valid hex number
 * technique:
 *	-if any hexpair doesn't decode cleanly, discard message
 *	-just before returning any data, search for an error message match
 *	-still missing : if caller requests 1 byte, and the error is "FB ERROR", we'll still happily return 0xFB !
 * new (on and after 2017-12-02) technique:
 *	-if any hexpair doesn't decode cleanly, do one of two things:
 *	     a)	UGLY BUT **VERY** PRACTICAL HACK:
 *	        if the decoding problem is due to encountering " <DATA ERROR" suffix 
 *	        **and** is for a D2 ecu 
 *	        **and** if there are no other error messages involved, then:
 *		- ignore (and discard) that " <DATA ERROR" suffix,
 *		- assume the decoded data is valid (even though it likely involves 2 or more complete messages with valid checksums),
 *		- return the data to the l1 caller, who will return it up to 
 *		  the l2 caller, who will evaluate if the checksums are valid, 
 *		  and who will decide what to do with concatenated responses --
 *		  the most typical thing being ignoring any prefixed "7E xx 23" (temporary delay) responses;
 *	     b)	if the decoding problem is for any other situation, then revert to the old technique of discarding the message.
 *	-just before returning any data, search for an error message match
 *	-still missing : if caller requests 1 byte, and the error is "FB ERROR", we'll still happily return 0xFB !
 *
 * TODO: improve "len" semantics for L0 interfaces that do framing, such as this. Currently this returns max 1 message, to
 * let L2 do another call to get further messages (typical case of multiple responses)
 */
static int elm_recv(struct diag_l0_device *dl0d, void *data, size_t len, unsigned int timeout) {
	int rv, xferd, xferd_saved_when_data_error;
	struct elm_device *dev = dl0d->l0_int;
	uint8_t rxbuf[3*MAXRBUF +1];    //I think some hotdog code in L2/L3 calls _recv with MAXRBUF so this needs to be huge.
	//the +1 is to \0-terminate the buffer for elm_parse_errors() to work

	unsigned long t0,tf;    //manual timeout control
	int steplen;    /* bytes per read */
	int wp, rp;     /* write & read indexes in rxbuf; a type of FIFO */
	const char *err;

	if ((!len) || (len > MAXRBUF)) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	t0=diag_os_getms();
	tf=t0+timeout + ELM_SLOWNESS;   //timeout when tf is reached

	steplen=2;
	wp=0;
	rp=0;
	xferd=0;
	xferd_saved_when_data_error=0;  // Part of 2017-12-02 "try to use valid D2 concatenated messages suffixed with <DATA ERROR" hack.
	rxbuf[0] = 0x00;        //null-terminate

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
	          FLFMT "Expecting 3*%d bytes from ELM, %u ms timeout(+400)...",
	          FL, (int)len, timeout);

	while (1) {
		unsigned long tcur;
		/* technique : try to read hexpairs (2 bytes); split messages according to
		 * spacing chars (">\r\n")
		 */
		tcur = diag_os_getms();
		if (tcur >= tf) {
			/* timed out : */
			goto pre_exit;
		}
		timeout = tf - tcur;

		rv = diag_tty_read(dev->tty_int, rxbuf+wp, steplen, timeout);
		if (rv == DIAG_ERR_TIMEOUT) {
			goto pre_exit;
		}

		if (rv <= 0) {
			fprintf(stderr, FLFMT "elm_recv error\n", FL);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}

		wp += rv;       /* position for next tty_read */
		rxbuf[wp]=0;            // '\0'-terminate "string"

		int skipc;      /* chars to skip */

		skipc = strspn((char *)(&rxbuf[rp]), " ");      /* skip contig spaces */
		rp += skipc;
		/* line end ? */
		skipc=strspn((char *)(&rxbuf[rp]), "\r\n>");
		rp += skipc;
		if (skipc > 0) {
			/* definitely a line-end / prompt ! return data so far, if any */
			if (xferd > 0) {
				goto pre_exit;
			}
		}

		if (strlen((char *)(&rxbuf[rp])) < 2) {
			/* probably incomplete hexpair. */
			steplen=1;
			continue;
		}

		unsigned int rbyte;
		if (sscanf((char *)(&rxbuf[rp]), "%02X", &rbyte) == 1) {
			/* good hexpair */
			((uint8_t *)data)[xferd]=(uint8_t) rbyte;
			xferd++;
			if ( (size_t)xferd==len) {
				goto pre_exit;
			}
		} else {
			/* didn't scanf properly... error message ? ex. "NO DATA\r>"
			 * NO DATA is pretty inoffensive; skip printing that one */
			int bypass_logging_elm_error_envir;
			if (!(rxbuf[rp] == 'N' && rxbuf[rp+1] == 'O')) {
				// This block has been expanded to clearly log the 2 bytes in order to help understand why
				// the 2 bytes are not matching the subsequent ELM327 error message that is printed
				// after the 20171114 changes occurred to adapt to intervening "7E xx 23" responses.
				// - In particular, many cases are recorded of seeing "ST",
				//   but the subsequently printed ELM327 error messages seem to be random -
				//   eg, BUS ERROR, STOPPED, FB ERROR, etc.
				// - I believe the introduction of the 2017-11-26 change to delay at least 60 ms
				//   after most requests has eliminated the BUS ERROR and FB ERROR manifestations,
				//   and has greatly lessened the STOPPED manifestations.
				if (0) {	// The old logging method.
					fprintf(stderr, FLFMT "ELM sscanf failed to eat '%s'\n", FL, rxbuf);
				}
				else if (!(rxbuf[rp] == '<' && rxbuf[rp+1] == 'D') || (diag_l1_debug & DIAG_DEBUG_DATA) || (diag_l0_debug & DIAG_DEBUG_DATA)) {
					// STOPPED and all other errors (besides "NO DATA" and "<DATA ERROR") have their 1st two chars logged here.
					// The "<DATA ERROR" case is almost bypassed like "NO DATA" (since it is transparently recovered from almost always),
					//   but it's not totally bypassed here, since it is still logged when debugging is turned on.
					fprintf(stderr, FLFMT "ELM sscanf failed to convert [", FL);
					diag_data_dump(stderr, &rxbuf[rp], 2);
					fprintf(stderr, "], ie, string '%s'\n", (char *) &rxbuf[rp]);
					if ((0) && ((diag_l1_debug & DIAG_DEBUG_DATA) || (diag_l0_debug & DIAG_DEBUG_DATA))) {
						// This is now unnecessary almost always [thus the (0)], since it shows an incomplete view of the entire buffer, 
						// but is retained in case programmer needs it again, eg, to see rp and wp along with the buffer in hex,
						// [and would thus temporarily change (0) to (1), or change && to ||, depending on the situation].
						fprintf(stderr, FLFMT "rp=%d, wp=%d, rxbuf (thru wp) = [", FL, rp, wp);
						diag_data_dump(stderr, rxbuf, wp);
						fprintf(stderr, "]\n");
					}
				}
				bypass_logging_elm_error_envir = 0;
			} else {
				bypass_logging_elm_error_envir = 1;
			}
			/* finish pulling the error message or whatever garbage */
			rv = diag_tty_read(dev->tty_int, rxbuf+wp, sizeof(rxbuf) - wp, ELM_SLOWNESS);
			if (rv >= 0) {
				rxbuf[wp + rv] = 0x00;
			}
			if (!bypass_logging_elm_error_envir && ((diag_l1_debug & DIAG_DEBUG_DATA) || (diag_l0_debug & DIAG_DEBUG_DATA))) {
				// The entire rxbuf is logged in a (usually) readable fashion here when debugging is turned on -- except for NO DATA -- 
				// in order to get an overview of the entire buffer,
				// including any previously converted hex pairs, any error message, and any subsequent garbage.
				// I realize it is fuzzy about exactly what is discarded,
				// but with the new reliability enhancements in late 2017-11,
				// that's probably a moot point, since a quick recovery will usually be automatic,
				// and since it's really better to see the entire buffer --
				// both the portion shown in the original "ELM sscanf failed to eat" message, ie, up to the ELM error,
				// in addition to the full ELM error message and any subsequent garbage,
				// which by this time has been "eaten".
				// This message is meant to follow the "ELM sscanf failed to convert" message,
				// thus it is tied together with it via the bypass_logging_elm_error_envir flag.
				if (rxbuf[rp] == '<' && rxbuf[rp+1] =='D')
					fprintf(stderr, FLFMT "in rxbuf (as string, including error message, etc, shown on next line) = \n'%s'\n", FL, (char *) rxbuf);
				else
					fprintf(stderr, FLFMT "in rxbuf (as string, including error message, etc) = '%s'\n", FL, (char *) rxbuf);
			}
			if ((0) && !bypass_logging_elm_error_envir && ((diag_l1_debug & DIAG_DEBUG_DATA) || (diag_l0_debug & DIAG_DEBUG_DATA))) {
				// This is probably superfluous now, even when DIAG_DEBUG_DATA is set [thus the (0)], 
				// but is retained in case programmer needs it again for some quick debugging
				// [and would thus temporarily change (0) to (1), or change && to ||, depending on the situation].
				// This message is meant to follow the immediately preceding "in rxbuf (as string, including..." message.
				if ((strcmp((char *)rxbuf,"STOPPED") != 0) && (strncmp((char *)&rxbuf[rp],"<DATA ERROR",11) != 0)) {
					fprintf(stderr, FLFMT "in rxbuf (in hex) = [", FL);
					diag_data_dump(stderr, rxbuf, strlen((char *) rxbuf));
					fprintf(stderr, "]\n");
				}
			}
			if (rxbuf[rp] == '<' && rxbuf[rp+1] == 'D') {
				// Determine if D2 ECU using close to the same approach used in elm_bogusinit()
				// [since I don't yet know how to retrieve the D2 info from L2 structures] --
				// ie, if physical ECU-specific WM = {0x82,0x??,0x13,0xA1}, 
				// eg, M44 WM = {0x82,0x7A,0x13,0xA1} -- before the auto-generated checksum --
				// then assume this is a D2 ECU.
				// ****************************************************************************************************
				// To Adam G and/or fenugrec:
				// 1. Should this kind of check for L1=ISO9141 / L2=D2 be performed better/legally/more appropriately/quicker via:
				//        if ((global_cfg.L1proto == DIAG_L1_ISO9141) && (global_cfg.L2proto == DIAG_L2_PROT_D2))
				//    without using dev, both here and in elm_bogusinit()?
				//    It would require adding scantool_cli.h.
				// 2. Is there a better way for elm_bogusinit() and elm_recv() to verify that this is a D2 ECU?
				// 3. I couldn't figure out how to access in->addr, so the dev->wm->data[1] oriented test
				//    used in elm_bogusinit():
				//	  ((dev->wm->data[1] & 0x7F) == (in->addr & 0x7F))
				//    became the mildly useful test:
				//	  ((dev->wm->data[1] & 0x80) == 0x00)
				//    which at least checks that the WM target's high bit is not set,
				//    which should be the case for a D2 WM.
				// 4. As of 2017-12-06, the D2 tests have been generalized and folded into is_d2().
				// ****************************************************************************************************
				if (is_d2(dev,NULL)) {
					// Part of 2017-12-02 "try to use valid D2 concatenated messages suffixed with <DATA ERROR" hack.
					xferd_saved_when_data_error = xferd;
				}
			}
			xferd = DIAG_ERR_GENERAL;
			goto pre_exit;
		}
		/* here, we just sscanf'd 2 bytes, so we read 1 more. */
		/* we can't read 2 more, in case we're in a multi-message, for instance if we just decoded 0x00 in
		 * "48 6A 01 00\n48 6A..." , reading two bytes "\n4" would corrupt the next message !
		 */
		rp += 2;
		steplen=1;
	}       // while (1)

pre_exit:
	err = elm_parse_errors(dl0d, rxbuf);
	if (err) {
		if (strcmp(err, "NO DATA") == 0) {
			return DIAG_ERR_TIMEOUT;
		}
		if (strcmp(err, "<DATA ERROR") != 0) {
			fprintf(stderr, FLFMT "ELM error: %s\n", FL, err);
			return diag_iseterr(DIAG_ERR_GENERAL);
		}
		xferd = xferd_saved_when_data_error;  // Part of 2017-12-02 "try to use valid D2 concatenated messages suffixed with <DATA ERROR" hack.
	}
	return (xferd>0)? xferd:DIAG_ERR_TIMEOUT;
}


/* set new wakeup message, if possible */
static int elm_setwm(struct diag_l0_device *dl0d, struct diag_msg *pmsg) {
	struct elm_device *dev;

	dev = (struct elm_device *)dl0d->l0_int;

	if (dev->elmflags & ELM_INITDONE) {
		/*
		 * It's possible to change the wakeup message after init,
		 * but we don't currently implement this.
		 */
		fprintf(stderr, FLFMT "elm_setwm: tried to set wakeup message after init\n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (dev->elmflags & ELM_323_BASIC) {
		fprintf(stderr, FLFMT "elm_setwm: ELM323 doesn't support setting wakeup message\n", FL);
		return diag_iseterr(DIAG_ERR_IOCTL_NOTSUPP);
	}

	if (pmsg->len != 4) {
		fprintf(stderr, FLFMT "elm_setwm: invalid message length %u\n", FL, pmsg->len);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (dev->wm != NULL) {
		diag_freemsg(dev->wm);
	}
	dev->wm = diag_dupsinglemsg(pmsg);
	if (dev->wm == NULL) {
		return diag_iseterr(DIAG_ERR_NOMEM);
	}
	return 0;
}


static uint32_t elm_getflags(struct diag_l0_device *dl0d) {

	struct elm_device *dev;
	uint32_t flags=0;

	dev = (struct elm_device *)dl0d->l0_int;

	flags = DIAG_L1_DATAONLY | DIAG_L1_AUTOSPEED | DIAG_L1_DOESP4WAIT |
	        DIAG_L1_DOESL2FRAME | DIAG_L1_DOESL2CKSUM | DIAG_L1_DOESFULLINIT | DIAG_L1_DOESKEEPALIVE;

	switch (dev->protocol) {
	case DIAG_L1_ISO9141:
		flags |= DIAG_L1_SLOW;
		flags &= ~DIAG_L1_DATAONLY;
		//flags |= DIAG_L1_NOHDRS;	//probably not needed since we send ATH1 on init (enable headers)
		break;
	case DIAG_L1_ISO14230:
		flags |= DIAG_L1_SLOW | DIAG_L1_FAST | DIAG_L1_PREFFAST;
		//flags |= DIAG_L1_NOHDRS;
		break;
	default:
		break;
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_PROTO, DIAG_DBGLEVEL_V,
	          FLFMT "getflags link %p proto %d flags 0x%X\n",
	          FL, (void *)dl0d, dev->protocol, flags);

	return flags;

}

//elm_parse_cr : change 0x0A to 0x0D in datastream.
static void elm_parse_cr(uint8_t *data, int len) {
	int i=0;
	for (; i<len; i++) {
		if (*data == 0x0D) {
			*data = 0x0A;
		}
		data++;
	}
	return;
}


static int elm_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	int rv = 0;
	struct elm_device *dev;

	switch (cmd) {
	case DIAG_IOCTL_IFLUSH:
		// 2017-12-10  jonesrh  Add new receive buffer flushing (for same reason diag_tty_iflush was previously added to _sendcmd and _open).
		//			- This should at least be used for D2 ECUs, so scantool_850.c can issue a L2 _IOCTL_IFLUSH before a retry,
		//			  thereby hopefully eliminating the "at least one old response awaiting to be read" problem
		//			  that begins after a STOPPED response.
		if ((0) || ((diag_l1_debug & DIAG_DEBUG_DATA) || (diag_l0_debug & DIAG_DEBUG_DATA))) {
			fprintf(stderr, "flushing any unread input...\n");
		}
		dev = (struct elm_device *)dl0d->l0_int;
		diag_tty_iflush(dev->tty_int);	/* Flush unread input, if any. */

		rv = 0;
		break;
	case DIAG_IOCTL_INITBUS:
		rv = elm_initbus(dl0d, (struct diag_l1_initbus_args *)data);
		break;
	case DIAG_IOCTL_SETWM:
		rv = elm_setwm(dl0d, (struct diag_msg *)data);
		break;
	default:
		rv = DIAG_ERR_IOCTL_NOTSUPP;
		break;
	}

	return rv;
}


const struct diag_l0 diag_l0_elm = {
	"Scantool.net ELM32x Chipset Device",
	"ELM",
	DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_J1850_PWM | DIAG_L1_J1850_VPW | DIAG_L1_CAN,
	elm_init,
	elm_new,
	elm_getcfg,
	elm_del,
	elm_open,
	elm_close,
	elm_getflags,
	elm_recv,
	elm_send,
	elm_ioctl
};
