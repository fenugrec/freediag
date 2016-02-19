/*
 * freediag - Vehicle Diagnostic Utility
 *
 *
 *
 * Copyright (C) 2001 Richard Almeida & Ibex Ltd (rpa@ibex.co.uk)
 * Copyright (C) 2015 fenugrec <fenugrec@users.sourceforge.net>
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
 */
#ifndef _DIAG_L2_VAG_H_
#define _DIAG_L2_VAG_H_

#if defined(__cplusplus)
extern "C" {
#endif

#define KWP1281_KW_BYTE_1 0x01
#define KWP1281_KW_BYTE_2 0x8A
#define KWP1281_END_BYTE  0x03

//Initialization-specific times
#define KWP1281_T_R0       300  //before 5baud Initialization Byte (Scan Tool -> ECU)
#define KWP1281_T_R1_MIN    80  //between Init Byte and Synchronization Byte (ECU -> Scan Tool)
#define KWP1281_T_R1_MAX   210
#define KWP1281_T_R2_MIN     5  //time between Sync Byte and KW1 Byte (ECU -> Scan Tool)
#define KWP1281_T_R2_MAX    20
#define KWP1281_T_R3_MIN     1  //time between KW1 and KW2 Byte (ECU -> Scan Tool)
#define KWP1281_T_R3_MAX    20
#define KWP1281_T_R4_MIN    25  //time between KW2 Byte and KW2 Complement (Scan Tool -> ECU)
#define KWP1281_T_R4_MAX    50
//After initialization
#define KWP1281_T_R5_MIN    25  //time between KW2 Complement and 1st ECU message (ECU -> Scan Tool)
#define KWP1281_T_R5_MAX    50
#define KWP1281_T_RK       231  //time ECU waits before re-sending the Sync Byte if KW2 Complement was incorrect (ECU -> Scan Tool)
//Communication-specific times
#define KWP1281_T_R6_MIN     1  //time Scan Tool waits before sending next byte to ECU
#define KWP1281_T_R6_MAX    50
#define KWP1281_T_R7_MIN     1  //time ECU waits before sending next byte to Scan Tool this is 0.5ms, actually...
#define KWP1281_T_R7_MAX    50
#define KWP1281_T_R8        55  //timeout while waiting for a message byte (ECU and Scan Tool; R6_MAX+5 or R7_MAX+5)
#define KWP1281_T_RB_MIN     1  //time between messages (ECU and Scan Tool)
#define KWP1281_T_RB      1000
#define KWP1281_T_RB_MAX  1100
//
#define KWP1281_NA_RETRIES   5  //number of No Ack retries before a message is discarded
#define KWP1281_TO_RETRIES   3  //number of time-out retries before a message is discarded

#define KWP1281_SID_ACK      0x09
#define KWP1281_SID_NO_ACK   0x0A

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_L2_VAG_H_ */
