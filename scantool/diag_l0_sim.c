/*
 *	freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2004 Vasco Nevoa.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * This is not an interface driver like all the other L0 files.
 * It implements a "Car Simulator" instead of a true ECU interface.
 * The intention is to free freediag from the need of an actual car,
 * when all you want to do is to test a protocol stack or a client
 * application.
 *
 * This is implemented as L0 and not L1, 2, or 3, because this way it allows
 * us to test the protocol stack as well, not just the applications.
 *
 * In this L0 "pseudo-driver", the serial port is not used, and in it's
 * place is a simple file, called "freediag_carsim.db". This file holds
 * one or more responses for each OBDII request. Feel free to enlarge
 * that file with valid information for your case, customise it at will
 * for your own tests. The format is pretty raw (message bytes in hexadecimal),
 * with allowance for comments (lines started with "#") and a very small and
 * rigid syntax (check the comments in the file).
 * 
 */
#include <unistd.h> // POSIX stuff

#include <errno.h> 
#include <stdlib.h>
#include <string.h> // str**()
#include <math.h> // sin()

#include "diag.h"
#include "diag_err.h"
#include "diag_tty.h"
#include "diag_l1.h"



/**************************************************/
// LOCAL DATATYPES AND GLOBALS:
/**************************************************/


extern const struct diag_l0 diag_l0_sim;
const char *simfile=NULL;	//pointer to remote filename.
//this must be set externally either through "set simfile" from the scantool cli, or
//by calling _set_simfile thru libdiag.
const char *simfile_default=DB_FILE;	//default filename


struct diag_l0_sim_device
{
	int protocol;
	struct diag_serial_settings serial; //for compatibility;
	FILE* fp; // DB file pointer.
};

// Global init flag.
static int diag_l0_sim_initdone;

// ECU responses linked list:
struct sim_ecu_response
{
	char *text; // unparsed text for the response!
	uint8_t *data; // parsed final response.
	uint8_t len; // final response length.
	struct sim_ecu_response* next;
};

// For keeping all the responses to the last request.
struct sim_ecu_response* sim_last_ecu_responses = NULL;

// Configuration variables.
// These affect the kind of flags we should return.
// This makes the simulator configurable towards using
// or not the L2 framing and CRC/Checksums.
// These boolean flags are to be programmed with values
// from the DB file in use.
char sim_skip_frame = 0;
char sim_skip_crc = 0;


/**************************************************/
// FORWARD DECLARATIONS:
/**************************************************/


static int
diag_l0_sim_send(struct diag_l0_device *dl0d,
		 const char *subinterface __attribute__((unused)),
		 const void *data, size_t len);
static int
diag_l0_sim_recv(struct diag_l0_device *dl0d,
		 const char *subinterface __attribute__((unused)),
		 void *data, size_t len, int timeout);

extern void
diag_l0_sim_setfile(char * fname);

/**************************************************/
// LOCAL FUNCTIONS:
/**************************************************/


// Allocates one new ecu response and fills it with given text.
struct sim_ecu_response*
sim_new_ecu_response_txt(const char* text)
{
	struct sim_ecu_response *resp;
	int rv;

	//~ resp = calloc(1, sizeof(struct sim_ecu_response));
	if (rv=diag_calloc(&resp, 1))
		return (struct sim_ecu_response *)diag_pseterr(rv);
	
	resp->data = NULL;
	resp->len = 0;
	resp->text = NULL;
	resp->next = NULL;
	
	if (text != NULL && strlen(text)) {
		// resp->text = calloc(strlen(text)+1, sizeof(char));
		if (rv=diag_calloc(&(resp->text), strlen(text)+1))
			return (struct sim_ecu_response *)diag_pseterr(rv);
		
		strncpy(resp->text, text, strlen(text));
	}

	return resp;
}

