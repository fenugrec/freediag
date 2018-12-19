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
 * In this L0 "pseudo-driver", the serial port is not used, and in its
 * place is a simple file, such as "freediag_carsim.db". This file holds
 * one or more responses for each OBDII request. Feel free to enlarge
 * that file with valid information for your case, customise it at will
 * for your own tests. The format is pretty raw (message bytes in hexadecimal),
 * with allowance for comments (lines started with "#") and a very small and
 * rigid syntax (check the comments in the file).
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h> // str**()
#include <ctype.h>
#include <stdbool.h>
#include <math.h> // sin()

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_tty.h"
#include "diag_l0.h"
#include "diag_l1.h"
#include "diag_cfg.h"

#include "utlist.h"


/**************************************************/
// LOCAL DATATYPES AND GLOBALS:
/**************************************************/


extern const struct diag_l0 diag_l0_sim;

const char *simfile_default=DB_FILE;	//default filename


// ECU responses linked list:
struct sim_ecu_response {
	char *text; // unparsed text for the response!
	uint8_t *data; // parsed final response.
	uint8_t len; // final response length.
	struct sim_ecu_response *next;
};

/* Internal state (struct diag_l0_device->l0_int) */
struct sim_device {
	int protocol;
	FILE *fp; // DB file pointer.
	// Configuration variables.
	// These affect the kind of flags we should return.
	// This makes the simulator configurable towards using
	// or not the L2 framing and CRC/Checksums.
	// These boolean flags are to be programmed with values
	// from the DB file in use.
	bool	dataonly;	/* messages are sent/received without headers or checksums; required for J1850 */
	bool	nocksum;	/* messages are sent/received without checksums */
	bool	framed;		/* responses must be considered as complete frames; dataonly and nocksum imply this */
	bool	fullinit;	/* indicate that l0 does full init */

	int	proto_restrict;	/* (optional) only accept connections matching this proto */

	struct cfgi simfile;

	uint8_t sim_last_ecu_request[255];	// Copy of most recent request.
	struct sim_ecu_response *sim_last_ecu_responses;	// For keeping all the responses to the last request.
};



/**************************************************/
// FORWARD DECLARATIONS:
/**************************************************/


static int
sim_send(struct diag_l0_device *dl0d,
		UNUSED(const char *subinterface),
		 const void *data, size_t len);

static int
sim_recv(struct diag_l0_device *dl0d,
		UNUSED(const char *subinterface),
		 void *data, size_t len, unsigned int timeout);

static void sim_close(struct diag_l0_device *dl0d);

/**************************************************/
// LOCAL FUNCTIONS:
/**************************************************/


// Allocates one new ecu response and fills it with given text.
struct sim_ecu_response
*sim_new_ecu_response_txt(const char *text) {
	struct sim_ecu_response *resp;
	int rv;

	if ((rv = diag_calloc(&resp, 1))) {
		return diag_pfwderr(rv);
	}

	resp->data = NULL;
	resp->len = 0;
	resp->text = NULL;
	resp->next = NULL;

	if ((text != NULL) && strlen(text)) {
		if ((rv=diag_calloc(&(resp->text), strlen(text)+1))) {
			free(resp);
			return diag_pfwderr(rv);
		}

		strncpy(resp->text, text, strlen(text));	//using strlen() defeats the purpose of strncpy ...
	}

	return resp;
}

// Allocates one new ecu response and fills it with given data.
// (not used yet, here for "just in case")
struct sim_ecu_response
*sim_new_ecu_response_bin(const uint8_t *data, const uint8_t len) {
	struct sim_ecu_response *resp;
	int rv;

	rv = diag_calloc(&resp, 1);
	if (rv != 0) {
		return diag_pfwderr(rv);
	}
	resp->data = NULL;
	resp->len = 0;
	resp->text = NULL;
	resp->next = NULL;

	if ((len > 0) && (data != NULL)) {
		rv = diag_calloc(&resp->data, len);
		if (rv != 0) {
			free(resp);
			return diag_pfwderr(rv);
		}
		memcpy(resp->data, data, len);
		resp->len = len;
	}

	return resp;
}

