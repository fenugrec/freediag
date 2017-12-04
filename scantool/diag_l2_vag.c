/*
 * freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * Copyright (C) 2015-2016 Tomasz Kaźmierczak <tomek-k@users.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *************************************************************************
 *
 * Diag
 *
 * L2 driver for Volkswagen Aktiengesellschaft (VAG) KW1281 protocol
 * (Keyword 0x01 0x8a)
 *
 * This implementation is written according to the SAE J2818 specification,
 * but with one exception - the response to ECU's No Acknowledge Retry message
 * uses an incremented sequence number (SAE J2818 says that it shouldn't be
 * incremented); similarly after sending No Acknowledge Retry message,
 * the repeated ECU message is expected to have the sequence number incremented
 * (SAE J2818 says that also the ECU should repeat the message using the
 * previous sequence number). This is because the code has been tested only
 * with a European version of a VAG ECU (non-US VAG ECUs do not follow the SAE
 * specification strictly) - once it is confirmed that US VAG ECUs do follow
 * the SAE specification with regards to the No Acknowledge Retry behaviour,
 * the spec-obeying behaviour can be added as an option (or as a default, with
 * an option to use the non-US behaviour).
 *
 * The default baud rate (used when none is set by the user) is 10400, so here
 * the code follows the SAE J2818 specification, which is mandatory on the US
 * market, but again - it has not been tested on a US VAG ECU.
 * European VAG ECUs use 9600 baud rate, so this value should be used if
 * the default doesn't work (the default most probably won't work outside US).
 *
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "diag_err.h"
#include "diag_os.h"
#include "diag_l1.h"
#include "diag_l2.h"

#include "diag_l2_vag.h"

/*
 * ISO vag specific data
 */
struct diag_l2_vag {
	uint8_t seq_nr; //Sequence number
	uint8_t master; //Master flag, 1 = us, 0 = ECU
	uint8_t first_telegram_started;

	struct diag_msg *ecu_id_telegram; //a pointer to store the ECU ID telegram received during initiation

	uint8_t rxbuf[MAXRBUF]; //Receive buffer, for building message in
	int rxoffset;           //Offset to write into buffer

	unsigned long long msg_finish_time; //a point in time when the last message finished arriving/departing
};

/*
 * Useful internal routines
 */

/*
 *
 * Receives a single Block from the ECU
 *
 * Returns 0 on success, errorcode<0 on errors
 */