// Allocates one new ecu response and fills it with given data.
// (not used yet, here for "just in case")
struct sim_ecu_response*
sim_new_ecu_response_bin(const uint8_t* data, const uint8_t len)
{
	struct sim_ecu_response *resp;

	resp = calloc(1, sizeof(struct sim_ecu_response));
	resp->data = NULL;
	resp->len = 0;
	resp->text = NULL;
	resp->next = NULL;

	if (len > 0 && data != NULL) {
		resp->data = calloc(len, sizeof(uint8_t));
		memcpy(resp->data, data, len);
		resp->len = len;
	}

	return resp;
}

// Frees an ecu response and returns the next one in the list.
struct sim_ecu_response*
sim_free_ecu_response(struct sim_ecu_response** resp)
{
	struct sim_ecu_response* next_resp = NULL;

	if (*resp == NULL)
		return next_resp;

	//get pointer to next one.
	next_resp = (*resp)->next;

	//free this one.
	(*resp)->len = 0;
	(*resp)->next = 0;
	free((*resp)->data);
	free(*resp);
	resp = NULL;

	//return next one.
	return next_resp;
}


// Frees all responses from the given one until the end of the list.
void sim_free_ecu_responses(struct sim_ecu_response** resp_pp)
{
	struct sim_ecu_response** temp_resp_pp = resp_pp;
	uint8_t count = 0;

	while (*temp_resp_pp != NULL) {
		*temp_resp_pp = sim_free_ecu_response(temp_resp_pp);
		count++;
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE)
		fprintf(stderr, FLFMT " %d responses freed from queue.\n", FL, count);
}

// for debug purposes.
void sim_dump_ecu_responses(struct sim_ecu_response* resp_p)
{
	struct sim_ecu_response* temp_resp_p = resp_p;
	uint8_t count = 0;

	while (temp_resp_p != NULL) {
		fprintf(stderr, FLFMT "response #%d: %s", FL, count, temp_resp_p->text);
		temp_resp_p = temp_resp_p->next;
		count++;
	}

	fprintf(stderr, FLFMT "%d responses in queue.\n", FL, count);
}


// Builds a list of responses for a request, by finding them in the DB file.
void sim_find_responses(struct sim_ecu_response** resp_pp, FILE* fp, const uint8_t* data, const uint8_t len)
{
#define TAG_REQUEST "RQ"
#define TAG_RESPONSE "RP"

	uint8_t resp_count = 0;
	uint8_t new_resp_count = 0;
	uint8_t synth_req[11]; // 11 request bytes.
	char line_buf[1280+1]; // 255 response bytes * 5 ("0xYY ") + tolerance for a token ("abc1 ") = 1280.

	// walk to the end of the list (last valid item).
	struct sim_ecu_response* resp_p = *resp_pp;
	if (resp_p != NULL) {
		resp_count++;
		while (resp_p->next != NULL) {
			resp_p = resp_p->next;
			resp_count++;
		}
	}

	// go to the beginning of the DB file.
	char end_responses = 0;
	char request_found = 0;
	rewind(fp);

	// search for the given request.
	while (!request_found && !end_responses) {
		// get a line from DB file.
		if (fgets(line_buf, 1281, fp) == NULL)
		{
			// EOF reached.
			break;
		}
		// ignore all lines except requests.
		if (strncmp(line_buf, TAG_REQUEST, strlen(TAG_REQUEST)) != 0)
			continue;
		// synthesize up to 11 byte values from DB request line.
		memset(synth_req, 0, 11);
		int num = sscanf(line_buf+3, "%x %x %x %x %x %x %x %x %x %x %x",
			 &synth_req[0], &synth_req[1], &synth_req[2], &synth_req[3],
			 &synth_req[4], &synth_req[5], &synth_req[6], &synth_req[7],
			 &synth_req[8], &synth_req[9], &synth_req[10]);
		// compare given request with synthesized DB file request.
		if (memcmp(data, synth_req, MIN(len, num)) == 0) {
			// got a match, now cycle the following lines for responses.
			request_found = 1;
			while (!end_responses) {
				// get a line from file.
				if (fgets(line_buf, 1281, fp) == NULL) {
					// EOF reached.
					end_responses = 1;
					break;
				}
				// ignore all lines except responses.
				if (strncmp(line_buf, TAG_RESPONSE, strlen(TAG_RESPONSE)) != 0) {
					// if it's another request, then end the list.
					if (strncmp(line_buf, TAG_REQUEST, strlen(TAG_REQUEST)) == 0) {
						end_responses = 1;
						break;
					} else {
						continue;
					}
				}
				// add the new response (without the tag).
				if (resp_count + new_resp_count == 0) {
					// create the root of the list.
					resp_p = *resp_pp = sim_new_ecu_response_txt(line_buf + strlen(TAG_RESPONSE) + 1);
					if (!resp_p) {
						fprintf(stderr, FLFMT "Could not add new response \"%s\"\n", FL, line_buf + strlen(TAG_RESPONSE) + 1);
						end_responses=1;
					}
				} else {
					// add to the end of the list.
					resp_p->next = sim_new_ecu_response_txt(line_buf + strlen(TAG_RESPONSE) + 1);
					if (!(resp_p->next)) {
						fprintf(stderr, FLFMT "Could not add new response \"%s\"\n", FL, line_buf + strlen(TAG_RESPONSE) + 1);
						end_responses=1;
					}
					resp_p = resp_p->next;
				}
				new_resp_count++;
			}
		}
	}

	if (diag_l0_debug & DIAG_DEBUG_DATA)
		fprintf(stderr, FLFMT "%d responses queued for receive, %d new.\n", FL, resp_count+new_resp_count, new_resp_count);
}

