#ifndef _DIAG_VAG_H_
#define _DIAG_VAG_H_

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
 * VW Specific protocol info
 */
#if defined(__cplusplus)
extern "C" {
#endif

/*
 * ECU Addresses, needs to be odd parity
 */
#define DIAG_VAG_ECU_ALL	0x00	// Special: get fault codes from all */
#define DIAG_VAG_ECU_ENGINE	0x01	// Engine electronic I               */
#define DIAG_VAG_ECU_GEARBOX	0x02
#define DIAG_VAG_ECU_ABS	0x03	// Brake electronics */
#define DIAG_VAG_ECU_EZS        0x05	// EZS / Kessy modul                 */
#define DIAG_VAG_ECU_SEATPS	0x06    // Seat adjustment passenger side    */
#define DIAG_VAG_ECU_CENTDISP	0x07	// Central display system            */
#define DIAG_VAG_ECU_CLIMA	0x08	// Clima-/heating electronic         */
#define DIAG_VAG_ECU_CENTRAL	0x09    // Central electronic system         */
#define DIAG_VAG_ECU_ENGINE_II	0x11    // Engine electronic II              */
#define DIAG_VAG_ECU_CLUTCH	0x12    // Clutch electronic                 */
#define DIAG_VAG_ECU_DISTADJ    0x13    // Automaticly distance adjustment   */
#define DIAG_VAG_ECU_SHOCKABS	0x14    // Wheel shock absorber electronic   */
#define DIAG_VAG_ECU_AIRBAGS	0x15
#define DIAG_VAG_ECU_STEER      0x16    // Steering wheel electronic         */
#define DIAG_VAG_ECU_DASHBOARD  0x17    // Dashboard                         */
#define DIAG_VAG_ECU_HEATING    0x18    // Aux-/Independent-heating          */
#define DIAG_VAG_ECU_GATEWAY    0x19	// Gateway K<>CAN                    */
#define DIAG_VAG_ECU_ENGINE_III 0x21    // Engine electronic III             */
#define DIAG_VAG_ECU_4WD	0x22    // 4 Wheel-drive system              */
#define DIAG_VAG_ECU_BREAKAMP	0x23    // Break amplifier                   */
#define DIAG_VAG_ECU_TRACTION	0x24    // Traction
#define DIAG_VAG_ECU_IMMO	0x25    // Immobilizer                       */
#define DIAG_VAG_ECU_ELROOF	0x26    // El. Roof electronic               */
#define DIAG_VAG_ECU_CENTDISPR  0x27    // Central display rear              */
#define DIAG_VAG_ECU_LIGHT      0x29    // Light adjustment system           */
#define DIAG_VAG_ECU_NIVEAU	0x34    // Niveau system                     */
#define DIAG_VAG_ECU_CENTLOCK   0x35    // Door locking system               */
#define DIAG_VAG_ECU_SEATDS	0x36    // Seat adjustment driver side       */
#define DIAG_VAG_ECU_NAVIGAT    0x37    // Navigation system                 */
#define DIAG_VAG_ECU_LIGHTR     0x39    // Light adjustment system right-side*/
#define DIAG_VAG_ECU_DIESELPUMP 0x41    // Diesel pump electronic            */
#define DIAG_VAG_ECU_BREAKSUP	0x43    // Break support                     */
#define DIAG_VAG_ECU_STEERHELP	0x44    // Steering help system              */
#define DIAG_VAG_ECU_INTERIMON	0x45    // Interior Monitoring
#define DIAG_VAG_ECU_LOCKS	0x45
#if notdef
#define DIAG_VAG_ECU_CENTRAL    0x46    // Central modul comfort-system      */
#endif
#define DIAG_VAG_ECU_SOUND 	0x47    // Sound system
#define DIAG_VAG_ECU_AUTLIGHT   0x49    // Automatical light switch          */
#define DIAG_VAG_ECU_ECM	0x4B	// Emergency control monitoring */
#define DIAG_VAG_ECU_ELTRAC     0x51    // Electrical traction               */
#define DIAG_VAG_ECU_SPOILER	0x54	// Rear spoiler                      */
#define DIAG_VAG_ECU_LWR	0x55	// Headlamp leveling device          */
#define DIAG_VAG_ECU_RADIO	0x56	// Radio                             */
#define DIAG_VAG_ECU_TVTUNER    0x57    // TV-Tuner                          */
#define DIAG_VAG_ECU_TANK	0x58	// Additional tank		     */
#define DIAG_VAG_ECU_TOWSEC	0x59	// Tow security system               */
#define DIAG_VAG_ECU_BATTADJ    0x61	// Battery adjustment/control        */
#define DIAG_VAG_ECU_TYREPRES   0x65    // Tyre pressure modul               */
#define DIAG_VAG_ECU_SEAT       0x66    // Seat-/mirror-adjustment           */
#define DIAG_VAG_ECU_SPEECH     0x67    // Speech control system             */
#define DIAG_VAG_ECU_WIPER      0x68    // Wiper modul                       */
#define DIAG_VAG_ECU_TRAILER	0x69	// Trailer function                  */
#define DIAG_VAG_ECU_BATTLOAD	0x71	// Battery loading system            */
#define DIAG_VAG_ECU_EMERGENCY  0x75	// Emergency system                  */
#define DIAG_VAG_ECU_PARKING	0x76    // Parking help system               */
#define DIAG_VAG_ECU_SLD	0x78	// Sliding door                      */


/*
 * Block IDs "commands"
 */
#define DIAG_VAG_CMD_ECU_INFO	0x00
#define DIAG_VAG_CMD_TEST	0x04
#define DIAG_VAG_CMD_DTC_CLEAR	0X05
#define DIAG_VAG_CMD_END_COMMS	0X06
#define DIAG_VAG_CMD_DTC_RQST	0X07
#define DIAG_VAG_CMD_READ_DATA	0X08
#define DIAG_VAG_CMD_ACK	0X09
#define DIAG_VAG_CMD_RECODE	0x10
#define DIAG_VAG_CMD_SET_GROUP	0x11
#define DIAG_VAG_CMD_DATA_GROUP	0x12
#define DIAG_VAG_CMD_ADP_READ	0x21
#define DIAG_VAG_CMD_ADP_TEST	0x22
#define DIAG_VAG_CMD_SET_OTHER	0x28
#define DIAG_VAG_CMD_DATA_OTHER	0x29
#define DIAG_VAG_CMD_ADP_SAVE	0x2A
#define DIAG_VAG_CMD_LOGIN	0x2B

#define DIAG_VAG_RSP_ASCII	0XF6
#define DIAG_VAG_RSP_HEX	0XFC

#if defined(__cplusplus)
}
#endif
#endif /* _DIAG_VAG_H_ */