static struct diag_msg *
diag_l2_vag_block_recv(struct diag_l2_conn *d_l2_conn, int *errval, int msg_timeout) {
	int rv;
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	//prepare a No Acknowledge message - we may need one
	uint8_t noack_data[1];
	struct diag_msg noack;
	memset(&noack, 0, sizeof(noack));
	noack.type = KWP1281_SID_NO_ACK;
	noack.data = noack_data;
	noack.len = 1;

	//clear the offset
	dp->rxoffset = 0;

	//Set the timeout for the first byte of the awaited message
	int timeout = msg_timeout;

	while (1) {
		if (d_l2_conn->diag_link->l1flags & DIAG_L1_DOESL2FRAME) {
			//for framed L0, must read the whole frame at once
			rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, dp->rxbuf, MAXRBUF, timeout);
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr, FLFMT "after recv, rv=%d\n", FL,
					rv);
			}
			if (rv < 0) {
				*errval = rv;
				return diag_pseterr(rv);
			}
			dp->msg_finish_time = diag_os_gethrt();
			//currently the only framed L0 for KW1281 is carsim, so don't bother validating sequence number
			break;
		}

		//one byte at a time is sent by the ECU
		uint8_t byte;
		rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, &byte, 1, timeout);
		unsigned long long byte_recv_time = diag_os_gethrt();
		//now set the timeout value for all the remaining awaited bytes
		timeout = KWP1281_T_R8;

		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr, FLFMT "after recv, rv=%d rxoffset=%d\n",
				FL, rv, dp->rxoffset);
		}

		if (rv < 0) {
			if (rv == DIAG_ERR_TIMEOUT) {
				//a very special case of timeout is when waiting for the first telegram from the ECU;
				//if it occurs, then it means that the ECU either received wrong KB2 complement
				//(which is more probable) or didn't receive the KB2 at all (less probable);
				//since we cannot OR the error code, let's return the more probable one
				if (dp->first_telegram_started == 0) {
					*errval = DIAG_ERR_BADRATE; //wrong KB2 value indicates baud rate problems
					return diag_pseterr(*errval);
				}
				//the timeout may occur if the transmitter didn't receive our complement byte
				//or if that byte was incorrect - in such cases it will retry sending the whole
				//message again, but only after 2*T_R8 time units since it sent the previous message byte;
				//we (the receiver) have been waiting already for T_R8 time unit since sending our
				//complement byte, so the transmitter should re-start the message _within_
				//_our_ another T_R8 time unit (which started later than the receiver's secont T_R8
				//time unit);
				rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, &byte, 1, KWP1281_T_R8);
				if (rv < 0) {
					//if we timed out again, then this means that the communication line
					//has been broken or closed; but with one exception - if we were waiting for
					//the last byte of the message (the ETX byte), then we should assume that the ETX
					//byte might have been sent by the transmitter, but got lost on its way,
					//and we should try to go on as if we have received it
					if (dp->rxoffset < dp->rxbuf[0] || rv != DIAG_ERR_TIMEOUT) {
						*errval = rv;
						return diag_pseterr(rv);
					}
				} else {
					//the first byte of the re-started message has arrived, so just reset the rxoffset
					//and go on with the regular code path
					dp->rxoffset = 0;
				}
			} else {
				*errval = rv;
				return diag_pseterr(rv);
			}
		}
		//now we can set the flag indicating that the initialization
		//has been fully successful
		if (dp->first_telegram_started == 0) {
			dp->first_telegram_started = 1;
		}

		//store the byte
		dp->rxbuf[dp->rxoffset] = byte;
		dp->rxoffset++;

		//is this the last byte?
		if (dp->rxoffset-1 == dp->rxbuf[0]) {
			dp->msg_finish_time = diag_os_gethrt();
			//check whether the last byte is correct
			if (byte != KWP1281_END_BYTE ||
			   //check also whether the sequence number present in the message is correct
			   //(should be odd and be greater than our sequence number by 1)
			   ((dp->rxbuf[1] % 2) == 0 || dp->rxbuf[1] != dp->seq_nr+1)) {
				//arbitrarily set our sequence number -  we could get here because of an incorrect
				//sequence number sent by the ECU
				dp->seq_nr += 2;
				//send a NoAck Retry message by using the sequence number from the ECU's message
				//(we can be sure that the buffer contains the number that ECU sent us, because when we received it,
				//we responded with a complement and ECU didn't complain)
				noack_data[0] = dp->rxbuf[1];
				//we must flag ourselves as master before calling the send function
				//(and the send function will re-set the flag to slave)
				dp->master = 1;
				rv = diag_l2_send(d_l2_conn, &noack);
				if (rv < 0) {
					*errval = rv;
					return diag_pseterr(rv);
				}
				//set the old sequence number again
				//NOTE: SAE J2818 says that "The Message number is NOT incremented by the transmitter for a repeated block."
				//      so we should expect that the repeated message will have the same sequence number and thus we should
				//      decrease our own sequence number to the previous value also. However, when testing on an ECU installed
				//      in a European VW, this is not the case - the ECU repeated the message with an increased sequence number.
				//      Can't test this with a VW from USA, so commenting-out for now. If US VWs indeed follow the specification
				//      strictly, then a possible solution would be to set/unset this behaviour based on a flag
				//      (eg. L2_vag specific option for enabling/disabling strict SAE J2818).
				//dp->seq_nr -= 2;
				//prepare for receiving the whole message again
				dp->rxoffset = 0;
				//time elapsed since receiving last message
				unsigned long long elapsed_time = diag_os_hrtus(diag_os_gethrt() - dp->msg_finish_time)/1000;
				//the timeout values are between messages, so decrease the T_RB_MAX timeout
				//by the time that has elapsed since sending the no-ack message to the ECU
				timeout = elapsed_time < KWP1281_T_RB_MAX ? KWP1281_T_RB_MAX-elapsed_time : 0;
				continue;
			}
			break;
		}

		//calculate the complement byte
		byte = ~byte;
		//how much time elapsed since receiving another byte?
		unsigned long long elapsed_time = diag_os_hrtus(diag_os_gethrt() - byte_recv_time)/1000;
		//give ECU some time before sending the complement byte
		diag_os_millisleep(elapsed_time < KWP1281_T_R6_MIN ? KWP1281_T_R6_MIN-elapsed_time : 0);
		rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, 0, &byte, 1, 0);

		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr, FLFMT "after send, rv=%d\n", FL, rv);
		}

		if (rv < 0) {
			*errval = rv;
			return diag_pseterr(rv);
		}
	}

	//now we are the master (!!)
	dp->master = 1;
	//update our sequence number
	//(the sequence number from the ECU's message has been validated already)
	dp->seq_nr = dp->rxbuf[1]+1;

	//length of the data inside the block;
	//the length byte (the first one) doesn't count itself,
	//so subtract only three bytes: block counter, command title, the end byte
	uint8_t data_length = dp->rxbuf[0]-3;

	//alloc new message
	struct diag_msg *tmsg = diag_allocmsg(data_length);
	if (tmsg == NULL) {
		*errval = DIAG_ERR_NOMEM;
		return diag_pseterr(*errval);
	}

	//copy the message data, if exists
	if (data_length > 0) {
		memcpy(tmsg->data, &dp->rxbuf[3], (size_t)data_length);
	}

	//set the message info
	tmsg->rxtime = diag_os_getms();
	tmsg->type = dp->rxbuf[2];
	tmsg->dest = tmsg->src = 0; //these are not used by the protocol (no such info in message blocks)
	tmsg->fmt |= DIAG_FMT_CKSUMMED; //no real checksum, but the inverted bytes thing assures data integrity
	*errval = 0;
	return tmsg;
}