// Frees an ecu response and returns the next one in the list.
struct sim_ecu_response
*sim_free_ecu_response(struct sim_ecu_response **resp) {
	struct sim_ecu_response* next_resp = NULL;

	if (resp && *resp) {

		//get pointer to next one.
		next_resp = (*resp)->next;

		//free this one.
		if ((*resp)->data) {
			free((*resp)->data);
		}
		if ((*resp)->text) {
			free((*resp)->text);
		}
		free(*resp);
		*resp = NULL;
	}

	//return next one.
	return next_resp;
}


// Frees all responses from the given one until the end of the list.
void sim_free_ecu_responses(struct sim_ecu_response **resp_pp) {
	struct sim_ecu_response **temp_resp_pp = resp_pp;
	uint8_t count = 0;

	if ((resp_pp == NULL) || (*resp_pp == NULL)) {
		return;
	}

	while (*temp_resp_pp != NULL) {
		*temp_resp_pp = sim_free_ecu_response(temp_resp_pp);
		count++;
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V,
		FLFMT " %d responses freed from queue.\n", FL, count);

	return;
}

// for debug purposes.
void sim_dump_ecu_responses(struct sim_ecu_response *resp_p) {
	struct sim_ecu_response *tresp;
	uint8_t count = 0;

	LL_FOREACH(resp_p, tresp) {
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_DATA, DIAG_DBGLEVEL_V,
			FLFMT "response #%d: %s", FL, count, tresp->text);
		count++;
	}

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_DATA, DIAG_DBGLEVEL_V,
		FLFMT "%d responses in queue.\n", FL, count);
	return;
}


// Builds a list of responses for a request, by finding them in the DB file.
void sim_find_responses(struct sim_ecu_response **resp_pp, FILE *fp, const uint8_t *data, const uint8_t len) {
#define TAG_REQUEST "RQ"
#define TAG_RESPONSE "RP"
#define VALUE_DONTCARE "XXXX"
#define REQBYTES	11	//number of request bytes analyzed

	uint8_t resp_count = 0;
	uint8_t new_resp_count = 0;
	uint8_t synth_req[REQBYTES];
	char line_buf[1280+1]; // 255 response bytes * 5 ("0xYY ") + tolerance for a token ("abc1 ") = 1280.
	int end_responses = 0;
	int request_found = 0;
	struct sim_ecu_response *resp_p;


	// walk to the end of the list (last valid item).
	LL_FOREACH(*resp_pp, resp_p) {
		resp_count++;
	}

	// go to the beginning of the DB file.
	rewind(fp);

	// search for the given request.
	while (!request_found && !end_responses) {
		// get a line from DB file.
		if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
			// EOF reached.
			break;
		}
		// ignore all lines except requests.
		if (strncmp(line_buf, TAG_REQUEST, strlen(TAG_REQUEST)) != 0) {
			continue;
		}
		// synthesize up to 11 byte values from DB request line.
		unsigned int i, num;
		char *p, *q;
		p = line_buf + 3;
		for (i=0; i < REQBYTES; i++) {
			while (isspace(*p)) {
				p++;
			}
			if (*p == '\0') {
				break;
			}
			if (strncmp(p, VALUE_DONTCARE, strlen(VALUE_DONTCARE)) == 0) {
				if (i < len) {
					synth_req[i] = data[i];
				}
				p += strlen(VALUE_DONTCARE);
			} else {
				synth_req[i] = (uint8_t)strtoul(p, &q, 16);
				if (p == q) {
					break;
				}
				p = q;
				if (!isspace(*p)) {
					break;
				}
			}
		}
		num = i;
		// compare given request with synthesized DB file request.
		if ((len >= num) && (memcmp(data, synth_req, MIN(len, num)) == 0)) {
			// got a match, now cycle the following lines for responses.
			request_found = 1;
			while (!end_responses) {
				// get a line from file.
				if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
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
					}
					continue;
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

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "%d responses queued for receive, %d new.\n",
		FL, resp_count + new_resp_count, new_resp_count);
	return;
}


// Returns a value between 0x00 and 0xFF calculated as the trigonometric
// sine of the current system time (with a period of one second).
uint8_t sine1(UNUSED(uint8_t *data), UNUSED(uint8_t pos)) {
	unsigned long now=diag_os_getms();
	//sin() returns a float between -1.0 and 1.0
	return (uint8_t) (0x7F * sin(now * 6.283185 / 1000));
}