// Returns the ISO9141 checksum of a byte sequence.
uint8_t cs1(uint8_t *data, uint8_t pos)
{
	return diag_l2_proto_iso9141_cs(data, pos-1);
}

// Returns a value between 0x00 and 0xFF calculated as the trigonometric
// sine of the current system time (with a period of one second).
uint8_t sine1(uint8_t *data, uint8_t pos)
{
	struct timeval now;
	(void)gettimeofday(&now, NULL);
	return 0xFF * sin(now.tv_usec / 1000000 * 6.283185);
}

// Returns a value between 0x00 and 0xFF directly proportional
// to the value of the current system time (with a period of one second).
uint8_t sawtooth1(uint8_t *data, uint8_t pos)
{
	struct timeval now;
	(void)gettimeofday(&now, NULL);
	return 0xFF * now.tv_usec / 1000000;
}

// Parses a response's text to data.
// Replaces special tokens with function results.
void sim_parse_response(struct sim_ecu_response* resp_p)
{
// the magic number here is "5": all elements in a file line
// are exactly 5 characters long ("0x00 ", "cks1 ", etc).
// don't screw this! :)
#define TOKEN_SINE1	 "sin1"
#define TOKEN_SAWTOOTH1 "swt1"
#define TOKEN_ISO9141CS "cks1"

	uint8_t synth_resp[255];	// 255 response bytes.
	char token[5];	// to get tokens from the text.

	// extract byte values from response line, allowing for tokens.
	memset(synth_resp, 0, 255);
	uint8_t pos = 0;
	char* parse_offset = NULL;
	int ret = 0;
	do {
		if (pos*5 >= strlen(resp_p->text))
			break;
		// scan an element.
		parse_offset = resp_p->text + pos*5;
		ret = sscanf(parse_offset, "%s", token);
		// try replacing a token with a calculated value.
		if (strcmp(token, TOKEN_SINE1) == 0)
			synth_resp[pos] = sine1(synth_resp, pos);
		else if (strcmp(token, TOKEN_SAWTOOTH1) == 0)
			synth_resp[pos] = sawtooth1(synth_resp, pos);
		else if (strcmp(token, TOKEN_ISO9141CS) == 0)
			synth_resp[pos] = cs1(synth_resp, pos);
		else
			// failed. try scanning element as an Hex byte.
			if ((ret = sscanf(parse_offset, "%x", &synth_resp[pos])) != 1) {
				// failed. something's wrong.
				fprintf(stderr, FLFMT "Error parsing line: %s at position %d.\n", FL, resp_p->text, pos*5);
				break;
			}
		// next byte.
		pos++;
	} while (ret != 0);

	// copy to user.
	//resp_p->data = calloc(pos, sizeof(uint8_t));
	if (diag_calloc(&(resp_p->data),pos)) {
		fprintf(stderr, FLFMT "Error parsing response\n", FL);
		return;
	}
	memcpy(resp_p->data, synth_resp, pos);
	resp_p->len = pos;
}