/*
 * Internal function for receiving a full telegram from ECU.
 *
 * Will store the received telegram in the d_l2_conn->diag_msg.
 */
int
diag_l2_vag_int_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout) {
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	struct diag_msg ack;
	unsigned long long elapsed_time, msg_timeout;
	int rv, na_retry_cnt = 0;

	//Clear out last received message if not done already
	if (d_l2_conn->diag_msg) {
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	//ECU can send a telegram consisting of multiple messages, but it will expect us
	//to send an ACK message after every part (message) of the telegram, so prepare one
	memset(&ack, 0, sizeof(ack));
	ack.type = KWP1281_SID_ACK;

	//how much time has elapsed since sending our message to the ECU
	elapsed_time = diag_os_hrtus(diag_os_gethrt() - dp->msg_finish_time)/1000;
	//timeout while waiting for a message
	msg_timeout = elapsed_time < timeout ? timeout-elapsed_time : 0;

	while (1) {
		//receive another message
		struct diag_msg *tmsg = diag_l2_vag_block_recv(d_l2_conn, &rv, msg_timeout);
		if (rv < 0) {
			if (d_l2_conn->diag_msg) {
				diag_freemsg(d_l2_conn->diag_msg);
				d_l2_conn->diag_msg = NULL;
			}
			return diag_iseterr(rv);
		}

		//if this is the first messag sent by the ECU in the current telegram and this is either
		//ACK or NO_ACK, then pass it to the caller - we cannot do anything about NO_ACK if it was
		//caused by something we didn't send; and if ECU responded only with ACK, then the caller should
		//be informed that there is no other response from the ECU (for instance, the keep-alive routine
		//will be interested in an ACK reply)
		if (d_l2_conn->diag_msg == NULL && (tmsg->type == KWP1281_SID_ACK || tmsg->type == KWP1281_SID_NO_ACK)) {
			diag_l2_addmsg(d_l2_conn, tmsg);
			if ((diag_l2_debug & DIAG_DEBUG_DATA) && (diag_l2_debug & DIAG_DEBUG_PROTO)) {
				fprintf(stderr, FLFMT "Copying %u bytes to data: ", FL, tmsg->len);
				diag_data_dump(stderr, tmsg->data, tmsg->len);
				fprintf(stderr, "\n");
			}
			break;
		}

		//if the ECU has responded with an ACK message to our ACK message, then it means that
		//the telegram has finished with the previous received message
		if (tmsg->type == KWP1281_SID_ACK) {
			//we don't need the just-received ACK message
			diag_freemsg(tmsg);
			break;
		}

		//if this is a NO_ACK message (sent by the ECU as a response to our ACK),
		//then we will re-try with that ACK
		if (tmsg->type == KWP1281_SID_NO_ACK) {
			//check if it is NO_ACK Retry
			if (tmsg->data[0] == dp->seq_nr-2) {
				if (diag_l2_debug & DIAG_DEBUG_PROTO) {
					fprintf(stderr,
						FLFMT
						"Received No Acknowledge - "
						"Retry message\n",
						FL);
				}
				//accept at most KWP1281_NA_RETRIES of No Ack Retry messages in a row
				if (++na_retry_cnt == KWP1281_NA_RETRIES) {
					if (diag_l2_debug & DIAG_DEBUG_PROTO) {
						fprintf(stderr,
							FLFMT
							"\tbut too many Retry "
							"messages in a row "
							"already - aborting\n",
							FL);
					}
					diag_freemsg(d_l2_conn->diag_msg);
					d_l2_conn->diag_msg = NULL;
					return diag_iseterr(DIAG_ERR_ECUSAIDNO);
				}
				if (diag_l2_debug & DIAG_DEBUG_PROTO) {
					fprintf(stderr,
						FLFMT "\tso will retry\n", FL);
				}
				//re-send with the previous sequence number
				//NOTE: SAE J2818 says that "The Message number is NOT incremented by the transmitter for a repeated block."
				//      so we should send the repeated message with the same sequence number as the original message.
				//      However, when testing this on an ECU installed in a European VW the ECU didn't accept such a message
				//      (responded with another NO_ACK Retry) and a repeated message with an incremented sequence number
				//      was accepted.
				//      Can't test this with a VW from USA, so commenting-out for now. If US VWs indeed follow the specification
				//      strictly, then a possible solution would be to set/unset this behaviour based on a flag
				//      (eg. L2_vag specific option for enabling/disabling strict SAE J2818).
				//dp->seq_nr -= 2;
			}
		} else {
			//add the new block to the telegram
			diag_l2_addmsg(d_l2_conn, tmsg);
			if (d_l2_conn->diag_msg == tmsg) {
				if ((diag_l2_debug & DIAG_DEBUG_DATA) && (diag_l2_debug & DIAG_DEBUG_PROTO)) {
					fprintf(stderr, FLFMT "Copying %u bytes to data: ", FL, tmsg->len);
					diag_data_dump(stderr, tmsg->data, tmsg->len);
					fprintf(stderr, "\n");
				}
			}
			//reset the counter of No Ack Retry messages received in a row
			na_retry_cnt = 0;
		}

		//now tell the ECU that we are waiting for morr!
		rv = diag_l2_send(d_l2_conn, &ack);
		if (rv < 0) {
			//clean up and set the error code
			diag_freemsg(d_l2_conn->diag_msg);
			d_l2_conn->diag_msg = NULL;
			return diag_iseterr(rv);
		}

		//re-calculate the message timeout
		elapsed_time = diag_os_hrtus(diag_os_gethrt() - dp->msg_finish_time)/1000;
		msg_timeout = elapsed_time < KWP1281_T_RB_MAX ? KWP1281_T_RB_MAX-elapsed_time : 0;
	}

	return 0;
}

/* External interface */

/*
 * The complex initialisation routine for ISOvag, which supports
 * 2 types of initialisation (5-BAUD, FAST) and functional
 * and physical addressing. The ISOvag spec describes CARB initialisation
 * which is done in the ISO9141 code
 */

static int
dl2p_vag_startcomms(struct diag_l2_conn *d_l2_conn, UNUSED(flag_type flags),
                             unsigned int bitrate, target_type target, UNUSED(source_type source)) {
	struct diag_serial_settings set;
	struct diag_l2_vag *dp;
	int rv;
	uint8_t cbuf[MAXRBUF];

	struct diag_l1_initbus_args in;

	rv = diag_calloc(&dp, 1);
	if (rv != 0) {
		return diag_iseterr(rv);
	}

	d_l2_conn->diag_l2_proto_data = (void *)dp;
	//set several initial values needed by checks performed in the send/receive code
	dp->seq_nr = 0;
	dp->master = 0;
	dp->ecu_id_telegram = NULL;
	dp->first_telegram_started = 0;

	if (bitrate == 0) {
		bitrate = 10400; // default as per SAE J2818
	}
	d_l2_conn->diag_l2_speed = bitrate;

	set.speed = bitrate;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_1;
	set.parflag = diag_par_n;

	//Set the speed as shown
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_SETSPEED, &set);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	//Flush unread input, then wait for idle bus.
	(void)diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_IFLUSH, NULL);
	diag_os_millisleep(KWP1281_T_R0);

	//Now do 5 baud init of supplied address
	in.type = DIAG_L1_INITBUS_5BAUD;
	in.addr = target;
	//NOTE: there is no way to pass the timeout value into the init function - KWP1281_T_R1_MAX
	rv = diag_l2_ioctl(d_l2_conn, DIAG_IOCTL_INITBUS, &in);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	//Mode bytes are in 7-Odd-1, read as 8N1 and ignore parity
	rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, cbuf, 1, KWP1281_T_R2_MAX);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}
	rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, &cbuf[1], 1, KWP1281_T_R3_MAX);
	if (rv < 0) {
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}

	if (diag_l2_debug & DIAG_DEBUG_PROTO) {
		fprintf(stderr,
			FLFMT
			"Received KeyWord bytes: KB1: 0x%.2X\tKB2: 0x%.2X\n",
			FL, cbuf[0], cbuf[1]);
	}

	//Note down the bytes
	d_l2_conn->diag_l2_kb1 = cbuf[0];
	d_l2_conn->diag_l2_kb2 = cbuf[1];

	//transmit the inverted KB2 so that the ECU knows we have received it
	//and can validate if we got it wrong (and can act accordingly, which
	//essentially means it will timeout us if anything went wrong)
	if ((d_l2_conn->diag_link->l1flags & DIAG_L1_DOESSLOWINIT) == 0) {
		//can transmit the invrted KB2 only after R4_MIN,
		//so that the ECU has time to switch to receive mode
		diag_os_millisleep(KWP1281_T_R4_MIN);
		//Now transmit KB2 inverted
		cbuf[0] = ~ d_l2_conn->diag_l2_kb2;
		rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, 0, cbuf, 1, d_l2_conn->diag_l2_p4min);
		if (rv < 0) {
			free(dp);
			d_l2_conn->diag_l2_proto_data=NULL;
			return diag_iseterr(rv);
		}
	}
	//update the message finish time so that when waiting for the first ECU message
	//a correct timeout can be calculated
	dp->msg_finish_time = diag_os_gethrt();

	//the first ECU telegram should now arrive
	rv = diag_l2_vag_int_recv(d_l2_conn, KWP1281_T_R5_MAX);
	if (rv < 0) {
		//if the error was a timeout while waiting for the very first byte of the telegram,
		//then the error will be set to DIAG_ERR_BADRATE, which means that the ECU informs
		//us that we have probably set incorrect baudrate (it must have received incorrect
		//KB2 complement, and it can happen if we are using incorrect baudrate);
		//ECU will re-try sending the synchronization byte in a moment, but since we are
		//using a user-provided baudrate (instead of decoding it from the sync byte), then
		//we cannot do anything about it here; so just report the error and leave
		free(dp);
		d_l2_conn->diag_l2_proto_data=NULL;
		return diag_iseterr(rv);
	}
	//the first telegram is now stored in d_l2_conn->diag_msg - copy its address
	//to the dp->ecu_id_telegram pointer
	dp->ecu_id_telegram = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;

	//message interval for use by external timeout handler for sending keep-alive messages
	d_l2_conn->tinterval = KWP1281_T_RB/2;
	return 0;
}