// Returns a value between 0x00 and 0xFF directly proportional
// to the value of the current system time (with a period of one second).
uint8_t sawtooth1(UNUSED(uint8_t *data), UNUSED(uint8_t pos)) {
	unsigned long now=diag_os_getms();
	return (uint8_t) (0xFF * (now % 1000));
}

// Returns a value copied from the specified position in the request.
uint8_t requestbyten(UNUSED(uint8_t *data), char *s, uint8_t req[]) {
	int index;
	bool increment = 0;
	bool bogus = 0;
	uint8_t value;
	char *p;

	index = (uint8_t)strtoul(s, &p, 10);
	if (*s == '\0') {
		bogus = 1;
	} else if (p[0]=='+' && p[1]=='\0') {
		increment = 1;
	} else if (*p != '\0') {
		bogus = 1;
	}

	index--;
	if (index < 0 || index > 254) {
		bogus = 1;
	}

	if (bogus) {
		fprintf(stderr, FLFMT "Invalid req* token in response\n", FL);
		return 0;
	}

	value = req[index];
	if (increment) {
		value++;
	}
	return value;
}

// Parses a response's text to data.
// Replaces special tokens with function results. This mangles resp_p->text, which shouldn't be a problem
void sim_parse_response(struct sim_ecu_response *resp_p, uint8_t req[]) {
#define TOKEN_SINE1	 "sin1"
#define TOKEN_SAWTOOTH1 "swt1"
#define TOKEN_ISO9141CS "cks1"
#define TOKEN_REQUESTBYTE "req"
#define SRESP_SIZE 255

	uint8_t synth_resp[SRESP_SIZE];	// 255 response bytes.
	char *cur_tok = NULL;		//current token
	char *rptr = resp_p->text;	//working copy of the ptr. We will mangle resp_p->text
	int ret;
	int pos = 0;

	// extract byte values from response line, splitting tokens at whitespace / EOL.
	while ((cur_tok = strtok(rptr, " \t\r\n")) != NULL) {
		if (pos == 0xff) {
			fprintf(stderr, "Malformed db file, > 255 bytes on one line !");
			break;
		}
		// try replacing a token with a calculated value.
		if (strcmp(cur_tok, TOKEN_SINE1) == 0) {
			synth_resp[pos] = sine1(synth_resp, pos);
		} else if (strcmp(cur_tok, TOKEN_SAWTOOTH1) == 0) {
			synth_resp[pos] = sawtooth1(synth_resp, pos);
		} else if (strcmp(cur_tok, TOKEN_ISO9141CS) == 0) {
			synth_resp[pos] = diag_cks1(synth_resp, pos);
		} else if (strncmp(cur_tok, TOKEN_REQUESTBYTE,
				   strlen(TOKEN_REQUESTBYTE)) == 0) {
			synth_resp[pos] = requestbyten(synth_resp, cur_tok + strlen(TOKEN_REQUESTBYTE), req);
		} else {
			// failed. try scanning element as an Hex byte.
			unsigned int tempbyte;
			ret = sscanf(cur_tok, "%X", &tempbyte);	//can't scan direct to uint8 !
			if (ret != 1) {
				fprintf(stderr, FLFMT "Error parsing line: %s at position %d.\n", FL, resp_p->text, pos*5);
				break;
			}
			synth_resp[pos] = (uint8_t) tempbyte;
		}
		pos++;
		rptr = NULL;	//strtok: continue parsing
	}

	// copy to user.
	if (diag_calloc(&(resp_p->data),pos)) {
		fprintf(stderr, FLFMT "Error parsing response\n", FL);
		return;
	}
	memcpy(resp_p->data, synth_resp, pos);
	resp_p->len = pos;
}