// Reads the configuration options from the file.
// Stores them in globals.
void sim_read_cfg(FILE *fp)
{
	char *p; // temp string pointer.
	char line_buf[21]; // 20 chars generally enough for a config token.

#define TAG_CFG "CFG"
#define CFG_NOL2FRAME "SIM_NOL2FRAME"
#define CFG_NOL2CKSUM "SIM_NOL2CKSUM"

sim_skip_crc = 0;
sim_skip_frame = 0;

	// search for all config lines.
	while (1) {
		// get a line from DB file.
		if (fgets(line_buf, 20, fp) == NULL) {
			// EOF reached.
			break;
		}
		// ignore all lines except configs.
		if (strncmp(line_buf, TAG_CFG, strlen(TAG_CFG)) != 0)
			continue;
		// get the config values.
		p = line_buf + strlen(TAG_CFG) + 1;
		
		if (strncmp(p, CFG_NOL2FRAME, strlen(CFG_NOL2FRAME)) == 0) {
			// "no l2 frame":
			p += strlen(CFG_NOL2FRAME) + 1;
			sscanf(p, "%d", &sim_skip_frame);
			continue;
		} else if (strncmp(p, CFG_NOL2CKSUM, strlen(CFG_NOL2CKSUM)) == 0) {
			// "no l2 checksum":
			p += strlen(CFG_NOL2CKSUM) + 1;
			sscanf(p, "%d", &sim_skip_crc);
			continue;
		}
	}
}

/**************************************************/
// INTERFACE FUNCTIONS:
/**************************************************/


// Initializes the simulator.
static int
diag_l0_sim_init(void)
{
	sim_free_ecu_responses(&sim_last_ecu_responses);

	if (diag_l0_sim_initdone)
	return 0;
	
	diag_l0_sim_initdone = 1;
	if (!simfile)
		//not filled in yet : use default DB_FILE.
		simfile=simfile_default;

	return 0;
}


// Opens the simulator DB file.
static struct diag_l0_device *
diag_l0_sim_open(const char *subinterface, int iProtocol)
{
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l0_sim_device *dev;

	// If we're doing debugging, print to stderr
	if (diag_l0_debug & DIAG_DEBUG_OPEN)
		fprintf(stderr, FLFMT "open subinterface %s protocol %d\n", FL, subinterface, iProtocol);

	diag_l0_sim_init();

	// Create diag_l0_sim_device:
	if (rv=diag_calloc(&dev, 1))
		return (struct diag_l0_device *)diag_pseterr(rv);

	dev->protocol = iProtocol;

	// Create diag_l0_device:
	if (rv=diag_calloc(&dl0d, 1))
		return (struct diag_l0_device *)diag_pseterr(rv);

	dl0d->fd = -1;
	dl0d->dl0_handle = dev;
	dl0d->dl0 = &diag_l0_sim;
	if (rv=diag_calloc(&dl0d->name, strlen(simfile)+1)) {
		free(dl0d);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}
	strcpy(dl0d->name, simfile);
	if (rv=diag_calloc(&dl0d->ttystate, 1))  {
		free(dl0d);
		return (struct diag_l0_device *)diag_pseterr(rv);
	}
	// Open the DB file:
	if ((dev->fp = fopen(dl0d->name, "r")) == NULL) {
		fprintf(stderr, FLFMT "Unable to open file \"%s\"\n", FL, dl0d->name);
		free(dl0d);
		return (struct diag_l0_device *)diag_pseterr(DIAG_ERR_GENERAL);
	}

	dl0d->fd = 1;
	rewind(dev->fp);

	// Read the configuration flags from the db file:
	sim_read_cfg(dev->fp);

	return dl0d;
}