//free what _startcomms alloc'ed
static int dl2p_vag_stopcomms(struct diag_l2_conn *d_l2_conn) {
	//according to SAE J2818 if we want to finish the session
	//we should just stop sending anything and let the ECU timeout;
	//but of course l3 can implement the endcomms SID
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	if (dp != NULL) {
		if (dp->ecu_id_telegram != NULL) {
			diag_freemsg(dp->ecu_id_telegram);
		}
		free(dp);
	}
	d_l2_conn->diag_l2_proto_data = NULL;

	if (d_l2_conn->diag_msg) {
		diag_freemsg(d_l2_conn->diag_msg);
		d_l2_conn->diag_msg = NULL;
	}

	//make sure the ECU detects the timeout
	diag_os_millisleep(KWP1281_T_RB_MAX);
	return 0;
}

/*
 * Sends a single Block (message) to the ECU
 */
static int
dl2p_vag_send(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg) {
	int rv = 0;

	if (diag_l2_debug & DIAG_DEBUG_WRITE) {
		fprintf(stderr,
			FLFMT "diag_l2_vag_send %p msg %p len %d called\n", FL,
			(void *)d_l2_conn, (void *)msg, msg->len);
	}

	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	//if this function is called right after receiving the first ECU telegram,
	//then it means that the caller doesn't care about the telegram, so delete it
	if (dp->ecu_id_telegram != NULL) {
		diag_freemsg(dp->ecu_id_telegram);
		dp->ecu_id_telegram = NULL;
	}

	//are we master? if not then the caller should be redesigned/fixed
	assert(dp->master == 1);

	//the length of the block (counter byte, title byte, data bytes and block end byte)
	dp->rxbuf[0] = msg->len + 3;
	//block counter
	dp->rxbuf[1] = dp->seq_nr;
	//block title (service identification - SID)
	dp->rxbuf[2] = msg->type;
	//data
	memcpy(&dp->rxbuf[3], msg->data, msg->len);
	//block end byte
	dp->rxbuf[3+msg->len] = KWP1281_END_BYTE;
	dp->rxoffset = 0;

	//time gap between messages
	unsigned long long elapsed_time = diag_os_hrtus(diag_os_gethrt() - dp->msg_finish_time)/1000;
	diag_os_millisleep(elapsed_time < KWP1281_T_RB_MIN ? KWP1281_T_RB_MIN-elapsed_time : 0);

	int retries = 0;
	//send the block to the ECU
	while (1) {
		if (d_l2_conn->diag_link->l1flags & DIAG_L1_DOESL2FRAME) {
			//for framed L0, must send the whole block at once
			rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, 0, dp->rxbuf, dp->rxbuf[0]+1, d_l2_conn->diag_l2_p4min);
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr, FLFMT "after send, rv=%d\n", FL,
					rv);
			}
			if (rv < 0) {
				return diag_iseterr(rv);
			}
			dp->msg_finish_time = diag_os_gethrt();
			break;
		}

		//send one byte at a time
		rv = diag_l1_send(d_l2_conn->diag_link->l2_dl0d, 0, &dp->rxbuf[dp->rxoffset], 1,
		                  d_l2_conn->diag_l2_p4min);
		unsigned long long byte_sent_time = diag_os_gethrt();

		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr, FLFMT "after send, rv=%d rtoffset=%d\n",
				FL, rv, dp->rxoffset);
		}

		if (rv < 0) {
			return diag_iseterr(rv);
		}

		//have we just written the last byte? if so, then no inverted response will arrive
		if (dp->rxoffset == dp->rxbuf[0]) {
			dp->msg_finish_time = diag_os_gethrt();
			break;
		}

		uint8_t recv_byte;
		//ECU should respond with an inverted byte at most after t_r8
		rv = diag_l1_recv(d_l2_conn->diag_link->l2_dl0d, 0, &recv_byte, 1, KWP1281_T_R8);
		unsigned long long complement_recv_time = diag_os_gethrt();

		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr, FLFMT "after recv, rv=%d\n", FL, rv);
		}

		if (rv < 0) {
			//finish communication if exceeded the max number of retries or if some other error than timeout
			if (++retries > KWP1281_TO_RETRIES ||
			    rv != DIAG_ERR_TIMEOUT) {
				return diag_iseterr(rv);
			}
			//retry sending the message
			dp->rxoffset = 0;
			//but only after another t_r8 - we must be sure that the receiver times-out
			//so that it will expect a re-started message
			elapsed_time = diag_os_hrtus(diag_os_gethrt() - byte_sent_time)/1000;
			diag_os_millisleep(elapsed_time < 2*KWP1281_T_R8 ? 2*KWP1281_T_R8-elapsed_time : 0);
			continue;
		}

		//check the received byte
		uint8_t complement = ~dp->rxbuf[dp->rxoffset];
		if (recv_byte != complement) {
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr,
					FLFMT
					"Received incorrect inverted byte: "
					"0x%.2X (expected 0x%.2X)\n",
					FL, (int)recv_byte, (int)complement);
			}
			//finish communication if exceeded the max number of retries
			if (++retries > KWP1281_TO_RETRIES) {
				return diag_iseterr(DIAG_ERR_BADCSUM);
			}
			//retry sending the message
			dp->rxoffset = 0;
			//but only after another t_r8 - we must be sure that the receiver times-out
			//so that it will expect a re-started message
			elapsed_time = diag_os_hrtus(diag_os_gethrt() - byte_sent_time)/1000;
			diag_os_millisleep(elapsed_time < 2*KWP1281_T_R8 ? 2*KWP1281_T_R8-elapsed_time : 0);
			continue;
		}

		dp->rxoffset++;
		//how much time elapsed since receiving a correct complement byte?
		elapsed_time = diag_os_hrtus(diag_os_gethrt() - complement_recv_time)/1000;
		//give ECU some time before sending next byte
		diag_os_millisleep(elapsed_time < KWP1281_T_R6_MIN ? KWP1281_T_R6_MIN-elapsed_time : 0);
	}

	//we are slave now
	dp->master = 0;

	return 0;
}