// Reads the configuration options from the file.
// Stores them in globals.
void sim_read_cfg(struct sim_device *dev) {
	FILE *fp = dev->fp;
	char *p; // temp string pointer.
	char line_buf[21]; // 20 chars generally enough for a config token.

#define TAG_CFG "CFG"
#define CFG_DATAONLY "DATAONLY"
#define CFG_NOL2CKSUM "NOL2CKSUM"
#define CFG_FRAMED "FRAMED"
#define CFG_FULLINIT "FULLINIT"
#define CFG_P9141	"P_9141"
#define CFG_P14230	"P_14230"
#define CFG_P1850P	"P_J1850P"
#define CFG_P1850V	"P_J1850V"
#define CFG_PCAN	"P_CAN"
#define CFG_PRAW	"P_RAW"

	dev->dataonly = 0;
	dev->nocksum = 0;
	dev->fullinit = 0;
	dev->proto_restrict = 0;

	// search for all config lines.
	while (1) {
		// get a line from DB file.
		if (fgets(line_buf, 20, fp) == NULL) {
			// EOF reached.
			break;
		}
		// ignore all lines except configs.
		if (strncmp(line_buf, TAG_CFG, strlen(TAG_CFG)) != 0) {
			continue;
		}
		// get the config values.
		p = line_buf + strlen(TAG_CFG) + 1;

		if (strncmp(p, CFG_DATAONLY, strlen(CFG_DATAONLY)) == 0) {
			dev->dataonly = 1;
			continue;
		}
		if (strncmp(p, CFG_NOL2CKSUM, strlen(CFG_NOL2CKSUM)) == 0) {
			dev->nocksum = 1;
			continue;
		}
		if (strncmp(p, CFG_FRAMED, strlen(CFG_FRAMED)) == 0) {
			dev->framed = 1;
		} else if (strncmp(p, CFG_FULLINIT, strlen(CFG_FULLINIT)) == 0) {
			dev->fullinit = 1;
		} else if (strncmp(p, CFG_P9141, strlen(CFG_P9141)) == 0) {
			dev->proto_restrict=DIAG_L1_ISO9141;
			continue;
		} else if (strncmp(p, CFG_P14230, strlen(CFG_P14230)) == 0) {
			dev->proto_restrict=DIAG_L1_ISO14230;
			continue;
		} else if (strncmp(p, CFG_P1850P, strlen(CFG_P1850P)) == 0) {
			dev->proto_restrict=DIAG_L1_J1850_PWM;
			continue;
		} else if (strncmp(p, CFG_P1850V, strlen(CFG_P1850V)) == 0) {
			dev->proto_restrict=DIAG_L1_J1850_VPW;
			continue;
		} else if (strncmp(p, CFG_PCAN, strlen(CFG_PCAN)) == 0) {
			dev->proto_restrict=DIAG_L1_CAN;
			continue;
		} else if (strncmp(p, CFG_PRAW, strlen(CFG_PRAW)) == 0) {
			dev->proto_restrict=DIAG_L1_RAW;
			continue;
		}
	}
}

/**************************************************/
// INTERFACE FUNCTIONS:
/**************************************************/


// Initializes the simulator.
static int
sim_init(void) {
	return 0;
}

/* fill & init new dl0d */
int
sim_new(struct diag_l0_device *dl0d) {
	int rv;
	struct sim_device *dev;

	// Create sim_device:
	if ((rv = diag_calloc(&dev, 1))) {
		return diag_ifwderr(rv);
	}

	dl0d->l0_int = dev;

	//init configurable params:
	if (diag_cfgn_str(&dev->simfile, simfile_default,
						"Simulation file to use as data input", "simfile")) {
		free(dev);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}
	dev->simfile.next = NULL;	//mark as first/only/last item in the list
	return 0;
}

/* Clear + free the contents of dl0d; assumes L0 was closed first */
static void
sim_del(struct diag_l0_device *dl0d) {
	struct sim_device *dev;

	assert(dl0d !=NULL);

	dev = (struct sim_device *)dl0d->l0_int;

	if (!dev) {
		return;
	}

	diag_cfg_clear(&dev->simfile);
	free(dev);

	return;
}

