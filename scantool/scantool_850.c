/*
 *      freediag - Vehicle Diagnostic Utility
 *
 *
 * Copyright (C) 2017, 2023 Adam Goldman
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
 *
 * Mostly ODBII Compliant Scan Tool (as defined in SAE J1978)
 *
 * CLI routines - 850 subcommand
 *
 * Extended diagnostics for '96-'98 Volvo 850, S40, C70, S70, V70, XC70 and V90
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include "diag.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l7.h"
#include "diag_err.h"
#include "diag_os.h"

#include "libcli.h"

#include "diag_l7_d2.h"
#include "diag_l7_kwp71.h"
#include "scantool.h"
#include "scantool_cli.h"

struct ecu_info {
	uint8_t addr;
	char *name;
	char *desc;
	char *dtc_prefix;
};

static struct ecu_info ecu_list[] = {
	{0x01, "abs", "antilock brakes", "ABS"},
#if 0
	/*
	 * Don't have an M4.3 ECU to test. Will probably need separate DTC and
	 * live data tables for M4.3.
	 */
	{0x10, "m43", "Motronic M4.3 engine management (DLC pin 3)", "EFI"}, /* 12700bps, KWP71 */
#endif
	{0x10, "m44old", "Motronic M4.4 engine management (old protocol)", "EFI"},
	{0x11, "msa", "MSA 15.7 engine management (diesel vehicles)","EFI"},
	/* 0x13 - Volvo Scan Tool tester address */
#if 0
	{0x15, "m18", "Motronic M1.8 engine management (960)", "EFI"}, /* 4800bps, KWP71 */
#endif
	{0x18, "add", "912-D fuel-driven heater (cold climate option)", "HEA"},
	{0x29, "ecc", "electronic climate control", "ECC"},
	{0x2d, "vgla", "alarm", "GLA"},
	{0x2e, "psl", "left power seat", "PSL"},
	{0x2f, "psr", "right power seat", "PSR"},
	/* 0x33 - J1979 OBD2 */
	{0x41, "immo", "immobilizer", "IMM"},
	{0x51, "combi", "combined instrument panel", "CI"},
	{0x58, "srs", "airbags", "SRS"},
	{0x6e, "aw50", "AW50-42 transmission", "AT"},
	{0x7a, "m44", "Motronic M4.4 engine management", "EFI"},
	{0, NULL, NULL, NULL}
};

struct dtc_table_entry {
	uint8_t ecu_addr;
	uint8_t raw_value;
	uint16_t dtc_suffix;
	char *desc;
};