/*
 * Protocol receive routine
 */
static int
dl2p_vag_recv(struct diag_l2_conn *d_l2_conn, unsigned int timeout,
                       void (*callback)(void *handle, struct diag_msg *msg),
                       void *handle) {
	int rv;

	if (diag_l2_debug & DIAG_DEBUG_PROTO && timeout != 0) {
		fprintf(stderr,
			FLFMT
			"WARNING! l2_vag will ignore the given timeout! (%d "
			"msec)\n",
			FL, timeout);
	}

	//if it is the first call to the recv() function since startcomms, then
	//the message (ECU ID telegram) is already read, so call int_recv() only if the little shiny present
	//has already been collected
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	if (dp->ecu_id_telegram == NULL) {
		//call the internal routine
		rv = diag_l2_vag_int_recv(d_l2_conn, KWP1281_T_RB_MAX);
		if (rv < 0) {
			return rv;
		}
	} else {
		//int_recv() also does this
		if (d_l2_conn->diag_msg) {
			diag_freemsg(d_l2_conn->diag_msg);
			d_l2_conn->diag_msg = NULL;
		}
		//copy the ECU ID telegram address
		d_l2_conn->diag_msg = dp->ecu_id_telegram;
		//and make sure the pointer is no more
		dp->ecu_id_telegram = NULL;
	}

	if (diag_l2_debug & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "calling rcv callback, handle=%p\n", FL,
			handle);
	}

	//Call user callback routine
	//NOTE: if ECU returned NO_ACK, then the caller won't know what type it was (retry or unknown)
	if (callback) {
		callback(handle, d_l2_conn->diag_msg);
	}

	//Message no longer needed
	diag_freemsg(d_l2_conn->diag_msg);
	d_l2_conn->diag_msg = NULL;

	if (diag_l2_debug & DIAG_DEBUG_READ) {
		fprintf(stderr, FLFMT "rcv callback completed\n", FL);
	}

	return 0;
}