// Opens the simulator DB file
int
sim_open(struct diag_l0_device *dl0d, int iProtocol) {
	struct sim_device *dev;
	const char *simfile;

	assert(dl0d != NULL);

	dev = (struct sim_device *) dl0d->l0_int;
	simfile = dev->simfile.val.str;

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_OPEN, DIAG_DBGLEVEL_V,
		FLFMT "open simfile %s proto=%d\n", FL, simfile, iProtocol);

	dev->protocol = iProtocol;
	dev->sim_last_ecu_responses = NULL;

	// Open the DB file:
	if ((dev->fp = fopen(simfile, "r")) == NULL) {
		fprintf(stderr, FLFMT "Unable to open file \"%s\": ", FL, simfile);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	rewind(dev->fp);

	// Read the configuration flags from the db file:
	sim_read_cfg(dev);

	/* if a specific proto was set, refuse a mismatched connection */
	if (dev->proto_restrict) {
		if (dev->proto_restrict != iProtocol) {
			sim_close(dl0d);
			return diag_iseterr(DIAG_ERR_PROTO_NOTSUPP);
		}
	}

	dl0d->opened = 1;
	return 0;
}


// Closes the simulator DB file; cleanup after _sim_open()
static void
sim_close(struct diag_l0_device *dl0d) {
	assert(dl0d != NULL);
	//if (!dl0d) return;

	struct sim_device *dev = (struct sim_device *)dl0d->l0_int;
	assert(dev != NULL);

	// If debugging, print to stderr.
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_CLOSE, DIAG_DBGLEVEL_V,
		FLFMT "dl0d=%p closing simfile\n", FL, (void *)dl0d);

	sim_free_ecu_responses(&dev->sim_last_ecu_responses);


	if (dev->fp != NULL) {
		fclose(dev->fp);
		dev->fp = NULL;
	}

	dl0d->opened = 0;
	return;
}


// Simulates the bus initialization.
static int
sim_initbus(struct diag_l0_device *dl0d, struct diag_l1_initbus_args *in) {
	struct sim_device *dev;
	uint8_t synch_patt[1];
	const uint8_t sim_break = 0x00;

	dev = (struct sim_device *)dl0d->l0_int;

	sim_free_ecu_responses(&dev->sim_last_ecu_responses);

	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_IOCTL, DIAG_DBGLEVEL_V,
		FLFMT "device link %p info %p initbus type %d\n",
		FL, (void *)dl0d, (void *)dev, in->type);

	if (!dev) {
		return diag_iseterr(DIAG_ERR_INIT_NOTSUPP);
	}

	if (dev->fullinit) {
		return 0;
	}

	switch (in->type) {
	case DIAG_L1_INITBUS_FAST:
		// Send break.
		// We simulate a break with a single "0x00" char.
		DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_INIT, DIAG_DBGLEVEL_V,
			FLFMT "Sending: BREAK!\n", FL);
		sim_send(dl0d, 0, &sim_break, 1);
		break;
	case DIAG_L1_INITBUS_5BAUD:
		// Send Service Address (as if it was at 5baud).
		sim_send(dl0d, 0, &in->addr, 1);
		// Receive Synch Pattern (as if it was at 10.4kbaud).
		sim_recv(dl0d, 0 , synch_patt, 1, 0);
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
sim_send(struct diag_l0_device *dl0d,
		UNUSED(const char *subinterface),
		 const void *data, const size_t len) {
	struct sim_device *dev = dl0d->l0_int;

	if (len <= 0) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}

	if (len > 255) {
		fprintf(stderr, FLFMT "Error : calling sim_send with len >255 bytes! (%u)\n", FL, (unsigned int) len);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (dev->sim_last_ecu_responses != NULL) {
		fprintf(stderr, FLFMT "AAAHHH!!! You're sending a new request before reading all previous responses!!! \n", FL);
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_WRITE, DIAG_DBGLEVEL_V, data, len,
		FLFMT "dl0d=%p sending %u bytes; ", FL, (void *)dl0d, (unsigned int)len);

	// Store a copy of this request for use by req* function tokens.
	memcpy(dev->sim_last_ecu_request, data, len);

	// Build the list of responses for this request.
	sim_find_responses(&dev->sim_last_ecu_responses, dev->fp, data, (uint8_t) len);

	sim_dump_ecu_responses(dev->sim_last_ecu_responses);

	return 0;
}