// Taken from http://jonesrh.info/volvo850/freediag_v1_08_20171212.zip
// Many additions to the dtc_table added by jonesrh on 2017-12-10
//   (and made compilable on 2017-12-11).
static struct dtc_table_entry dtc_table[] = {
	//-----------------------------------------------------------------------------
	//  Define all known, interpreted ECU 10 (M44, via KWP71) [instead of D2 via either ELM327 or KKL]) DTCs.
	//  - The first two DTC lines below were already in scantool_850's dtc_table
	//    and demonstrate two different ways to retrieve that DTC --
	//    one via M44 addressed as 0x10 using KWP71 via VAG KKL cable, and
	//    one via M44 addressed as 0x7A using D2 (KWPD3B0) via ELM327 
	//      (or even via KKL, like Vol-FCR and Brick-Diag Free do, 
	//      if freediag implements D2 via KKL).
	//  - It's quite useful that freediag can communicate with the 
	//    '96-'98 850/S70/V70/C70/XC70 with either KWP71 or D2.  Impressive!
	//-----------------------------------------------------------------------------
	{0x10, 0x54, 445, "Pulsed secondary air injection system pump signal"},
    ////{0x7a, 0x54, 445, "Pulsed secondary air injection system pump signal"},  /* There's a more complete definition for this DTC in the 0x7A section below. */
	//------------------------------------------------------
	//  Define all known, interpreted ECU 51 (COMBI) DTCs.
	//------------------------------------------------------
	// ECU 51 has 22 DTCs: 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 14 15 16 17 20
	{0x51, 0x01, 222, "Vehicle Speed Signal too high"},
	{0x51, 0x02, 221, "Vehicle Speed Signal missing [usually due to bad ABS module solders] [P0500]"},
	{0x51, 0x03, 114, "Fuel Level sensor stuck / signal constant for 94 miles"},
	{0x51, 0x04, 112, "Fuel Level signal short to ground"},
	{0x51, 0x05, 113, "Low Fuel / Fuel Level signal interrupted"},
	{0x51, 0x06, 121, "Engine Coolant Temperature signal faulty [see \"http://www.volvotips.com/index.php/850-2/volvo-850-s70-v70-c70-service-repair-manual/volvo-850-instrument-panel-service-repair-manual/\"]"},
	{0x51, 0x07, 123, "48-pulse output Speed Signal short to supply"},
	{0x51, 0x08, 143, "48-pulse output Speed Signal short to ground"},
	{0x51, 0x09, 131, "12-pulse output Speed Signal short to supply"},
	{0x51, 0x0A, 141, "12-pulse output Speed Signal short to ground"},
	{0x51, 0x0B, 132, "Engine RPM signal missing"},
	{0x51, 0x0C, 124, "Engine RPM signal faulty"},
	{0x51, 0x0D, 211, "D+ alternator voltage signal missing (or too low) for >= 10 sec when > 1000 RPM"},
	{0x51, 0x0E, 133, "Fuel Level Signal To Trip Computer short to supply"},
	{0x51, 0x0F, 142, "Ambient Temperature signal missing"},
	{0x51, 0x10, 231, "COMBI microprocessor internal fault"},
	{0x51, 0x11, 174, "Wide Disparity in Fuel Levels and/or Fuel Consumption signal missing [DTC never seen, Freeze Frame sometimes precedes a Low Fuel situation and occasionally follows a Fillup]"},
	{0x51, 0x14, 174, "??Fuel Consumption signal (according to Brick-Diag Free v0.0.6.6)??"},
	{0x51, 0x15, 174, "??Fuel Consumption signal (according to Brick-Diag Free v0.0.6.6)??"},
	{0x51, 0x16, 174, "??Fuel Consumption signal (according to Brick-Diag Free v0.0.6.6)??"},
	{0x51, 0x17, 174, "??Fuel Consumption signal (according to Brick-Diag Free v0.0.6.6)??"},
	{0x51, 0x20, 232, "COMBI is not yet programmed"},
	//------------------------------------------------------
	//  Define all known, interpreted ECU 58 (SRS) DTCs.
	//------------------------------------------------------
	// ECU 58 has 53 DTCs: 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 52 53 65 66 67 68 69 6A 6B 6C 6D 6E 6F 70 71 72 73 78 79 7A 7B 7C 7D 7E 7F 80 81 82 83 88 89 8A 8B 8C 8D C7
	{0x58, 0x01, 112, "Crash Sensor Module internal fault"},
	{0x58, 0x02, 211, "Driver Airbag short circuit (in contact reel and/or in SRS harness to airbag, connectors, or igniter)"},
	{0x58, 0x03, 212, "Driver Airbag open circuit (in SRS harness, connector, or airbag)"},
	{0x58, 0x04, 213, "Driver Airbag short circuit to ground (in contact reel, airbag, wiring harness, or connectors)"},
	{0x58, 0x05, 214, "Driver Airbag short circuit to supply (in contact reel, airbag, wiring harness, or connectors)"},
	{0x58, 0x06, 221, "Passenger Airbag short circuit (in SRS harness to airbag, connectors, or igniter)"},
	{0x58, 0x07, 222, "Passenger Airbag open circuit (in SRS harness, connector, or airbag)"},
	{0x58, 0x08, 223, "Passenger Airbag short circuit to ground (in airbag, wiring harness, or connectors)"},
	{0x58, 0x09, 224, "Passenger Airbag short circuit to supply (in airbag, wiring harness, or connectors)"},
	{0x58, 0x0A, 231, "Left Seat Belt Tensioner short circuit (in SRS harness to tensioner, connectors, or igniter)"},
	{0x58, 0x0B, 232, "Left Seat Belt Tensioner open circuit (in SRS harness, connector, or tensioner)"},
	{0x58, 0x0C, 233, "Left Seat Belt Tensioner short circuit to ground (in tensioner, wiring harness, or connectors)"},
	{0x58, 0x0D, 234, "Left Seat Belt Tensioner short circuit to supply (in tensioner, wiring harness, or connectors)"},
	{0x58, 0x0E, 241, "Right Seat Belt Tensioner short circuit (in SRS harness to tensioner, connectors, or igniter)"},
	{0x58, 0x0F, 242, "Right Seat Belt Tensioner open circuit (in SRS harness, connector, or tensioner)"},
	{0x58, 0x10, 243, "Right Seat Belt Tensioner short circuit to ground (in tensioner, wiring harness, or connectors)"},
	{0x58, 0x11, 244, "Right Seat Belt Tensioner short circuit to supply (in tensioner, wiring harness, or connectors)"},
	{0x58, 0x52, 127, "SRS Warning Light short circuit to ground or open circuit [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"http://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"]"},
	{0x58, 0x53, 128, "SRS Warning Light short circuit to supply [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"http://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"]"},
	{0x58, 0x65, 127, "SRS Warning Light short circuit to ground or open circuit [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"http://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"]"},
	{0x58, 0x66, 128, "SRS Warning Light short circuit to supply [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"http://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"]"},
	{0x58, 0x67, 129, "Battery Voltage signal too low"},
	{0x58, 0x68, 213, "Driver Airbag signal too low"},
	{0x58, 0x69, 214, "Driver Airbag signal too high"},
	{0x58, 0x6A, 233, "Left Seat Belt Tensioner signal too low"},
	{0x58, 0x6B, 234, "Left Seat Belt Tensioner signal too high"},
	{0x58, 0x6C, 243, "Right Seat Belt Tensioner signal too low"},
	{0x58, 0x6D, 244, "Right Seat Belt Tensioner signal too high"},
	{0x58, 0x6E, 223, "Passenger Airbag signal too low"},
	{0x58, 0x6F, 224, "Passenger Airbag signal too high"},
	{0x58, 0x70, 313, "Left Rear Seat Belt Tensioner signal too low"},
	{0x58, 0x71, 314, "Left Rear Seat Belt Tensioner signal too high"},
	{0x58, 0x72, 323, "Right Rear Seat Belt Tensioner signal too low"},
	{0x58, 0x73, 324, "Right Rear Seat Belt Tensioner signal too high"},
	{0x58, 0x78, 211, "Driver Airbag signal faulty"},
	{0x58, 0x79, 212, "Driver Airbag signal missing"},
	{0x58, 0x7A, 231, "Left Seat Belt Tensioner signal faulty"},
	{0x58, 0x7B, 232, "Left Seat Belt Tensioner signal missing"},
	{0x58, 0x7C, 241, "Right Seat Belt Tensioner signal faulty"},
	{0x58, 0x7D, 242, "Right Seat Belt Tensioner signal missing"},
	{0x58, 0x7E, 221, "Passenger Airbag signal faulty"},
	{0x58, 0x7F, 222, "Passenger Airbag signal missing"},
	{0x58, 0x80, 311, "Left Rear Seat Belt Tensioner signal faulty"},
	{0x58, 0x81, 312, "Left Rear Seat Belt Tensioner signal missing"},
	{0x58, 0x82, 321, "Right Rear Seat Belt Tensioner signal faulty"},
	{0x58, 0x83, 322, "Right Rear Seat Belt Tensioner signal missing"},
	{0x58, 0x88, 210, "Driver Airbag fault"},
	{0x58, 0x89, 230, "Left Seat Belt Tensioner fault"},
	{0x58, 0x8A, 240, "Right Seat Belt Tensioner fault"},
	{0x58, 0x8B, 220, "Passenger Airbag fault"},
	{0x58, 0x8C, 310, "Left Rear Seat Belt Tensioner fault"},
	{0x58, 0x8D, 320, "Right Rear Seat Belt Tensioner fault"},
	{0x58, 0xC7, 114, "Control Module faulty signal"},
	//------------------------------------------------------
	//  Define all known, interpreted ECU 01 (ABS) DTCs.
	//------------------------------------------------------
	// ECU 01 has 44 DTCs: 10 11 12 13 14 15 20 21 22 23 24 25 30 31 32 33 34 35 40 41 42 43 44 45 50 51 52 54 55 56 60 61 64 65 66 67 70 72 75 77 80 81 82 83
	{0x01, 0x10, 311, "Left Front Wheel Sensor, open/short [or bad ABS module solders or ignition switch]"},
	{0x01, 0x11, 321, "Left Front Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x12, 888, "??Left Front Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x13, 211, "Left Front Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x14, 221, "Left Front Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x15, 888, "??Left Front Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x20, 312, "Right Front Wheel Sensor, open/short [or bad ABS module solders or ignition switch]"},
	{0x01, 0x21, 322, "Right Front Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x22, 888, "??Right Front Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x23, 212, "Right Front Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x24, 222, "Right Front Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x25, 888, "??Right Front Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x30, 313, "Left Rear Wheel Sensor, open/short [or bad ABS module solders or ignition switch]"},
	{0x01, 0x31, 323, "Left Rear Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x32, 888, "??Left Rear Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x33, 213, "Left Rear Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch, or stuck emergency brake]"},
	{0x01, 0x34, 223, "Left Rear Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x35, 888, "??Left Rear Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x40, 314, "Right Rear Wheel Sensor, open/short [or bad ABS module solders or ignition switch]"},
	{0x01, 0x41, 324, "Right Rear Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x42, 888, "??Right Rear Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x43, 214, "Right Rear Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch, or stuck emergency brake]"},
	{0x01, 0x44, 224, "Right Rear Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch]"},
	{0x01, 0x45, 888, "??Right Rear Wheel Sensor, ?????? [or bad ABS module solders or ignition switch]??"},
	{0x01, 0x50, 411, "Left Front Wheel Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x51, 413, "Right Front Wheel Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x52, 421, "Rear Wheels Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x54, 412, "Left Front Wheel Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x55, 414, "Right Front Wheel Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x56, 422, "Rear Wheels Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x60, 423, "TRACS Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch]"},
	{0x01, 0x61, 424, "??TRACS Pressure Switch, circuit open/short [or bad wiring, hydraulic modulator, brake light switch, ABS module, ignition switch, or blown fuse 12 (STOP LAMPS)]??"},
	{0x01, 0x64, 141, "Brake Pedal Sensor, short [or bad ABS module solders, wiring, brake pedal sensor, or ignition switch]"},
	{0x01, 0x65, 142, "Brake Light/Pedal Switch, open/short or adjustment [or brake light bulb, bad ABS module solders, wiring, or ignition switch]"},
	{0x01, 0x66, 144, "TRACS Disengaged to Avoid Front Brake Discs Overheating [or bad ABS module solders or ignition switch]"},
	{0x01, 0x67, 143, "??Missing or Faulty Vehicle Speed Signal from ABS module?? [or bad ABS module solders, ABS module memory fault, or otherwise faulty ABS module or ignition switch]"},
	{0x01, 0x70, 443, "Pump motor fault [or fuse 9 (ABS PUMP MOTOR), pump power plug not seated properly, bad wiring (eg, insulation that falls off, any short/open), bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]"},
	{0x01, 0x72, 431, "ABS Module, general hardware fault [or bad ABS module solders or ignition switch]"},
	{0x01, 0x75, 432, "ABS Module, general interference fault [or bad ABS module solders or ignition switch]"},
	{0x01, 0x77, 433, "Battery Voltage, ?too high?"},
	{0x01, 0x80, 441, "ABS Microprocessors, redundant calculations mismatch [or fuse 14 (ABS MAIN SUPPLY), bad ABS module solders, wheel sensor wiring close to interference, ignition switch, or bad ABS module]"},
	{0x01, 0x81, 444, "??ABS Module, internal valve reference voltage error or no power to hydraulic valves [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??"},
	{0x01, 0x82, 442, "??ABS Module, leakage current or Hydraulic Modulator Pump Pressure, too low [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??"},
	{0x01, 0x83, 445, "??ABS Module, either inlet or return valve circuit fault [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??"},
	//----------------------------------------------------------------------------------------
	//  Define all known, interpreted ECU 6E (AW 50-42, both Gas & Diesel, and AW 42-AWD) DTCs.
	//----------------------------------------------------------------------------------------
	// ECU 6E has 39 DTCs: 13 17 18 35 36 37 38 39 3A 3B 3C 3D 3E 3F 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50 51 55 60 61 62 63 64 E0
	{0x6e, 0x13, 332, "Torque converter lock-up solenoid open circuit"},  /* Use Adam Goldman's scantool_850.c definition, since he has analyzed this error */
	{0x6E, 0x17, 888, "Has been seen in 1 car with P0120, so might be AT-213, AT-223, or otherwise Throttle Position Sensor related [eg, TPS signal too high/low/erratic, open or short in TPS signal ground or power ground, TPS sensor circuit failure, poor terminal contact, ECM to TCM wiring close to interference, or faulty TCM]"},
	{0x6E, 0x18, 888, "Has been seen in 2 cars with P0120, so might be AT-213, AT-223, or otherwise Throttle Position Sensor related [eg, TPS signal too high/low/erratic, open or short in TPS signal ground or power ground, TPS sensor circuit failure, poor terminal contact, ECM to TCM wiring close to interference, or faulty TCM]"},
	{0x6E, 0x35, 114, "Mode Selector Switch, circuit malfunction [eg, poor terminal contact, open circuit or short circuit to supply, poor TCM connector terminal contact, driving mode selector module, or cluster fault feeding transmission fault light]"},
	{0x6E, 0x36, 121, "Shift Solenoid S1 circuit, short to ground [eg, bad wiring or solenoid, or low/dirty fluid] [P0750]"},
	{0x6E, 0x37, 122, "Shift Solenoid S1 circuit, open in either signal cable or power ground, or short to supply in signal cable  [eg, bad wiring or solenoid, poor terminal contact at TCM connector, bad TCM, or low/dirty fluid] [P0750?]"},
	{0x6E, 0x38, 123, "Line Pressure Solenoid STH circuit, short to supply [eg, bad wiring or TCM, or low/dirty fluid] [P0745]"},
	{0x6E, 0x39, 124, "Mode Selector Switch, faulty or short circuit to ground [eg, \"W\" button depressed > 25 sec, controls sticking, loose parts, bad resistance, bad wiring]"},
	{0x6E, 0x3A, 131, "Line Pressure Solenoid STH circuit, open or short to ground [eg, bad wiring, poor terminal contact, bad TCM, or low/dirty fluid]"},
	{0x6E, 0x3B, 132, "TCM Fault, amplifier STH short circuit [eg, poor terminal contact at TCM connector, open circuit in TCM voltage supply or grounds, bad TCM, or low/dirty fluid] [P0745?]"},
	{0x6E, 0x3C, 134, "Engine Load signal"},
	{0x6E, 0x3D, 141, "Oil Temp Sensor"},
	{0x6E, 0x3E, 142, "Oil Temp Sensor"},
	{0x6E, 0x3F, 143, "Kickdown switch circuit"},
	{0x6E, 0x40, 213, "Throttle Position Sensor signal too high [P0120]"},
	{0x6E, 0x41, 221, "Shift solenoid S2 circuit, short to ground [eg, bad wiring or solenoid, or low/dirty fluid] [P0755]"},
	{0x6E, 0x42, 222, "Shift solenoid S2 circuit, open in either signal cable or power ground, or short to supply in signal cable [eg, bad wiring or solenoid, poor terminal contact at TCM connector, bad TCM, or low/dirty fluid] [P0755]"},
	{0x6E, 0x43, 223, "Throttle Position Sensor signal too low [P0120]"},
	{0x6E, 0x44, 232, "[might be either AT-232 or AT-233] Vehicle Speed Signal (VSS) from (ABS then) COMBI is missing or incorrect [eg, bad ABS module solders, CI-221 or CI-222, bad speedometer, bad wiring between (or poor terminal contact at) speedometer or TCM] [P0500]??"},
	{0x6E, 0x45, 235, "Oil Temperature"},
	{0x6E, 0x46, 245, "Torque Limiting Circuit, open or short [eg, bad wiring between TCM and ECM, bad TCM, poor terminal contact, bad ECM]"},
	{0x6E, 0x47, 311, "[might be either AT-311 or AT-312] Transmission RPM signal missing or incorrect [eg, open or short circuit from bad wiring, bad Transmission RPM sensor, CI-222 (ie, VSS excessively high), poor terminal contact, interference] [P0715]??"},
	{0x6E, 0x48, 313, "Gear position sensor signal faulty / PNP switch fault [P0705?]"},
	{0x6E, 0x49, 412, "Control Module ROM read fault [P0601?]"},
	{0x6E, 0x4A, 322, "Gear 2, incorrect ratio [P0732] / Alternatively, on earlier models: Gear Ratio Info Incorrect [P0730]"},
	{0x6E, 0x4B, 323, "Gear 3, incorrect ratio [P0733] / Alternatively, on earlier models: Lock-Up Slips Or Is Not Engaged"},
	{0x6E, 0x4C, 324, "Gear 4, incorrect ratio [P0734]"},
	{0x6E, 0x4D, 331, "Lock-up solenoid SL circuit, short to supply [P0740]"},
	{0x6E, 0x4E, 332, "Lock-up solenoid SL circuit, open [P0740]"},
	{0x6E, 0x4F, 333, "Lock-up solenoid SL circuit, short to ground [P0740]"},
	{0x6E, 0x50, 411, "Control Module EEPROM write fault [eg, low battery power, bad TCM]"},
	{0x6E, 0x51, 421, "Battery Volts too low"},
	{0x6E, 0x55, 511, "Control Module Communication"},
	{0x6E, 0x60, 522, "Control Module Communication"},
	{0x6E, 0x61, 523, "Control Module Communication"},
	{0x6E, 0x62, 524, "Control Module Communication"},
	{0x6E, 0x63, 525, "Control Module Communication"},
	{0x6E, 0x64, 526, "Control Module Communication"},
	{0x6E, 0xE0, 527, "Control Module Communication"},
	//-----------------------------------------------------------------------------------------
	//  Define all known, interpreted ECU 2E (Power Seat Left) / ECU 2F (Power Seat Right) DTCs.
	//-----------------------------------------------------------------------------------------
	// ECU 2E has 29 DTCs: 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D
	{0x2E, 0x01, 112, "Legroom Motor 1 Pot signal too high"},
	{0x2E, 0x02, 121, "Backrest Motor 2 Pot signal too high"},
	{0x2E, 0x03, 122, "Seat Rear edge Motor 3 Pot signal too high"},
	{0x2E, 0x04, 211, "Seat front edge motor 4 Pot signal too high"},
	{0x2E, 0x05, 212, "Legroom Motor 1 Pot signal too low"},
	{0x2E, 0x06, 222, "Backrest Motor 2 Pot signal too low"},
	{0x2E, 0x07, 223, "Seat Rear edge Motor 3 Pot signal too low"},
	{0x2E, 0x08, 312, "Seat front edge motor 4 Pot signal too low"},
	{0x2E, 0x09, 123, "(Legroom) Motor 1 Running Even Though Its Button Not Operated"},
	{0x2E, 0x0A, 131, "Backrest Motor 2 Movement when not permitted"},
	{0x2E, 0x0B, 132, "Seat Rear edge Motor 3 Movement when not permitted"},
	{0x2E, 0x0C, 133, "Seat front edge motor 4 Movement when not permitted"},
	{0x2E, 0x0D, 143, "Legroom Motor 1 Moves in wrong direction"},
	{0x2E, 0x0E, 144, "Backrest Motor 2 Moves in wrong direction"},
	{0x2E, 0x0F, 214, "Seat Rear edge Motor 3 Moves in wrong direction"},
	{0x2E, 0x10, 224, "Seat front edge motor 4 Moves in wrong direction"},
	{0x2E, 0x11, 323, "Memory 1, Fault in stored memory position"},
	{0x2E, 0x12, 322, "Memory 2, Fault in stored memory position"},
	{0x2E, 0x13, 321, "Memory 3, Fault in stored memory position"},
	{0x2E, 0x14, 424, "Control Panel Not Connected [ignore since always set]"},
	{0x2E, 0x15, 421, "Fault in Control Module"},
	{0x2E, 0x16, 422, "Battery Voltage too low"},
	{0x2E, 0x17, 423, "Control button activated too long"},
	{0x2E, 0x18, 411, "Legroom motor 1 not calibrated"},
	{0x2E, 0x19, 412, "Backrest motor 2 not calibrated"},
	{0x2E, 0x1A, 413, "Seat Rear edge motor 3 not calibrated"},
	{0x2E, 0x1B, 414, "Seat front edge motor 4 not calibrated"},
	{0x2E, 0x1C, 331, "Entry Position not stored"},
	{0x2E, 0x1D, 332, "Original Position not stored"},
	// ECU 2F has 29 DTCs: 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D
	{0x2F, 0x01, 112, "Legroom Motor 1 Pot signal too high"},
	{0x2F, 0x02, 121, "Backrest Motor 2 Pot signal too high"},
	{0x2F, 0x03, 122, "Seat Rear edge Motor 3 Pot signal too high"},
	{0x2F, 0x04, 211, "Seat front edge motor 4 Pot signal too high"},
	{0x2F, 0x05, 212, "Legroom Motor 1 Pot signal too low"},
	{0x2F, 0x06, 222, "Backrest Motor 2 Pot signal too low"},
	{0x2F, 0x07, 223, "Seat Rear edge Motor 3 Pot signal too low"},
	{0x2F, 0x08, 312, "Seat front edge motor 4 Pot signal too low"},
	{0x2F, 0x09, 123, "(Legroom) Motor 1 Running Even Though Its Button Not Operated"},
	{0x2F, 0x0A, 131, "Backrest Motor 2 Movement when not permitted"},
	{0x2F, 0x0B, 132, "Seat Rear edge Motor 3 Movement when not permitted"},
	{0x2F, 0x0C, 133, "Seat front edge motor 4 Movement when not permitted"},
	{0x2F, 0x0D, 143, "Legroom Motor 1 Moves in wrong direction"},
	{0x2F, 0x0E, 144, "Backrest Motor 2 Moves in wrong direction"},
	{0x2F, 0x0F, 214, "Seat Rear edge Motor 3 Moves in wrong direction"},
	{0x2F, 0x10, 224, "Seat front edge motor 4 Moves in wrong direction"},
	{0x2F, 0x11, 323, "Memory 1, Fault in stored memory position"},
	{0x2F, 0x12, 322, "Memory 2, Fault in stored memory position"},
	{0x2F, 0x13, 321, "Memory 3, Fault in stored memory position"},
	{0x2F, 0x14, 424, "Control Panel Not Connected [ignore since always set]"},
	{0x2F, 0x15, 421, "Fault in Control Module"},
	{0x2F, 0x16, 422, "Battery Voltage too low"},
	{0x2F, 0x17, 423, "Control button activated too long"},
	{0x2F, 0x18, 411, "Legroom motor 1 not calibrated"},
	{0x2F, 0x19, 412, "Backrest motor 2 not calibrated"},
	{0x2F, 0x1A, 413, "Seat Rear edge motor 3 not calibrated"},
	{0x2F, 0x1B, 414, "Seat front edge motor 4 not calibrated"},
	{0x2F, 0x1C, 331, "Entry Position not stored"},
	{0x2F, 0x1D, 332, "Original Position not stored"},
	//----------------------------------------------------------
	//  Define all known, interpreted ECU 7A (Motronic 4.4) DTCs.
	//----------------------------------------------------------
	// ECU 7A has 93 DTCs: 0A 0C 0D 0E 0F 10 11 13 18 1A 1B 1D 28 31 32 33 34 35 36 37 39 3D 3E 3F 40 41 42 43 44 4B 4D 50 51 53 54 55 56 59 5D 5E 5F 60 61 62 63 65 67 6B 6D 6E 6F 70 73 75 77 78 79 7A 7B 7C 7F 80 81 82 96 97 98 99 9A 9B A4 A8 A9 AA B4 B5 B6 B7 B8 B9 D2 D3 DE E1 E5 E6 ED EE EF F1 F4 FD FE
	{0x7A, 0x0A, 212, "HO2S sensor signal, front"},
	{0x7A, 0x0C, 153, "HO2S sensor signal, rear [P0136 (faulty), P0137 (low), or P0138 (high)]"},
	{0x7A, 0x0D, 521, "Front HO2S, preheating"},
	{0x7A, 0x0E, 522, "Rear HO2S, preheating malfunction [P0141]"},
	{0x7A, 0x0F, 435, "Front HO2S slow response"},
	{0x7A, 0x10, 436, "Rear HO2S compensation [P0133] [see 1st 3 items in Bing search for 'P0133 \"EFI-436\"' and 1st 3 items in Google search]"},
	{0x7A, 0x11, 425, "Rear HO2S, control (or possibly Temperature warning, level 1)"},
	{0x7A, 0x13, 343, "Fuel Pump Relay"},
	{0x7A, 0x18, 211, "CO Potentiometer"},
	{0x7A, 0x1A, 231, "Long term fuel trim, part load [P0171 or P0172] [defective MAF, fuel pressure high/low (P0171/P0172), elbow at intake manifold (hidden underneath coolant thermostat) or elbow at PTC or vacuum leaks (if P0172), see OTP Volvo 850 DVD, see 7-step checkup in section including \"2-3-1 (Adaptive HO2S Control Lean At Part Load)\" of \"http://www.volvotips.com/index.php/850-2/volvo-850-s70-v70-c70-service-repair-manual/volvo-850-diagnostics-with-codes-obd/\"]"},
	{0x7A, 0x1B, 232, "Long term fuel trim, idling [P0172 or P0171] [elbow at intake manifold (hidden underneath coolant thermostat) or elbow at PTC or vacuum leaks (if P0172), fuel pressure high/low (P0171/P0172), defective MAF, see OTP Volvo 850 DVD, see 10-step checkup in section \"2-3-2 (Adaptive HO2S Control Provides Richer Mixture At Idle)\" of \"http://www.volvotips.com/index.php/850-2/volvo-850-s70-v70-c70-service-repair-manual/volvo-850-diagnostics-with-codes-obd/\"]"},
	{0x7A, 0x1D, 666, "DTC in transmission CM"},
	{0x7A, 0x28, 443, "TWC efficiency"},
	{0x7A, 0x31, 542, "Misfire, more than 1 cylinder"},
	{0x7A, 0x32, 451, "Misfire cylinder 1"},
	{0x7A, 0x33, 452, "Misfire cylinder 2"},
	{0x7A, 0x34, 453, "Misfire cylinder 3 [P0303]"},
	{0x7A, 0x35, 454, "Misfire cylinder 4"},
	{0x7A, 0x36, 455, "Misfire cylinder 5"},
    ////{0x7A, 0x37, 445, "??Misfire cylinder 6??.  [There is another more believable code that maps to EFI-445, so let's leave this commented out.]"},
	{0x7A, 0x39, 353, "Immobilizer"},
	{0x7A, 0x3D, 544, "Misfire, more than 1 cylinder"},
	{0x7A, 0x3E, 543, "Misfire exhaust value of at least one cylinder"},
	{0x7A, 0x3F, 551, "Misfire, cylinder 1"},
	{0x7A, 0x40, 552, "Misfire, cylinder 2"},
	{0x7A, 0x41, 553, "Misfire, cylinder 3"},
	{0x7A, 0x42, 554, "Misfire, cylinder 4"},
	{0x7A, 0x43, 555, "Misfire, cylinder 5"},
	{0x7A, 0x44, 556, "Misfire, cylinder 6"},
	{0x7A, 0x4B, 545, "Misfire, at least 1 cylinder"},
	{0x7A, 0x4D, 444, "Accelerometer signal [P1307 (low); P1308 (high)] [see OTP 850 DVD, TP-2318201, p149 and p268, for possible sources]"},
	{0x7a, 0x50, 442, "Pulsed secondary air injection system flow fault [P0410] [see OTP Volvo 850 DVD, TP-2308202, p669, p78, p316]"},
	{0x7a, 0x51, 448, "Pulsed secondary air injection system pump flow fault [P0410] [see OTP Volvo 850 DVD, TP-2308202, p669, p78, p361]"},
	{0x7a, 0x53, 446, "Pulsed secondary air injection system valve leakage [P0410] [see OTP Volvo 850 DVD, TP-2308202, p669, p78, p344]"},
	{0x7A, 0x54, 445, "Pulsed secondary air injection system pump signal [P0410] [see OTP 850 DVD, TP-2318201, p268 and p44]"},
	{0x7A, 0x55, 447, "Pulsed secondary air injection system solenoid valve signal [P0413 (missing); P0414 (low/high)] [see OTP 850 DVD, TP-2318201, p268 and p44]"},
	{0x7A, 0x56, 241, "EGR system"},
	{0x7A, 0x59, 413, "EGR temperature sensor signal"},
	{0x7A, 0x5D, 315, "Canister purge (CP) valve leakage"},
	{0x7A, 0x5E, 611, "Fuel Tank system, large leakage [P0455]"},
	{0x7A, 0x5F, 614, "EVAP canister shut off valve flow fault"},
	{0x7A, 0x60, 616, "EVAP canister shut off valve signal"},
	{0x7A, 0x61, 612, "Fuel Tank system, small leakage [P0442] [check fuel cap seated and sealed properly, leaking fuel tank, filler pipe, EVAP canister or its shut-off valve, lines connecting any of those, or bad fuel tank pressure sensor]"},
	{0x7A, 0x62, 541, "Canister purge (CP) valve signal"},
	{0x7A, 0x63, 621, "Fuel tank pressure sensor signal"},
	{0x7A, 0x65, 112, "MFI Control Module (CM) fault"},
	{0x7A, 0x67, 112, "Memory Fault (ROM) In Engine Control Module (ECM)"},
	{0x7A, 0x6B, 132, "Battery Voltage"},
	{0x7A, 0x6D, 315, "Canister purge (CP) valve leakage"},
	{0x7A, 0x6E, 131, "RPM sensor signal missing"},
	{0x7A, 0x6F, 214, "RPM sensor signal sporadic faulty"},
	{0x7A, 0x70, 314, "Camshaft Position sensor (CMP) signal [P0340] [see OTP 850 DVD, TP-2308202, p262 for possible sources]"},
	{0x7A, 0x73, 121, "Mass Air Flow (MAF) sensor signal"},
	{0x7A, 0x75, 411, "Throttle Position (TP) sensor signal"},
	{0x7A, 0x77, 422, "Atmospheric pressure sensor signal"},
	{0x7A, 0x78, 311, "Engine RPM high for too long while no Vehicle Speed"},
	{0x7A, 0x79, 355, "Mass air flow (MAF) sensor, faulty signal"},
	{0x7A, 0x7A, 112, "Fault in Engine Control Module (ECM) NTC switch"},
	{0x7A, 0x7B, 123, "Engine Coolant Temperature (ECT) sensor [P0116 (faulty), P0117 (low), P0118 (high), or P0115 (missing)]"},
	{0x7A, 0x7C, 251, "Ambient Temperature sensor signal [P0111 (sporadic), P0112 (low), or P0113 (high)]"},
	{0x7A, 0x7F, 112, "Fault in Engine Control Module (ECM) Temperature"},
	{0x7A, 0x80, 432, "Temperature warning, level 1"},
	{0x7A, 0x81, 513, "Temperature warning, level 2"},
	{0x7A, 0x82, 225, "A/C pressure sensor signal"},
	{0x7A, 0x96, 115, "Injector 1"},
	{0x7A, 0x97, 125, "Injector 2"},
	{0x7A, 0x98, 135, "Injector 3"},
	{0x7A, 0x99, 145, "Injector 4"},
	{0x7A, 0x9A, 155, "Injector 5"},
	{0x7A, 0x9B, 165, "Injector 6"},
	{0x7A, 0xA4, 335, "Request for MIL lighting from TCM [or possibly Comm Failure between Motronic 4.4 ECM and AW 50-42 TCM] [P1618 (High), P1617 (Low/Missing/Faulty)] [check grounds 31/71, 31/32, 31/33; check TCM pin B15 to M44 pin B26 circuit & connections] [see OTP 850 DVD: TP-2308032, pp275-280; TP-2318201, p43, p269]"},
	{0x7A, 0xA8, 223, "Idle Air Control (IAC) valve opening signal"},
	{0x7A, 0xA9, 245, "IAC valve closing signal"},
	{0x7A, 0xAA, 233, "Adaptive idle air trim"},
	{0x7A, 0xB4, 531, "Power stage group A"},
	{0x7A, 0xB5, 532, "Power stage group B"},
	{0x7A, 0xB6, 533, "Power stage group C"},
	{0x7A, 0xB7, 534, "Power stage group D"},
	{0x7A, 0xB8, 536, "??Power stage group E??"},
	{0x7A, 0xB9, 537, "??Power stage group F??"},
	{0x7A, 0xD2, 143, "Front knock sensor (KS)"},
	{0x7A, 0xD3, 433, "Rear knock sensor (KS)"},
	{0x7A, 0xDE, 112, "Fault in Engine Control Module (ECM) Knock Control"},
	{0x7A, 0xE1, 233, "Long term idle air trim"},
	{0x7A, 0xE5, 416, "Boost pressure reduction from TCM"},
	{0x7A, 0xE6, 414, "Boost pressure regulation"},
	{0x7A, 0xED, 112, "Fault in Engine Control Module (ECM) NTC switch"},
	{0x7A, 0xEE, 432, "Temperature warning, level 1"},
	{0x7A, 0xEF, 513, "Temperature warning, level 2"},
	{0x7A, 0xF1, 154, "EGR system leakage"},
	{0x7A, 0xF4, 535, "Turbocharger (TC) control valve signal"},
	{0x7A, 0xFD, 514, "Engine coolant fan, low speed"},
	{0x7A, 0xFE, 621, "Fuel tank pressure sensor signal"},
	//------------------------------------------------------
	//  Define all known, interpreted ECU 11 (MSA 15.7) DTCs.
	//------------------------------------------------------
	// ECU 11 has 39 DTCs: 00 01 02 03 04 05 06 08 09 0A 0B 0C 0D 0F 10 11 12 14 19 1A 1B 20 21 22 23 24 26 2A 2B 2D 2E 2F 30 31 32 33 34 36 FE
	{0x11, 0x00, 131, "Engine Speed (RPM) signal"},
	{0x11, 0x01, 719, "Secondary Engine Speed signal"},
	{0x11, 0x02, 132, "Battery Voltage"},
	{0x11, 0x03, 711, "Needle Lift sensor signal"},
	{0x11, 0x04, 732, "Accelerator Pedal Position sensor signal"},
	{0x11, 0x05, 123, "Engine Coolant Temp (ECT) sensor signal"},
	{0x11, 0x06, 122, "Intake Air Temp (IAT) sensor signal"},
	{0x11, 0x08, 712, "Fuel Temp sensor signal"},
	{0x11, 0x09, 415, "Boost Pressure sensor signal"},
	{0x11, 0x0A, 112, "Fault in Engine Control Module (ECM) Barometric Pressure signal"},
	{0x11, 0x0B, 121, "Mass Air Flow (MAF) sensor signal"},
	{0x11, 0x0C, 743, "Cruise Control switch signal"},
	{0x11, 0x0D, 112, "Fault in Engine Control Module (ECM) Reference Voltage"},
	{0x11, 0x0F, 112, "Fault in Engine Control Module (ECM)"},
	{0x11, 0x10, 311, "Engine RPM vs. Vehicle Speed signal discrepancy"},
	{0x11, 0x11, 235, "Exhaust gas recirculation (EGR) controller signal"},
	{0x11, 0x12, 242, "Turbocharger Control Valve (TCV) signal"},
	{0x11, 0x14, 515, "Engine Coolant Fan, high speed signal"},
	{0x11, 0x19, 713, "Injection Advance Control Valve signal"},
	{0x11, 0x1A, 323, "CHECK ENGINE light (CEL) / Malfunction indicator lamp (MIL)"},
	{0x11, 0x1B, 721, "Glowplug Indicator Light signal"},
	{0x11, 0x20, 714, "Fuel Shut-off Valve signal"},
	{0x11, 0x21, 730, "Brake Pedal Switch signal"},
	{0x11, 0x22, 731, "Clutch Switch signal"},
	{0x11, 0x23, 724, "Engine Coolant Heater Relay signal"},
	{0x11, 0x24, 112, "Memory Fault (ROM) In Engine Control Module (ECM)"},
	{0x11, 0x26, 514, "Engine Coolant Fan, low speed signal"},
	{0x11, 0x2A, 725, "Main Relay signal"},
	{0x11, 0x2B, 715, "Fuel Regulation"},
	{0x11, 0x2D, 726, "Terminal 15-supply to Engine Control Module (ECM)"},
	{0x11, 0x2E, 716, "Fuel Quantity Actuator signal"},
	{0x11, 0x2F, 718, "Ignition Timing Control"},
	{0x11, 0x30, 335, "Request for MIL lighting from TCM [or possibly Comm Failure between MSA 15.7 ECM and AW 50-42 TCM] [see OTP 850 DVD]"},
	{0x11, 0x31, 353, "Comm Failure with Immobilizer"},
	{0x11, 0x32, 742, "Comm Failure between MSA 15.7 ECM and AW 50-42 TCM [or possibly Request for MIL lighting from TCM] [see OTP 850 DVD]"},
	{0x11, 0x33, 225, "A/C Pressure sensor signal"},
	{0x11, 0x34, 717, "Fuel Quantity Regulator Position sensor signal"},
	{0x11, 0x36, 732, "Accelerator pedal position sensor signal"},
	{0x11, 0xFE, 131, "Engine Speed (RPM) signal"},
	//---------------------------------------------------------
	//  Define all known, interpreted ECU 41 (Immobilizer) DTCs.
	//---------------------------------------------------------
	// ECU 41 has 23 DTCs: 01 02 03 04 05 06 07 30 31 32 33 34 35 36 37 38 51 52 53 54 55 F7 F8
	{0x41, 0x01, 112, "Internal EEPROM memory fault"},
	{0x41, 0x02, 211, "Comm fault with engine ECU. [might be either IMM-211 or IMM-213]"},
	{0x41, 0x03, 212, "No Contact with Antenna. [might be either IMM-212 or IMM-233]"},
	{0x41, 0x04, 221, "No Response from Key Transponder. [might be either IMM-221 or IMM-234]"},
	{0x41, 0x05, 222, "Key Code not in memory. [might be either IMM-222 or IMM-235]"},
	{0x41, 0x06, 223, "Comm fault from Transponder to IMMO Control Module"},
	{0x41, 0x07, 336, "Control circuit to VGLA, fault in VGLA or LED circuit (P600)"},
	{0x41, 0x30, 334, "Control circuit to Indicator Lamp, fault in circuit. [might be either IMM-334 or IMM-331]"},
	{0x41, 0x31, 335, "Starter Motor Control circuit, fault in circuit. [might be either IMM-335 or IMM-332]"},
	{0x41, 0x32, 324, "VERLOG code missing"},
	{0x41, 0x33, 325, "Comm fault with coded starter module (CSM). [might be either IMM-325 or IMM-323]"},
	{0x41, 0x34, 121, "PIN programming failed"},
	{0x41, 0x35, 225, "Key Code verification failed"},
	{0x41, 0x36, 132, "No Key Codes in EEPROM"},
	{0x41, 0x37, 122, "New replacement Immobilizer not yet programmed"},
	{0x41, 0x38, 999, "?EEPROM error?"},
	{0x41, 0x51, 311, "Comms with engine ECU, short to supply in comm link 2 or 4"},
	{0x41, 0x52, 312, "Comms with engine ECU, short to ground in comm link 2 or 4"},
	{0x41, 0x53, 214, "Comms from engine ECU, wrong MIN code (ie, Immobilizer is programmed for a different engine ECU)"},
	{0x41, 0x54, 321, "Initiating signal from ECM, missing"},
	{0x41, 0x55, 326, "Reply signal from engine control module"},
	{0x41, 0xF7, 333, "Control circuit to VGLA, fault in VGLA or LED circuit"},
	{0x41, 0xF8, 336, "Control circuit to VGLA, fault in VGLA or LED circuit (P600)"},
	//-------------------------------------------------------------------------------
	//  Define all known, interpreted ECU 29 (ECC) [for '98 S70/V70/XC70 only] DTCs.
	//  * There are many more ECC DTCs whose meaning is not yet known.
	//-------------------------------------------------------------------------------
	// ECU 29 has 4 DTCs: 35 36 37 38
	{0x29, 0x35, 412, "Driver's (or passenger's) side interior temperature sensor inlet fan shorted to earth (or signal too low) [see \"ac heater system auto.pdf\"]"},
	{0x29, 0x36, 413, "Driver's (or passenger's) side interior temperature sensor inlet fan, no control voltage (or signal too high) [see \"ac heater system auto.pdf\"]"},
	{0x29, 0x37, 414, "Driver's (or passenger's) side interior temperature sensor inlet fan seized (or open/short circuit or faulty signal) [carefully clean out any lint from temp sensor fan(s)]"},
	{0x29, 0x38, 411, "Blower fan seized or drawing excess current [or blower fan obstruction, or power stage surge protector problem (ECC-419)]"},
	//-----------------------------------------------------------
	//  Define all known, interpreted ECU 2D (VGLA) DTCs.
	//  * These are not yet known.
	//-----------------------------------------------------------
	// ECU 2D has 0 DTCs.
    ////{0x2D, 0x??, 312, "Siren, Internal battery fault"},
    ////{0x2D, 0x??, 441, "Resistive wire, Signal too high"},
	//-------------------------------------------------------------------------------------------
	//  Define all known, interpreted ECU 18 (Add Heater 912-D [diesel cold weather heater]) DTCs.
	//  * These are not yet known.
	//-------------------------------------------------------------------------------------------
	// ECU 18 has 0 DTCs.
    ////{0x18, 0x??, ???, "??????"},
	{0, 0, 0, NULL}
};


static bool have_read_dtcs = false;
static struct diag_msg *ecu_id = NULL;

static bool live_display_running = false;
static int live_data_lines;

static enum cli_retval cmd_850_help(int argc, char **argv);
static enum cli_retval cmd_850_connect(int argc, char **argv);
static enum cli_retval cmd_850_disconnect(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_ping(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_sendreq(int argc, char **argv);
static enum cli_retval cmd_850_peek(int argc, char **argv);
static enum cli_retval cmd_850_dumpram(int argc, char **argv);
static enum cli_retval cmd_850_read(int argc, char **argv);
static enum cli_retval cmd_850_readnv(int argc, char **argv);
static enum cli_retval cmd_850_adc(int argc, char **argv);
static enum cli_retval cmd_850_id(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_dtc(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_cleardtc(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_freeze(int argc, char **argv);
static enum cli_retval cmd_850_resetsrl(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_scan_all(int argc, UNUSED(char **argv));
static enum cli_retval cmd_850_test(int argc, char **argv);

const struct cmd_tbl_entry v850_cmd_table[] = {
	{ "help", "help [command]", "Gives help for a command",
	  cmd_850_help, 0, NULL},
	{ "?", "? [command]", "Gives help for a command",
	  cmd_850_help, 0, NULL},

	{ "connect", "connect <ecuname>", "Connect to ECU. Use '850 connect ?' to show ECU names.",
	  cmd_850_connect, 0, NULL},
	{ "disconnect", "disconnect", "Disconnect from ECU",
	  cmd_850_disconnect, 0, NULL},
	{ "scan-all", "scan-all", "Try connecting to all possible ECUs, print identification and DTCs",
	  cmd_850_scan_all, 0, NULL},
	{ "sendreq", "sendreq <byte0 [byte1 ...]>", "Send raw data to the ECU and print response",
	  cmd_850_sendreq, 0, NULL},
	{ "ping", "ping", "Verify communication with the ECU", cmd_850_ping,
	  0, NULL},
	{ "peek", "peek <addr1>[w|l][.addr2] [addr2 ...] [live|stream]", "Display contents of RAM, once or continuously",
	  cmd_850_peek, 0, NULL},
	{ "dumpram", "dumpram <filename> [fast]", "Dump entire RAM contents to file (Warning: takes 20+ minutes)",
	  cmd_850_dumpram, 0, NULL},
	{ "read", "read <id1>|*<addr1> [id2 ...] [live|stream]", "Display live data, once or continuously",
	  cmd_850_read, 0, NULL},
	{ "adc", "adc id1 [id2 ...]", "Display ADC readings, once or continuously",
	  cmd_850_adc, 0, NULL},
	{ "readnv", "readnv id1 [id2 ...]", "Display non-volatile data",
	  cmd_850_readnv, 0, NULL},
	{ "id", "id", "Display ECU identification",
	  cmd_850_id, 0, NULL},
	{ "dtc", "dtc", "Retrieve DTCs",
	  cmd_850_dtc, 0, NULL},
	{ "cleardtc", "cleardtc", "Clear DTCs from ECU",
	  cmd_850_cleardtc, 0, NULL},
	{ "freeze", "freeze dtc1|all [dtc2 ...]", "Display freeze frame(s)",
	  cmd_850_freeze, 0, NULL},
	{ "resetsrl", "resetsrl", "Reset the Service Reminder Light",
	  cmd_850_resetsrl, 0, NULL},
	{ "test", "test <testname>", "Test vehicle components",
	  cmd_850_test, 0, NULL},

	CLI_TBL_BUILTINS,
	CLI_TBL_END
};

static enum cli_retval cmd_850_help(int argc, char **argv) {
	return cli_help_basic(argc, argv, v850_cmd_table);
}

/*
 * Wrapper around printf. When live data display is running, increments the 
 * line count and clears old text remaining on the line we just printed.
 * Appends a newline to the output.
 */
static int printf_livedata(const char *format, ...) {
	va_list ap;
	int rv;

	va_start(ap, format);
	rv = vprintf(format, ap);
	va_end(ap);

	if (live_display_running) {
		live_data_lines++;
		diag_os_clrtoeol();
	}

	putchar('\n');
	return rv;
}

/*
 * Capitalize the first letter of the supplied string.
 * Returns a static buffer that will be reused on the next call.
 */
static char *capitalize(const char *in) {
	static char buf[80];

	strncpy(buf, in, sizeof(buf));
	buf[sizeof(buf)-1] = '\0';

	if (isalpha(buf[0]) && islower(buf[0])) {
		buf[0] = toupper(buf[0]);
	}
	return buf;
}

/*
 * Look up an ECU by name.
 */
static struct ecu_info *ecu_info_by_name(const char *name) {
	struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (strcasecmp(name, ecu->name) == 0) {
			return ecu;
		}
	}

	return NULL;
}

/*
 * Get an ECU's address by name.
 */
static int ecu_addr_by_name(const char *name) {
	struct ecu_info *ecu;
	unsigned long int i;
	char *p;

	if (isdigit(name[0])) {
		i = strtoul(name, &p, 0);
		if (*p != '\0') {
			return -1;
		}
		if (i > 0x7f) {
			return -1;
		}
		return i;
	}

	ecu = ecu_info_by_name(name);
	if (ecu == NULL) {
		return -1;
	}
	return ecu->addr;
}

/*
 * Get an ECU's description by address.
 */
static char *ecu_desc_by_addr(uint8_t addr) {
	struct ecu_info *ecu;
	static char buf[7];

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (addr == ecu->addr) {
			return ecu->desc;
		}
	}

	sprintf(buf, "ECU %02X", addr);
	return buf;
}

/*
 * Get the description of the currently connected ECU.
 */
static char *current_ecu_desc(void) {
	uint8_t addr;

	if (global_state < STATE_CONNECTED) {
		return "???";
	}

	addr = global_l2_conn->diag_l2_destaddr;

	if (addr > 0x7f) {
		return "???";
	}

	return ecu_desc_by_addr(addr);
}

/*
 * Get the printable designation (EFI-xxx, AT-xxx, etc) for a DTC by its raw
 * byte value. Optionally, also get a description of the DTC.
 * Returns a static buffer that will be reused on the next call.
 */
static char *dtc_printable_by_raw(uint8_t addr, uint8_t raw, char **desc) {
#define PRINTABLE_LEN   8       //including 0-termination
	static char printable[PRINTABLE_LEN];
	static char *empty="";
	struct ecu_info *ecu_entry;
	struct dtc_table_entry *dtc_entry;
	char *prefix;
	uint16_t suffix;

	prefix = "???";
	for (ecu_entry = ecu_list; ecu_entry->name != NULL; ecu_entry++) {
		if (addr == ecu_entry->addr) {
			prefix = ecu_entry->dtc_prefix;
			break;
		}
	}

	for (dtc_entry = dtc_table; dtc_entry->dtc_suffix != 0; dtc_entry++) {
		if (dtc_entry->ecu_addr == addr && dtc_entry->raw_value == raw) {
			suffix = dtc_entry->dtc_suffix;
			if (desc != NULL) {
				*desc = dtc_entry->desc;
			}
			if (suffix > 999) {
				suffix = 999;
			}
			snprintf(printable, PRINTABLE_LEN, "%s-%03d", prefix, suffix);
			return printable;
		}
	}

	if (desc != NULL) {
		*desc = empty;
	}
	snprintf(printable, PRINTABLE_LEN, "%s-???", prefix);
	return printable;
}

/*
 * Get the DTC prefix for the currently connected ECU.
 */
static char *current_dtc_prefix(void) {
	struct ecu_info *ecu;

	if (global_state < STATE_CONNECTED) {
		return "???";
	}

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		if (global_l2_conn->diag_l2_destaddr == ecu->addr) {
			return ecu->dtc_prefix;
		}
	}

	return "???";
}

/*
 * Get a DTC byte value by the printable designation. Returns 0xffff on
 * failure.
 */
static uint16_t dtc_raw_by_printable(char *printable) {
	char prefix[8];
	uint16_t suffix;
	char *p, *q, *r;
	struct dtc_table_entry *dtc_entry;
	uint8_t ecu_addr;

	/* extract prefix and suffix from string */
	if (strlen(printable) > sizeof(prefix) - 1) {
		return 0xffff; /* implausably long string */
	}
	strcpy(prefix, printable);
	p = prefix;
	while (isalpha(*p)) {
		p++;
	}
	q = p;
	if (*q == '-') {
		q++;
	}
	suffix = strtoul(q, &r, 10);
	if (*q == '\0' || *r != '\0') {
		return 0xffff; /* no valid numeric suffix */
	}
	*p = '\0';

	/* check prefix */
	if (strcasecmp(prefix, current_dtc_prefix()) != 0) {
		return 0xffff; /* doesn't match connected ecu prefix */
	}

	/* find suffix */
	ecu_addr = global_l2_conn->diag_l2_destaddr;
	for (dtc_entry = dtc_table; dtc_entry->dtc_suffix != 0; dtc_entry++) {
		if (dtc_entry->ecu_addr == ecu_addr &&
		    dtc_entry->dtc_suffix == suffix) {
			return dtc_entry->raw_value;
		}
	}
	return 0xffff; /* suffix not found */
}

/*
 * Print a list of known ECUs. Not all ECUs in this list are necessarily
 * present in the vehicle.
 */
static void print_ecu_list(void) {
	struct ecu_info *ecu;

	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		printf(" %s\t%s\n", ecu->name, capitalize(ecu->desc));
	}
}

enum connection_status {
	NOT_CONNECTED,          /* Not connected */
	CONNECTED_D2,           /* Connected with D2 over K-line */
	CONNECTED_KWP71,        /* Connected with KWP71 */
	CONNECTED_EITHER,       /* Connected with either D2 or KWP71 */
	CONNECTED_OTHER         /* Connected with non-Volvo protocol */
};

/*
 * Indicates whether we're currently connected.
 */
static enum connection_status get_connection_status(void) {
	if (global_state < STATE_CONNECTED) {
		return NOT_CONNECTED;
	}
	if (global_l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_D2) {
		return CONNECTED_D2;
	}
	if (global_l2_conn->l2proto->diag_l2_protocol == DIAG_L2_PROT_VAG) {
		return CONNECTED_KWP71;
	}
	return CONNECTED_OTHER;
}

/*
 * Check whether the number of arguments to a command is between the specified
 * minimum and maximum. If not, print a message and return false.
 */
static bool valid_arg_count(int min, int argc, int max) {
	if (argc < min) {
		printf("Too few arguments\n");
		return false;
	}

	if (argc > max) {
		printf("Too many arguments\n");
		return false;
	}

	return true;
}

/*
 * Check whether the connection status matches the required connection status
 * for this command. If not, print a message and return false.
 */
static bool valid_connection_status(unsigned int want) {
	if (want == CONNECTED_EITHER) {
		if (get_connection_status() == CONNECTED_D2 ||
		    get_connection_status() == CONNECTED_KWP71) {
			return true;
		}
	} else if (get_connection_status() == want) {
		return true;
	}

	switch (get_connection_status()) {
	case NOT_CONNECTED:
		printf("Not connected.\n");
		return false;
	case CONNECTED_OTHER:
		if (want == NOT_CONNECTED) {
			printf("Already connected with non-Volvo protocol. Please use 'diag disconnect'.\n");
		} else {
			printf("Connected with non-Volvo protocol.\n");
		}
		return false;
	case CONNECTED_D2:
	case CONNECTED_KWP71:
		if (want == NOT_CONNECTED) {
			printf("Already connected to %s. Please disconnect first.\n", current_ecu_desc());
		} else {
			printf("This function is not available with this protocol.\n");
		}
		return false;
	default:
		printf("Unexpected connection state!\n");
		return false;
	}
}

/*
 * Send 3 pings with a delay between them to get the ELM used to the ECU's
 * response time.
 */
static void adaptive_timing_workaround(void) {
	int i;

	for (i=0; i<3; i++) {
		(void)diag_l7_d2_ping(global_l2_conn);
		diag_os_millisleep(200);
	}
}

/*
 * Callback to store the ID block upon establishing a KWP71 connection
 */
static void ecu_id_callback(void *handle, struct diag_msg *in) {
	struct diag_msg **out = (struct diag_msg **)handle;
	*out = diag_dupmsg(in);
}

/*
 * Connect to an ECU by name or address.
 */
static enum cli_retval cmd_850_connect(int argc, char **argv) {
	int addr;
	int rv;
	struct diag_l0_device *dl0d;
	struct diag_l2_data l2data;

	if (!valid_arg_count(2, argc, 2)) {
		return CMD_USAGE;
	}

	if (strcmp(argv[1], "?") == 0) {
		printf("Known ECUs are:\n");
		print_ecu_list();
		printf("Can also specify target by numeric address.\n");
		return CMD_USAGE;
	}

	if (!valid_connection_status(NOT_CONNECTED)) {
		return CMD_OK;
	}

	addr = ecu_addr_by_name(argv[1]);
	if (addr < 0) {
		printf("Unknown ECU '%s'\n", argv[1]);
		return CMD_OK;
	}

	dl0d = global_dl0d;

	if (dl0d == NULL) {
		printf("No global L0. Please select + configure L0 first\n");
		return diag_iseterr(DIAG_ERR_GENERAL);
	}

	if (addr == 0x10) {
		global_cfg.speed = 9600;
		global_cfg.tgt = addr;
		global_cfg.L1proto = DIAG_L1_ISO9141;
		global_cfg.L2proto = DIAG_L2_PROT_VAG;
		global_cfg.initmode = DIAG_L2_TYPE_SLOWINIT;
	} else {
		global_cfg.speed = 10400;
		global_cfg.src = 0x13;
		global_cfg.tgt = addr;
		global_cfg.L1proto = DIAG_L1_ISO9141;
		global_cfg.L2proto = DIAG_L2_PROT_D2;
		global_cfg.initmode = DIAG_L2_TYPE_SLOWINIT;
	}

	rv = diag_l2_open(dl0d, global_cfg.L1proto);
	if (rv) {
		fprintf(stderr, "cmd_850_connect: diag_l2_open failed\n");
		return diag_ifwderr(rv);
	}

	global_l2_conn = diag_l2_StartCommunications(dl0d, global_cfg.L2proto,
	                                             global_cfg.initmode & DIAG_L2_TYPE_INITMASK, global_cfg.speed,
	                                             global_cfg.tgt, global_cfg.src);
	if (global_l2_conn == NULL) {
		rv = diag_geterr();
		diag_l2_close(dl0d);
		return diag_iseterr(rv);
	}

	if (global_cfg.L2proto == DIAG_L2_PROT_VAG) {
		(void)diag_l2_ioctl(global_l2_conn, DIAG_IOCTL_GET_L2_DATA, (void *)&l2data);
		if (l2data.kb1!=0xab || l2data.kb2!=0x02) {
			fprintf(stderr, FLFMT "_connect : wrong keybytes %02X%02X, expecting AB02\n", FL, l2data.kb1, l2data.kb2);
			diag_l2_StopCommunications(global_l2_conn);
			diag_l2_close(dl0d);
			global_l2_conn = NULL;
			global_state = STATE_IDLE;
			return diag_iseterr(DIAG_ERR_WRONGKB);
		}
	}

	global_state = STATE_CONNECTED;
	printf("Connected to %s.\n", ecu_desc_by_addr(addr));
	have_read_dtcs = false;

	if (get_connection_status() == CONNECTED_D2) {
		adaptive_timing_workaround();
	} else {
		printf("Warning: KWP71 communication is not entirely reliable yet.\n");
		/*
		 * M4.4 doesn't accept ReadECUIdentification request, so save
		 * the identification block it sends at initial connection.
		 */
		if (ecu_id != NULL) {
			diag_freemsg(ecu_id);
		}
		ecu_id = NULL;
		rv = diag_l2_recv(global_l2_conn, 300, ecu_id_callback, &ecu_id);
		if (rv < 0) {
			return diag_ifwderr(rv);
		}
		if (ecu_id == NULL) {
			return diag_iseterr(DIAG_ERR_NOMEM);
		}
	}

	return CMD_OK;
}

/*
 * Close the current connection.
 */
static enum cli_retval cmd_850_disconnect(int argc, UNUSED(char **argv)) {
	char *desc;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	desc = current_ecu_desc();

	diag_l2_StopCommunications(global_l2_conn);
	diag_l2_close(global_dl0d);

	global_l2_conn = NULL;
	global_state = STATE_IDLE;

	printf("Disconnected from %s.\n", desc);
	have_read_dtcs = false;
	return CMD_OK;
}

/*
 * Send a raw command and print the response.
 */
static enum cli_retval cmd_850_sendreq(int argc, char **argv) {
	uint8_t data[MAXRBUF] = {0};
	unsigned int len;
	unsigned int i;
	int rv;

	if (!valid_arg_count(2, argc, sizeof(data) + 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	len = argc - 1;
	for (i = 0; i < len; i++) {
		data[i] = (uint8_t) htoi(argv[i+1]);
	}

	rv = l2_do_send( global_l2_conn, data, len,
	                 (void *)&_RQST_HANDLE_DECODE);

	if (rv == DIAG_ERR_TIMEOUT) {
		printf("No data received\n");
	} else if (rv != 0) {
		printf("sendreq: failed error %d\n", rv);
	}

	return CMD_OK;
}

/*
 * Verify communication with the ECU.
 */
static enum cli_retval cmd_850_ping(int argc, UNUSED(char **argv)) {
	int rv;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_ping(global_l2_conn);
	} else {
		rv = diag_l7_kwp71_ping(global_l2_conn);
	}

	if (rv == 0) {
		printf("Pong!\n");
	} else {
		printf("Ping failed.\n");
	}

	return CMD_OK;
}

#define CLAMPED_LOOKUP(table,index) table[MIN(ARRAY_SIZE(table)-1,(unsigned)(index))]

/*
 * If we know how to interpret a live data value, print out the description and
 * scaled value.
 */
static void interpret_value(enum l7_namespace ns, uint16_t addr, UNUSED(int len), uint8_t *buf) {
	static const char *mode_selector_positions[]={"Open","S","E","W","Unknown"};
	static const char *driving_modes[]={"Economy","Sport","Winter","Unknown"};
	static const char *warmup_states[]={"in progress or engine off","completed","not possible","status unknown"};
	float volts;
	int16_t deg_c;
	uint8_t ecu = global_l2_conn->diag_l2_destaddr;

	if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0200) {
		printf_livedata("Engine Coolant Temperature: %dC (%dF)", buf[1]-80, (buf[1]-80)*9/5+32);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0300) {
		/*ECU pin A27, MCU P7.1 input, divider ratio 8250/29750, 5Vref*/
		printf_livedata("Battery voltage: %.1f V", (float)buf[0]*29750/8250*5/255);
	} else if (ns==NS_MEMORY && ecu==0x10 && addr==0x36) {
		printf_livedata("Battery voltage: %.1f V", (float)buf[0]*29750/8250*5/255);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x0A00) {
		printf_livedata("Warm-up %s", CLAMPED_LOOKUP(warmup_states, (buf[0]>>2)&3));
		printf_livedata("MIL %srequested by TCM", (buf[0]&0x10)?"":"not ");
		/* Low 2 bits supposedly indicate drive cycle and trip 
		   complete, but don't make sense - can get set without the 
		   car ever moving */
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1000) {
		/* ECU pin A4, MCU P7.4 input, divider ratio 8250/9460 */
		printf_livedata("MAF sensor signal: %.2f V", (float)buf[0]*9460/8250*5/255);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1800) {
		printf_livedata("Short term fuel trim: %+.1f%%", (float)buf[0]*100/128-100);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1900) {
		/* possibly in units of 0.004 milliseconds (injection time) */
		printf_livedata("Long term fuel trim, additive (unscaled): %+d", (signed int)buf[0]-128);
	} else if (ns==NS_LIVEDATA && ecu==0x7a && addr==0x1A00) {
		printf_livedata("Long term fuel trim, multiplicative: %+.1f%%", (float)buf[0]*100/128-100);
	} else if (ns==NS_LIVEDATA && ecu==0x6e && addr==0x0500) {
		printf_livedata("Mode selector: MS1 %s, MS2 %s, switch position %s", (buf[0]&1)?"low":"high", (buf[0]&2)?"low":"high", CLAMPED_LOOKUP(mode_selector_positions, buf[0]));
		printf_livedata("Driving mode: %s", CLAMPED_LOOKUP(driving_modes, buf[1]));
	} else if (ns==NS_LIVEDATA && ecu==0x6e && addr==0x0C00) {
		/* Full scale should be 1023, although highest value seen in
		   bench testing was 1020 */
		volts = ((float)buf[0]*256+buf[1])*5/1023;
		printf_livedata("ATF temperature sensor voltage: %.2f V", volts);
		/* Avoid divide by zero below */
		if (5.0f-volts == 0.0f) {
			volts = 4.999;
		}
		/* Input has 1k to +5V, sensor acts as a potential divider */
		printf_livedata("ATF temperature sensor resistance: %u ohms", (unsigned)((1000.0f*volts)/(5.0f-volts)));
		/* Offs 11 (!) agrees with T vs R chart in Volvo Green Book */
		deg_c = ((int16_t)buf[2]*256)+buf[3]-11;
		printf_livedata("ATF temperature: %dC (%dF)", deg_c, deg_c*9/5+32);
	}
}

/*
 * Try to interpret all the live data values in the buffer.
 */
static void interpret_block(enum l7_namespace ns, uint16_t addr, int len, uint8_t *buf) {
	int i;

	if (ns != NS_MEMORY) {
		addr <<= 8;
	}

	for (i=0; i<len; i++) {
		interpret_value(ns, addr+i, len-i, buf+i);
	}
}

/*
 * Print one line of a hex dump, with an address followed by one or more
 * values.
 */
static int print_hexdump_line(FILE *f, uint16_t addr, int addr_chars, uint8_t *buf, uint16_t len) {
	if (fprintf(f, "%0*X:", addr_chars, addr) < 0) {
		return 1;
	}
	while (len--) {
		if (fprintf(f, " %02X", *buf++) < 0) {
			return 1;
		}
	}
	if (live_display_running) {
		diag_os_clrtoeol();
	}
	if (fputc('\n', f) == EOF) {
		return 1;
	}
	return 0;
}

struct read_or_peek_item {
	uint16_t start;         /* starting address or identifier */
	uint16_t end;           /* ending address - for peeks only */
	enum l7_namespace ns;
};

/*
 * Parse an address argument on a peek command line.
 */
static int parse_peek_arg(char *arg, struct read_or_peek_item *item) {
	char *p, *q;

	item->ns = NS_MEMORY;
	item->start = strtoul(arg, &p, 0);
	if (*p == '\0') {
		item->end = item->start;
	} else if ((p[0] == 'w' || p[0] == 'W') && p[1] == '\0') {
		item->end = item->start + 1;
	} else if ((p[0] == 'l' || p[0] == 'L') && p[1] == '\0') {
		item->end = item->start + 3;
	} else if ((p[0] == '.' || p[0] == '-') && p[1] != '\0') {
		item->end = strtoul(p+1, &q, 0);
		if (*q != '\0' || item->end < item->start) {
			printf("Invalid address range '%s'\n", arg);
			return 1;
		}
	} else {
		printf("Invalid address '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on a read command line.
 */
static int parse_read_arg(char *arg, struct read_or_peek_item *item) {
	char *p;

	if (arg[0] == '*') {
		if (arg[1] == '\0') {
			printf("Invalid identifier '%s'\n", arg);
			return 1;
		}
		return parse_peek_arg(arg+1, item);
	}

	item->ns = NS_LIVEDATA;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on an adc command line.
 */
static int parse_adc_arg(char *arg, struct read_or_peek_item *item) {
	char *p;

	item->ns = NS_ADC;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on a readnv command line.
 */
static int parse_readnv_arg(char *arg, struct read_or_peek_item *item) {
	char *p;

	item->ns = NS_NV;
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		return 1;
	}
	return 0;
}

/*
 * Parse an identifier argument on a freeze command line.
 */
static int parse_freeze_arg(char *arg, struct read_or_peek_item *item) {
	char *p;

	item->ns = NS_FREEZE;
	if (isalpha(arg[0])) {
		item->start = dtc_raw_by_printable(arg);
		if (item->start == 0xffff) {
			printf("Invalid identifier '%s'\n", arg);
			return 1;
		}
		return 0;
	}
	item->start = strtoul(arg, &p, 0);
	if (*p != '\0' || item->start > 0xff) {
		printf("Invalid identifier '%s'\n", arg);
		if (isdigit(arg[0]) && arg[0] != '0' && *p == '\0') {
			printf("Did you mean %s-%s?\n",
			       current_dtc_prefix(), arg);
		}
		return 1;
	}
	if (isdigit(arg[0]) && arg[0]!='0') {
		if (item->start < 100) {
			printf("Warning: retrieving freeze frame by raw identifier %d (=%02X).\nDid you mean 0x%s?\n", item->start, item->start, arg);
		} else {
			printf("Warning: retrieving freeze frame by raw identifier %d (=%02X).\nDid you mean %s-%s?\n", item->start, item->start, current_dtc_prefix(), arg);
		}
	}
	return 0;
}

/*
 * Execute a read, peek or readnv command.
 */
static enum cli_retval read_family(int argc, char **argv, enum l7_namespace ns) {
	int count;
	int i, rv;
	bool continuous;
	struct read_or_peek_item *items;
	uint8_t buf[20];
	uint16_t addr, len;
	int gotbytes;

	if (!valid_arg_count(2, argc, 999)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	continuous = false;
	count = argc - 1;

	if (ns!=NS_NV && ns!=NS_FREEZE && strcasecmp(argv[argc-1], "stream")==0) {
		continuous = true;
		count--;
		if (count < 1) {
			return CMD_USAGE;
		}
	}

	if (ns!=NS_NV && ns!=NS_FREEZE && strcasecmp(argv[argc-1], "live")==0) {
		if (continuous) {
			return CMD_USAGE;
		}
		continuous = true;
		live_display_running = true;
		count--;
		if (count < 1) {
			return CMD_USAGE;
		}
	}

	rv = diag_calloc(&items, count);
	if (rv) {
		live_display_running = false;
		return diag_ifwderr(rv);
	}

	for (i=0; i<count; i++) {
		switch (ns) {
		case NS_MEMORY:
			if (parse_peek_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_LIVEDATA:
			if (parse_read_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_ADC:
			if (parse_adc_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_NV:
			if (parse_readnv_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		case NS_FREEZE:
			if (parse_freeze_arg(argv[i + 1], &(items[i])) != 0) {
				goto done;
			}
			break;
		default:
			fprintf(stderr, FLFMT "impossible ns value\n", FL);
			goto done;
		}
	}

	diag_os_ipending();
	while (1) {
		live_data_lines = 0;
		for (i=0; i<count; i++) {
			if (items[i].ns != NS_MEMORY) {
				addr = items[i].start;
				if (get_connection_status() == CONNECTED_D2) {
					gotbytes = diag_l7_d2_read(global_l2_conn, items[i].ns, addr, sizeof(buf), buf);
				} else {
					gotbytes = diag_l7_kwp71_read(global_l2_conn, items[i].ns, addr, sizeof(buf), buf);
				}
				if (gotbytes < 0) {
					printf("Error reading %02X\n", addr);
					goto done;
				}
				if (items[i].ns == NS_FREEZE) {
					printf("%s  ",
					       dtc_printable_by_raw(
						       global_l2_conn
						       ->diag_l2_destaddr,
						       addr, NULL));
				}
				if (gotbytes == 0) {
					printf_livedata("%02X: no data", addr);
				} else if ((unsigned int)gotbytes > sizeof(buf)) {
					print_hexdump_line(stdout, addr, 2, buf, sizeof(buf));
					live_data_lines++;
					printf_livedata(" (%d bytes received, only first %zu shown)", gotbytes, sizeof(buf));
					interpret_block(items[i].ns, addr, sizeof(buf), buf);
				} else {
					print_hexdump_line(stdout, addr, 2, buf, gotbytes);
					live_data_lines++;
					interpret_block(items[i].ns, addr, gotbytes, buf);
				}
			} else {
				addr = items[i].start;
				len = (items[i].end - items[i].start) + 1;
				while (len > 0) {
					if (get_connection_status() == CONNECTED_D2) {
						gotbytes = diag_l7_d2_read(global_l2_conn, NS_MEMORY, addr, (len<8)?len:8, buf);
					} else {
						gotbytes = diag_l7_kwp71_read(global_l2_conn, NS_MEMORY, addr, (len<8)?len:8, buf);
					}
					if (gotbytes == ((len<8)?len:8)) {
						print_hexdump_line(stdout, addr, 4, buf, (len<8)?len:8);
						live_data_lines++;
						interpret_block(NS_MEMORY, addr, (len<8)?len:8, buf);
					} else {
						printf("Error reading %s%04X\n", (ns==NS_LIVEDATA)?"*":"", addr);
						goto done;
					}
					len -= (len<8)?len:8;
					addr += 8;
				}
			}
		}
		if (!continuous || diag_os_ipending()) {
			break;
		}
		if (live_display_running) {
			diag_os_cursor_up(live_data_lines);
		}
	}

done:
	live_display_running = false;
	free(items);
	return CMD_OK;
}

/*
 * Read and display one or more values from RAM.
 *
 * Takes a list of addresses to read. Each address can have a suffix "w" or
 * "l" to indicate 2 or 4 bytes, respectively; otherwise a single byte is
 * read. Each item in the list can also be an address range with the starting
 * and ending addresses separated by ".".
 *
 * The word "live" can be added at the end to continuously read the 
 * requested addresses and update the display until interrupted, or "stream" 
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_peek(int argc, char **argv) {
	return read_family(argc, argv, NS_MEMORY);
}

/*
 * Read and display one or more live data parameters.
 *
 * Takes a list of one-byte identifier values. If a value is prefixed with *,
 * it is treated as an address or address range to read from RAM instead of
 * a live data parameter identifier; in this way, a list of "read" and "peek"
 * operations can be done in a single command.
 *
 * The word "live" can be added at the end to continuously read the 
 * requested addresses and update the display until interrupted, or "stream" 
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_read(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_LIVEDATA);
}

/*
 * Read and display one or more ADC readings.
 *
 * Takes a list of one-byte channel identifiers.
 *
 * The word "live" can be added at the end to continuously read the 
 * requested addresses and update the display until interrupted, or "stream" 
 * to continuously read and scroll the display.
 */
static enum cli_retval cmd_850_adc(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_KWP71)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_ADC);
}


/*
 * Read and display one or more non-volatile parameters.
 *
 * Takes a list of one-byte identifier values.
 */
static enum cli_retval cmd_850_readnv(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	return read_family(argc, argv, NS_NV);
}

/*
 * Read and display freeze frames for all stored DTCs.
 */
static enum cli_retval cmd_850_freeze_all(void) {
	uint8_t dtcs[12];
	int count;
	char *argbuf;
	char **argvout;
	char *p;
	int rv;
	int i;

	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	rv = diag_l7_d2_dtclist(global_l2_conn, sizeof(dtcs), dtcs);
	if (rv < 0) {
		printf("Couldn't retrieve DTCs.\n");
		return CMD_OK;
	}

	if (rv == 0) {
		printf("No stored DTCs.\n");
		return CMD_OK;
	}

	count = rv;

	rv = diag_calloc(&argbuf, 5);
	if (rv) {
		return diag_ifwderr(rv);
	}

	rv = diag_calloc(&argvout, count+1);
	if (rv) {
		return diag_ifwderr(rv);
	}

	p = argbuf;
	for (i=0; i<count; i++) {
		sprintf(p, "0x%x", dtcs[i]);
		argvout[i+1] = p;
		p += 5;
	}

	rv = read_family(count+1, argvout, NS_FREEZE);

	free(argvout);
	free(argbuf);
	return rv;
}

/*
 * Read and display one or more freeze frames.
 *
 * Takes a list of DTCs, or the option "all" to retrieve freeze frames for all
 * stored DTCs. Each DTC can be specified either as a raw byte value or by its
 * EFI-xxx, AT-xxx, etc designation.
 */
static enum cli_retval cmd_850_freeze(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	if (argc==2 && strcasecmp(argv[1], "all")==0) {
		return cmd_850_freeze_all();
	}
	return read_family(argc, argv, NS_FREEZE);
}

/*
 * Query the ECU for identification and print the result.
 */
static enum cli_retval cmd_850_id_d2(void) {
	uint8_t buf[15];
	int rv;
	int i;

	rv = diag_l7_d2_read(global_l2_conn, NS_NV, 0xf0, sizeof(buf), buf);
	if (rv < 0) {
		printf("Couldn't read identification.\n");
		return CMD_OK;
	}
	if (rv != sizeof(buf)) {
		printf("Identification response was %d bytes, expected %zu\n", rv, sizeof(buf));
		return CMD_OK;
	}
	if (buf[0] != 0) {
		printf("First identification response byte was %02X, expected 0\n", buf[0]);
		return CMD_OK;
	}
	if (!isprint(buf[5]) || !isprint(buf[6]) || !isprint(buf[7]) ||
	    !isprint(buf[12]) || !isprint(buf[13]) || !isprint(buf[14])) {
		printf("Unexpected characters in version response\n");
		return CMD_OK;
	}

	printf("Hardware ID: P%02X%02X%02X%02X revision %.3s\n", buf[1], buf[2], buf[3], buf[4], buf+5);
	printf("Software ID:  %02X%02X%02X%02X revision %.3s\n", buf[8], buf[9], buf[10], buf[11], buf+12);

	if (global_l2_conn->diag_l2_destaddr == 0x7a) {
		rv = diag_l7_d2_read(global_l2_conn, NS_NV, 1, sizeof(buf), buf);
		if (rv < 0) {
			return CMD_OK;
		}
		if (rv != 10) {
			printf("Identification response was %d bytes, expected %d\n", rv, 10);
			return CMD_OK;
		}
		for (i=0; i<10; i++) {
			if (!isdigit(buf[i])) {
				printf("Unexpected characters in identification block\n");
				return CMD_OK;
			}
		}
		printf("Order number: %c %.3s %.3s %.3s\n",buf[0], buf+1, buf+4, buf+7);
	}

	return CMD_OK;
}

/*
 * Print the ECU identification we received upon initial connection.
 */
static enum cli_retval cmd_850_id_kwp71(void) {
	int i;
	struct diag_msg *msg;

	if (ecu_id == NULL) {
		printf("No stored ECU identification!\n");
		return CMD_OK;
	}

	msg = ecu_id;

	if (msg->len != 10) {
		printf("Identification block was %u bytes, expected %d\n", msg->len, 10);
		return CMD_OK;
	}

	for (i=0; i<10; i++) {
		if (!isdigit(msg->data[i])) {
			printf("Unexpected characters in identification block\n");
			return CMD_OK;
		}
	}

	printf("Order number: %c %.3s %.3s %.3s\n",msg->data[0], msg->data+1, msg->data+4, msg->data+7);

	msg = msg->next;
	if (msg == NULL) {
		return CMD_OK;
	}
	/* Second block seems to be meaningless, don't print it. */
#if 0
	print_hexdump_line(stdout, msg->type, 2, msg->data, msg->len);
#endif
	msg = msg->next;
	if (msg == NULL) {
		return CMD_OK;
	}

	if (msg->len != 10) {
		printf("Identification block was %u bytes, expected %d\n", msg->len, 10);
		return CMD_OK;
	}

	for (i=0; i<7; i++) {
		if (!isdigit(msg->data[i])) {
			printf("Unexpected characters in identification block\n");
			return CMD_OK;
		}
	}
	printf("Hardware ID: P0%.7s\n", msg->data);

	/* There's a fourth block but it seems to be meaningless. */

	return CMD_OK;
}

/*
 * Display ECU identification.
 */
static enum cli_retval cmd_850_id(int argc, UNUSED(char **argv)) {
	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		return cmd_850_id_d2();
	}
	return cmd_850_id_kwp71();
}

/*
 * Dump the entire contents of RAM to the specified file as a hex dump with
 * 8 bytes per line.
 *
 * ECUs may have holes in the memory map (example: Motronic M4.4 has RAM
 * at 0000-00FF and XRAM at F800-FFFF and nothing in between). So we try
 * reading in 8 byte chunks and if an attempt to read a given address fails,
 * just skip the hexdump line for that address and continue on to the next one.
 * If the "fast" option is specified on the command line, skip ahead to 0xF000
 * when a read attempt fails.
 */
static enum cli_retval cmd_850_dumpram(int argc, char **argv) {
	uint16_t addr;
	FILE *f;
	bool happy;
	uint8_t buf[8];
	bool fast;

	if (argc == 2) {
		fast = false;
	} else if (argc == 3 && strcasecmp(argv[2], "fast") == 0) {
		fast = true;
	} else {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	f = fopen(argv[1], "w");
	if (f == NULL) {
		perror("Can't open file");
		return CMD_OK;
	}

	printf("Dumping RAM to %s...\n", argv[1]);

	addr = 0;
	while (1) {
		if (diag_l7_d2_read(global_l2_conn, NS_MEMORY, addr, 8, buf) == 8) {
			happy = 1;
			errno = 0;
			if (print_hexdump_line(f, addr, 4, buf, 8) != 0) {
				if (errno != 0) {
					perror("\nError writing file");
				} else {
					/*error, but fprintf didn't set errno*/
					printf("\nError writing file");
				}
				return CMD_OK;
			}
		} else {
			happy = 0;
		}
		if ((addr&0x1f) == 0) {
			printf("\r%04X %s", addr, happy?":)":":/");
			fflush(stdout);
		}
		if (addr == 0xfff8) {
			break;
		}
		addr += 8;

		if (fast && !happy && addr < 0xf000) {
			addr = 0xf000;
		}
	}

	if (fclose(f) != 0) {
		perror("\nError writing file");
		return CMD_OK;
	}

	printf("\r%04X :D\n", addr);

	return CMD_OK;
}

/*
 * Display list of stored DTCs.
 */
static enum cli_retval cmd_850_dtc(int argc, UNUSED(char **argv)) {
	uint8_t buf[12];
	int rv;
	int i;
	int span;
	char *code, *desc;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_dtclist(global_l2_conn, sizeof(buf), buf);
		span = 1;
	} else {
		rv = diag_l7_kwp71_dtclist(global_l2_conn, sizeof(buf), buf);
		span = 5;
	}

	if (rv < 0) {
		printf("Couldn't retrieve DTCs.\n");
		return CMD_OK;
	}
	have_read_dtcs = true;

	if (rv == 0) {
		printf("No stored DTCs.\n");
		return CMD_OK;
	}

	printf("Stored DTCs:\n");
	for (i=0; i<rv; i+=span) {
		code = dtc_printable_by_raw(global_l2_conn->diag_l2_destaddr, buf[i], &desc);
		printf("%s (%02X) %s\n", code, buf[i], desc);
	}

	return CMD_OK;
}

/*
 * Clear stored DTCs.
 */
static enum cli_retval cmd_850_cleardtc(int argc, UNUSED(char **argv)) {
	char *input;
	int rv;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(CONNECTED_EITHER)) {
		return CMD_OK;
	}

	input = cli_basic_get_input("Are you sure you wish to clear the Diagnostic Trouble Codes (y/n) ? ", stdin);
	if (!input) {
		return CMD_OK;
	}

	if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
		printf("Not done\n");
		goto done;
	}

	if (!have_read_dtcs) {
		free(input);
		input = cli_basic_get_input("You haven't read the DTCs yet. Are you sure you wish to clear them (y/n) ? ", stdin);
		if (!input) {
			return CMD_OK;
		}
		if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
			printf("Not done\n");
			goto done;
		}
	}

	if (get_connection_status() == CONNECTED_D2) {
		rv = diag_l7_d2_cleardtc(global_l2_conn);
	} else {
		rv = diag_l7_kwp71_cleardtc(global_l2_conn);
	}

	if (rv == 0) {
		printf("No DTCs to clear!\n");
	} else if (rv == 1) {
		printf("Done\n");
	} else {
		printf("Failed\n");
	}

done:
	free(input);
	return CMD_OK;
}

/*
 * Reset the Service Reminder Light.
 */
static enum cli_retval cmd_850_resetsrl(int argc, UNUSED(char **argv)) {
	char *input;
	char *argvout[] = {"connect", "combi"};
	int rv;
	bool old_car;

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	input = cli_basic_get_input("Are you sure you wish to reset the Service Reminder Light (y/n) ? ", stdin);
	if (!input) {
		return CMD_OK;
	}

	if ((strcasecmp(input, "yes") != 0) && (strcasecmp(input, "y")!=0)) {
		printf("Not done\n");
		goto done;
	}

	/* If talking to wrong ECU, disconnect first */
	if (get_connection_status() != NOT_CONNECTED && global_l2_conn->diag_l2_destaddr!=0x51) {
		printf("Disconnecting from %s first.\n", current_ecu_desc());
		cmd_850_disconnect(1, NULL);
	}

	/* If not connected to combi, connect */
	if (get_connection_status() == NOT_CONNECTED) {
		if (cmd_850_connect(2, argvout) != CMD_OK) {
			printf("Couldn't connect to combined instrument panel.\n");
			goto done;
		}
	}

	/* '96/'97 must be unlocked first, but '98 rejects unlock command */
	old_car = (diag_l7_d2_io_control(global_l2_conn, 0x30, 0) == 0);

	rv = diag_l7_d2_run_routine(global_l2_conn, 0x30);

	if (rv == 0) {
		printf("Done\n");
	} else if (rv == DIAG_ERR_TIMEOUT && old_car) {
		/* '96/'97 either don't respond after SRL reset, or respond 
		   after a long delay? */
		printf("Probably done\n");
	} else {
		printf("Failed\n");
	}

done:
	free(input);
	return CMD_OK;
}

/*
 * Try to connect to each possible ECU. Print identification and DTCs for each
 * successfully connected ECU.
 *
 * There will always be some unsuccessful connection attempts in a scan-all
 * because at least one ECU in our list will be missing from any given vehicle.
 * For example, MSA 15.7 and Motronic M4.4 will never both be present in the
 * same car.
 */
static enum cli_retval cmd_850_scan_all(int argc, UNUSED(char **argv)) {
	struct ecu_info *ecu;
	char *argvout[2];
	char buf[4];

	if (!valid_arg_count(1, argc, 1)) {
		return CMD_USAGE;
	}

	if (!valid_connection_status(NOT_CONNECTED)) {
		return CMD_OK;
	}

	printf("Scanning all ECUs.\n");

	argvout[1] = buf;
	for (ecu = ecu_list; ecu->name != NULL; ecu++) {
		sprintf(buf, "%d", ecu->addr);
		if (ecu->addr == 0x10) {
			/* Skip Motronic M4.4 old protocol */
		} else if (cmd_850_connect(2, argvout) == CMD_OK) {
			cmd_850_id(1, NULL);
			cmd_850_dtc(1, NULL);
			cmd_850_disconnect(1, NULL);
		} else {
			printf("Couldn't connect to %s.\n", ecu->desc);
		}
	}

	printf("Scan-all done.\n");

	return CMD_OK;
}

/*
 * Test the specified vehicle component.
 */
static enum cli_retval cmd_850_test(int argc, char **argv) {
	if (!valid_connection_status(CONNECTED_D2)) {
		return CMD_OK;
	}

	if (argc==2 && strcasecmp(argv[1], "fan1")==0 && global_l2_conn->diag_l2_destaddr==0x7a) {
		if (diag_l7_d2_io_control(global_l2_conn, 0x0e, 3) == 0) {
			printf("Activating engine cooling fan.\n");
		} else {
			printf("Unable to activate fan.\n");
		}
	} else if (argc==2 && strcasecmp(argv[1], "fan2")==0 && global_l2_conn->diag_l2_destaddr==0x7a) {
		if (diag_l7_d2_io_control(global_l2_conn, 0x1f, 3) == 0) {
			printf("Activating engine cooling fan.\n");
		} else {
			printf("Unable to activate fan.\n");
		}
	} else {
		printf("Usage: test <testname>\n");
		if (global_l2_conn->diag_l2_destaddr == 0x7a) {
			printf("Available tests:\n");
			printf("fan1 - Activate engine cooling fan, half speed (please keep fingers clear)\n");
			printf("fan2 - Activate engine cooling fan, full speed (please keep fingers clear)\n");
		} else {
			printf("No available tests for this ECU.\n");
		}
	}
	return CMD_OK;
}