static struct diag_msg *
dl2p_vag_request(struct diag_l2_conn *d_l2_conn, struct diag_msg *msg, int *errval) {
	int rv, na_retry_cnt = 0;
	struct diag_msg *rmsg;
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;

	while (1) {
		//send the request
		rv = diag_l2_send(d_l2_conn, msg);
		if (rv < 0) {
			*errval = rv;
			return diag_pseterr(rv);
		}

		//and receive the response telegram
		rv = diag_l2_vag_int_recv(d_l2_conn, KWP1281_T_RB_MAX);
		if (rv < 0) {
			*errval = rv;
			return diag_pseterr(rv);
		}

		//if it isn't No Acknowledge - Retry, then ok
		if (d_l2_conn->diag_msg->type != KWP1281_SID_NO_ACK ||
		    d_l2_conn->diag_msg->data[0] != dp->seq_nr - 2) {
			break;
		}

		//but if it is, then we will repeat the request
		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr,
				FLFMT
				"Received No Acknowledge - Retry message\n",
				FL);
		}

		//accept at most KWP1281_NA_RETRIES of No Ack Retry messages in a row
		if (++na_retry_cnt == KWP1281_NA_RETRIES) {
			if (diag_l2_debug & DIAG_DEBUG_PROTO) {
				fprintf(stderr,
					FLFMT
					"\tbut too many Retry messages in a "
					"row already - aborting\n",
					FL);
			}
			*errval = DIAG_ERR_ECUSAIDNO;
			return diag_pseterr(*errval);
		}
		if (diag_l2_debug & DIAG_DEBUG_PROTO) {
			fprintf(stderr, FLFMT "\tso will retry\n", FL);
		}
		//re-send with the previous sequence number
		//NOTE: SAE J2818 says that "The Message number is NOT incremented by the transmitter for a repeated block."
		//      so we should send the repeated message with the same sequence number as the original message.
		//      However, when testing this on an ECU installed in a European VW the ECU didn't accept such a message
		//      (responded with another NO_ACK Retry) and a repeated message with an incremented sequence number
		//      was accepted.
		//      Can't test this with a VW from USA, so commenting-out for now. If US VWs indeed follow the specification
		//      strictly, then a possible solution would be to set/unset this behaviour based on a flag
		//      (eg. L2_vag specific option for enabling/disabling strict SAE J2818).
		//dp->seq_nr -= 2;
	}

	//now it's the requester's responsibility to take care of the telegram
	rmsg = d_l2_conn->diag_msg;
	d_l2_conn->diag_msg = NULL;

	return rmsg;
}