// Gets present ECU response from the prepared list.
// Returns ECU response with parsed data (if applicable).
// Returns number of bytes read.
static int
sim_recv(struct diag_l0_device *dl0d,
		UNUSED(const char *subinterface),
		void *data, size_t len, unsigned int timeout) {
	size_t xferd;
	struct sim_ecu_response *resp_p = NULL;
	struct sim_device *dev = dl0d->l0_int;

	if (!len) {
		return diag_iseterr(DIAG_ERR_BADLEN);
	}
	DIAG_DBGM(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V,
		FLFMT "link %p recv upto %ld bytes timeout %u\n",
		FL, (void *)dl0d, (long)len, timeout);

	// "Receive from the ECU" a response.
	resp_p = dev->sim_last_ecu_responses;
	if (resp_p != NULL) {
		// Parse the response (replace simulated values if needed).
		sim_parse_response(resp_p, dev->sim_last_ecu_request);
		// Copy to client.
		xferd = MIN(resp_p->len, len);
		memcpy(data, resp_p->data, xferd);
		// Free the present response in the list (and walk to the next one).
		dev->sim_last_ecu_responses = sim_free_ecu_response(&dev->sim_last_ecu_responses);
	} else {
		// Nothing to receive, simulate timeout on return.
		xferd = 0;
		memset(data, 0, len);
	}

	DIAG_DBGMDATA(diag_l0_debug, DIAG_DEBUG_READ, DIAG_DBGLEVEL_V, data, xferd,
		FLFMT "dl0d=%p recv %d byte; ", FL, (void *)dl0d, (int) len);

	return (xferd == 0 ? DIAG_ERR_TIMEOUT : (int) xferd);
}



// Returns the interface's physical flags.
// The simulator doesn't need half-duplex or
// P4 timing, and implements all types of init.
static uint32_t
sim_getflags(struct diag_l0_device *dl0d) {
	struct sim_device *dev = dl0d->l0_int;
	int ret;

	ret = DIAG_L1_SLOW |
		DIAG_L1_FAST |
		DIAG_L1_PREFFAST |
		DIAG_L1_DOESP4WAIT |
		DIAG_L1_AUTOSPEED |
		DIAG_L1_NOTTY;

	/* both "no checksum" and "dataonly" modes take care of checksums, and imply framing. */
	if (dev->nocksum || dev->dataonly) {
		ret |= DIAG_L1_DOESL2CKSUM | DIAG_L1_STRIPSL2CKSUM |
		       DIAG_L1_DOESL2FRAME;
	}

	if (dev->dataonly) {
		ret |= DIAG_L1_NOHDRS | DIAG_L1_DATAONLY | DIAG_L1_DOESL2FRAME;
	}

	if (dev->framed) {
		ret |= DIAG_L1_DOESL2FRAME;
	}

	if (dev->fullinit) {
		ret |= DIAG_L1_DOESFULLINIT;
	}

	return ret;
}


static struct cfgi
*sim_getcfg(struct diag_l0_device *dl0d) {
	struct sim_device *dev;
	if (dl0d == NULL) {
		return diag_pseterr(DIAG_ERR_BADCFG);
	}

	dev = (struct sim_device *)dl0d->l0_int;
	return &dev->simfile;
}


static int sim_ioctl(struct diag_l0_device *dl0d, unsigned cmd, void *data) {
	int rv = 0;

	switch (cmd) {
	case DIAG_IOCTL_INITBUS:
		rv = sim_initbus(dl0d, (struct diag_l1_initbus_args *)data);
		break;
	default:
		rv = DIAG_ERR_IOCTL_NOTSUPP;
		break;
	}

	return rv;
}


// Declares the interface's protocol flags
// and pointers to functions.
// Like any simulator, it "implements" all protocols
// (it only depends on the content of the DB file).
const struct diag_l0 diag_l0_sim = {
	"Car Simulator interface",
	"CARSIM",
	DIAG_L1_J1850_VPW | DIAG_L1_J1850_PWM | DIAG_L1_ISO9141 | DIAG_L1_ISO14230 | DIAG_L1_RAW,
	sim_init,
	sim_new,
	sim_getcfg,
	sim_del,
	sim_open,
	sim_close,
	sim_getflags,
	sim_recv,
	sim_send,
	sim_ioctl
};