// Closes the simulator DB file.
static int
diag_l0_sim_close(struct diag_l0_device **pdl0d)
{
	sim_free_ecu_responses(&sim_last_ecu_responses);
	
	if (pdl0d && *pdl0d) {
		struct diag_l0_device *dl0d = *pdl0d;
		struct diag_l0_sim_device *dev = (struct diag_l0_sim_device *)diag_l0_dl0_handle(dl0d);
	
		// If debugging, print to strerr.
		if (diag_l0_debug & DIAG_DEBUG_CLOSE)
			fprintf(stderr, FLFMT "link %p closing\n", FL, dl0d);
		
		if (dev) {
			if (dev->fp != NULL)
			fclose(dev->fp);
			free(dev);
		}
		//XXX 
		if (dl0d->fd != -1) 
			dl0d->fd = -1;
	}
	
	return 0;
}


// Simulates the bus initialization.
static int
diag_l0_sim_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in)
{
	struct diag_l0_sim_device *dev;
	uint8_t synch_patt[1];

	sim_free_ecu_responses(&sim_last_ecu_responses);

	dev = (struct diag_l0_sim_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_IOCTL)
		fprintf(stderr, FLFMT "device link %p info %p initbus type %d\n", FL, dl0d, dev, in->type);

	if (!dev)
		return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);

	switch (in->type) {
	case DIAG_L1_INITBUS_FAST:
		// Send break.
		// We simulate a break with a single "0x00" char.
		if (diag_l0_debug & DIAG_DEBUG_DATA)
			fprintf(stderr, FLFMT "Sending: BREAK!\n", FL);
		uint8_t sim_break = 0x00;
		diag_l0_sim_send(dl0d, 0, &sim_break, 1);
		break;
	case DIAG_L1_INITBUS_5BAUD:
		// Send Service Address (as if it was at 5baud).
		diag_l0_sim_send(dl0d, 0, &in->addr, 1);
		// Receive Synch Pattern (as if it was at 10.4kbaud).
		diag_l0_sim_recv(dl0d, 0 , synch_patt, 1, 0);
		break;
	default:
		return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);
		break;
	}
	
	return 0;
}


// Simulates the send of a request.
// Returns 0 on success, -1 on failure.
// Should be called with the full message to send, because
// CARSIM behaves like a smart interface (does P4).
// Gets the list of responses from the DB file for the given request.
static int
diag_l0_sim_send(struct diag_l0_device *dl0d,
		 const char *subinterface __attribute__((unused)),
		 const void *data, const size_t len)
{
	ssize_t xferd;

	if (sim_last_ecu_responses != NULL) {
		fprintf(stderr, FLFMT "AAAHHH!!! You're sending a new request before reading all previous responses!!! \n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "device link %p send %ld bytes\n", FL, dl0d, (long)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			fprintf(stderr, FLFMT "L0 sim sending: ", FL);
			diag_data_dump(stderr, data, len);
			fprintf(stderr, "\n");
		}
	}
	

	// Build the list of responses for this request.
	struct diag_l0_sim_device * dev = dl0d->dl0_handle;
	sim_find_responses(&sim_last_ecu_responses, dev->fp, data, len);

	if (diag_l0_debug & DIAG_DEBUG_DATA)
		sim_dump_ecu_responses(sim_last_ecu_responses);

	return 0;
}