/*
 * Timeout, - if we don't send something to the ECU it will timeout soon,
 * so send it a keepalive message now.
 */
static void
dl2p_vag_timeout(struct diag_l2_conn *d_l2_conn) {
	struct diag_msg ack;
	memset(&ack, 0, sizeof(ack));
	ack.type = KWP1281_SID_ACK;

	if (diag_l2_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr, FLFMT "timeout impending for %p\n", FL,
			(void *)d_l2_conn);
	}

	//store the ECU ID address, so that the telegram won't get deleted by send()
	//(we don't want it to happen because of the keep-alive exchange)
	struct diag_l2_vag *dp = (struct diag_l2_vag *)d_l2_conn->diag_l2_proto_data;
	struct diag_msg *ecu_id_telegram = dp->ecu_id_telegram;
	dp->ecu_id_telegram = NULL;

	//Send the ACK message; important to use l2_send as it updates the timers
	int rv = diag_l2_send(d_l2_conn, &ack);
	if (rv < 0) {
		if (diag_l2_debug & DIAG_DEBUG_TIMER) {
			fprintf(stderr,
				FLFMT
				"KW1281 send keep-alive failed with the "
				"following error:\n\t%s\n",
				FL, diag_errlookup(rv));
		}
		return;
	}

	//we don't have to worry about ECU responding NoAck - it's just a keep-alive exchange
	//so it's ok as long as neither side timeouts
	rv = diag_l2_recv(d_l2_conn, 0, NULL, NULL);
	if (rv < 0 && diag_l2_debug & DIAG_DEBUG_TIMER) {
		fprintf(stderr,
			FLFMT
			"KW1281 receive keep-alive failed with the following "
			"error:\n\t%s\n",
			FL, diag_errlookup(rv));
	}

	//copy the ECU ID telegram address back where it belongs
	dp->ecu_id_telegram = ecu_id_telegram;
}

const struct diag_l2_proto diag_l2_proto_vag = {
	DIAG_L2_PROT_VAG,
	"VAG",
	DIAG_L2_FLAG_KEEPALIVE,
	dl2p_vag_startcomms,
	dl2p_vag_stopcomms,
	dl2p_vag_send,
	dl2p_vag_recv,
	dl2p_vag_request,
	dl2p_vag_timeout
};