// Gets present ECU response from the prepared list.
// Returns ECU response with parsed data (if applicable).
// Returns number of chars read.
static int
diag_l0_sim_recv(struct diag_l0_device *dl0d,
		 const char *subinterface __attribute__((unused)),
		 void *data, size_t len, int timeout)
{
	int xferd;
	struct diag_l0_sim_device *dev;
	struct sim_ecu_response* resp_p = NULL;

	dev = (struct diag_l0_sim_device *)diag_l0_dl0_handle(dl0d);

	if (diag_l0_debug & DIAG_DEBUG_READ)
		fprintf(stderr,
			FLFMT "link %p recv upto %ld bytes timeout %d\n",
			FL, dl0d, (long)len, timeout);
	
	// "Receive from the ECU" a response.
	resp_p = sim_last_ecu_responses;
	if (resp_p != NULL) {
		// Parse the response (replace simulated values if needed).
		sim_parse_response(resp_p);
		// Copy to client.
		xferd = MIN(resp_p->len, len);
		memcpy(data, resp_p->data, xferd);
		// Free the present response in the list (and walk to the next one).
		sim_last_ecu_responses = sim_free_ecu_response(&sim_last_ecu_responses);
	} else {
		// Nothing to receive, simulate timeout on return.
		xferd = 0;
		memset(data, 0, len);
	}

	if (diag_l0_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr, FLFMT "device link %p send %ld bytes\n", FL, dl0d, (long)len);
		if (diag_l0_debug & DIAG_DEBUG_DATA) {
			fprintf(stderr, FLFMT "L0 sim receiving: ", FL);
			diag_data_dump(stderr, data, xferd);
			fprintf(stderr, "\n");
		}
	}

	return (xferd == 0 ? DIAG_ERR_TIMEOUT : xferd);
}



// Simulates setting speed/parity etc.
// Just accepts whatever is specified.
static int
diag_l0_sim_setspeed(struct diag_l0_device *dl0d,
			 const struct diag_serial_settings *pset)
{
	struct diag_l0_sim_device *dev;
	int ret;

	dev = (struct diag_l0_sim_device *)diag_l0_dl0_handle(dl0d);

	dev->serial = *pset;

	return ret;
}


// Returns the interface's physical flags.
// The simulator doesn't need half-duplex or
// P4 timing, and implements all types of init.
// If you don't want to deal with checksums and CRCs,
// uncomment the SIM_NOL2CKSUM line in the file;
// If you don't want to deal with header bytes, uncomment
// the SIM_NOL2FRAME line in the file (required for SAEJ1850).
static int
diag_l0_sim_getflags(struct diag_l0_device *dl0d __attribute__((unused)))
{
	int ret = 0;

	ret = DIAG_L1_SLOW | 
	DIAG_L1_FAST | 
	DIAG_L1_PREFFAST | 
	DIAG_L1_DOESP4WAIT |
	DIAG_L1_HALFDUPLEX;

	if (sim_skip_crc)
		ret |= DIAG_L1_DOESL2CKSUM | DIAG_L1_STRIPSL2CKSUM;

	if (sim_skip_frame)
		ret |= 	DIAG_L1_DOESL2FRAME;

	return ret;
}


//called from outside to update local filename pointer.
extern void
diag_l0_sim_setfile(char * fname)
{
	simfile=fname;
	return;
}
	

// Declares the interface's protocol flags
// and pointers to functions.
// Like any simulator, it "implements" all protocols
// (it only depends on the content of the DB file).
const struct diag_l0 diag_l0_sim = 
{
	"Car Simulator interface", 
	"CARSIM",
	DIAG_L1_J1850_VPW | DIAG_L1_J1850_PWM | DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	diag_l0_sim_init,
	diag_l0_sim_open,
	diag_l0_sim_close,
	diag_l0_sim_initbus,
	diag_l0_sim_send,
	diag_l0_sim_recv,
	diag_l0_sim_setspeed,
	diag_l0_sim_getflags
};

#if defined(__cplusplus)
extern "C" 
{
#endif
	extern int diag_l0_sim_add(void);
#if defined(__cplusplus)
}
#endif

int
diag_l0_sim_add(void) 
{
	return diag_l1_add_l0dev(&diag_l0_sim);
}
