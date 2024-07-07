#include <stdlib.h>
#include "dtc.h"
#include "ecu.h"
#include "config.h"

#ifdef __SCANTOOL_850_DTC_FROBBED__


const struct dtc_table_entry dtc_list_01[] = {
    {0x10, 311, "Left Front Wheel Sensor, open/short [or bad ABS module solders or ignition switch].", NULL},
    {0x11, 321, "Left Front Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch].", NULL},
    {0x13, 211, "Left Front Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch].", NULL},
    {0x14, 221, "Left Front Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch].", NULL},
    {0x20, 312, "Right Front Wheel Sensor, open/short [or bad ABS module solders or ignition switch].", NULL},
    {0x21, 322, "Right Front Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch].", NULL},
    {0x23, 212, "Right Front Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch].", NULL},
    {0x24, 222, "Right Front Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch].", NULL},
    {0x30, 313, "Left Rear Wheel Sensor, open/short [or bad ABS module solders or ignition switch].", NULL},
    {0x31, 323, "Left Rear Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch].", NULL},
    {0x33, 213, "Left Rear Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch, or stuck emergency brake].", NULL},
    {0x34, 223, "Left Rear Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch].", NULL},
    {0x40, 314, "Right Rear Wheel Sensor, open/short [or bad ABS module solders or ignition switch].", NULL},
    {0x41, 324, "Right Rear Wheel Sensor, irregular > 25 mph (ie, interference or excess oscillation > 40 km/h) [or bad ABS module solders or ignition switch].", NULL},
    {0x43, 214, "Right Rear Wheel Sensor, wrong wheel speed (ie, signal absent yet circuit intact, or signal absent when moving off) [or bad ABS module solders or ignition switch, or stuck emergency brake].", NULL},
    {0x44, 224, "Right Rear Wheel Sensor, ABS control phase too long (ie, signal absent in ABS function, yet circuit intact) [or bad ABS module solders or ignition switch].", NULL},
    {0x50, 411, "Left Front Wheel Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x51, 413, "Right Front Wheel Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x52, 421, "Rear Wheels Inlet Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x54, 412, "Left Front Wheel Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x55, 414, "Right Front Wheel Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x56, 422, "Rear Wheels Return Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x60, 423, "TRACS Valve, circuit open/short [ie, bad wiring, hydraulic modulator, ABS module, or ignition switch].", NULL},
    {0x61, 424, "??TRACS Pressure Switch, circuit open/short [or bad wiring, hydraulic modulator, brake light switch, ABS module, ignition switch, or blown fuse 12 (STOP LAMPS)]??.", NULL},
    {0x64, 141, "Brake Pedal Sensor, short [or bad ABS module solders, wiring, brake pedal sensor, or ignition switch].", NULL},
    {0x65, 142, "Brake Light/Pedal Switch, open/short or adjustment [or brake light bulb, bad ABS module solders, wiring, or ignition switch].", NULL},
    {0x66, 144, "TRACS Disengaged to Avoid Front Brake Discs Overheating [or bad ABS module solders or ignition switch].", NULL},
    {0x67, 143, "??Missing or Faulty Vehicle Speed Signal from ABS module?? [or bad ABS module solders, ABS module memory fault, or otherwise faulty ABS module or ignition switch].", NULL},
    {0x70, 443, "Pump motor fault [or fuse 9 (ABS PUMP MOTOR), pump power plug not seated properly, bad wiring (eg, insulation that falls off, any short/open), bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module].", NULL},
    {0x72, 431, "ABS Module, general hardware fault [or bad ABS module solders or ignition switch].", NULL},
    {0x75, 432, "ABS Module, general interference fault [or bad ABS module solders or ignition switch] [see \"https://www.matthewsvolvosite.com/forums/viewtopic.php?f=1&t=78577&start=30#p428745\"].", NULL},
    {0x77, 433, "Battery Voltage, ?too high?.", NULL},
    {0x80, 441, "ABS Microprocessors, redundant calculations mismatch [or fuse 14 (ABS MAIN SUPPLY), bad ABS module solders, wheel sensor wiring close to interference, ignition switch, or bad ABS module].", NULL},
    {0x81, 444, "??ABS Module, internal valve reference voltage error or no power to hydraulic valves [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??.", NULL},
    {0x82, 442, "??ABS Module, leakage current or Hydraulic Modulator Pump Pressure, too low [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??.", NULL},
    {0x83, 445, "??ABS Module, either inlet or return valve circuit fault [or bad wiring, bad ABS module solders, combination relay, hydraulic modulator, ignition switch, or bad ABS module]??.", NULL},
    {0, 0, NULL, NULL},
};
const struct dtc_table_entry dtc_list_11[] = {
    {0x00, 131, "Engine Speed (RPM) signal.", NULL},
    {0x01, 719, "Secondary Engine Speed signal.", NULL},
    {0x02, 132, "Battery Voltage.", NULL},
    {0x03, 711, "Needle Lift sensor signal.", NULL},
    {0x04, 732, "Accelerator Pedal Position sensor signal.", NULL},
    {0x05, 123, "Engine Coolant Temp (ECT) sensor signal.", NULL},
    {0x06, 122, "Intake Air Temp (IAT) sensor signal.", NULL},
    {0x08, 712, "Fuel Temp sensor signal.", NULL},
    {0x09, 415, "Boost Pressure sensor signal.", NULL},
    {0x0A, 112, "Fault in Engine Control Module (ECM) Barometric Pressure signal.", NULL},
    {0x0B, 121, "Mass Air Flow (MAF) sensor signal.", NULL},
    {0x0C, 743, "Cruise Control switch signal.", NULL},
    {0x0D, 112, "Fault in Engine Control Module (ECM) Reference Voltage.", NULL},
    {0x0F, 112, "Fault in Engine Control Module (ECM).", NULL},
    {0x10, 311, "Vehicle Speed signal too high [neither cruise control nor A/C will activate, possibly due to VSS fault, COMBI->ECM fault, or COMBI fault] [see OTP 850 DVD]; else Engine RPM vs. Vehicle Speed signal discrepancy [derived from old \"www.volvopedia.de/index.php?title=MSA 15.7\"].", NULL},
    {0x11, 235, "Exhaust gas recirculation (EGR) controller signal.", NULL},
    {0x12, 242, "Turbocharger Control Valve (TCV) signal.", NULL},
    {0x14, 515, "Engine Coolant Fan, high speed signal.", NULL},
    {0x19, 713, "Injection Timing, Advance Control Valve signal.", NULL},
    {0x1A, 323, "CHECK ENGINE light (CEL) / Malfunction indicator lamp (MIL).", NULL},
    {0x1B, 721, "Glowplug Indicator Light signal.", NULL},
    {0x20, 714, "Fuel Shut-off Valve signal.", NULL},
    {0x21, 730, "Brake Pedal Switch signal.", NULL},
    {0x22, 731, "Clutch Switch signal.", NULL},
    {0x23, 724, "Engine Coolant Heater Relay signal.", NULL},
    {0x24, 112, "Memory Fault (ROM) In Engine Control Module (ECM).", NULL},
    {0x26, 514, "Engine Coolant Fan, low speed signal.", NULL},
    {0x2A, 725, "Main Relay signal.", NULL},
    {0x2B, 715, "Fuel Regulation.", NULL},
    {0x2D, 726, "Terminal 15-supply to Engine Control Module (ECM).", NULL},
    {0x2E, 716, "Fuel Quantity Actuator signal.", NULL},
    {0x2F, 718, "Ignition Timing Control.", NULL},
    {0x30, 335, "Request for MIL lighting from TCM [or possibly Comm Failure between MSA 15.7 ECM and AW 50-42 TCM] [see OTP 850 DVD].", NULL},
    {0x31, 353, "Comm Failure with Immobilizer.", NULL},
    {0x32, 742, "Comm Failure between MSA 15.7 ECM and AW 50-42 TCM [or possibly Request for MIL lighting from TCM] [see OTP 850 DVD].", NULL},
    {0x33, 225, "A/C Pressure sensor signal.", NULL},
    {0x34, 717, "Fuel Quantity Regulator Position sensor signal.", NULL},
    {0x36, 732, "Accelerator pedal position sensor signal.", NULL},
    {0xFE, 131, "Engine Speed (RPM) signal.", NULL},
    {0, 0, NULL, NULL},
};
const struct dtc_table_entry dtc_list_29[] = {
    {0x30, 314, "Passenger side temp damper motor, no position change [according to xiaotec \"850 OBD-II\" V1.2.9 app]; or might be Floor/Defrost Damper Motor Shorted To Ground Or Power [see \"ac heater system auto.pdf\"].", NULL},
    {0x35, 412, "Driver's (or passenger's) side interior temperature sensor inlet fan shorted to earth (or signal too low) [see \"ac heater system auto.pdf\"].", NULL},
    {0x36, 413, "Driver's (or passenger's) side interior temperature sensor inlet fan, no control voltage (or signal too high) [see \"ac heater system auto.pdf\"].", NULL},
    {0x37, 414, "Driver's (or passenger's) side interior temperature sensor inlet fan seized (or open/short circuit or faulty signal) [carefully clean out any lint from temp sensor fan(s)]; else (inlet)Fan motor passenger comp (interior) temp sensor, Faulty Signal [according to xiaotec \"850 OBD-II\" V1.3.4 app].", NULL},
    {0x38, 411, "Blower fan seized or drawing excess current [or blower fan obstruction, or power stage surge protector problem (ECC-419)].", NULL},
    {0, 0, NULL, NULL},
};
const struct dtc_table_entry dtc_list_41[] = {
    {0x01, 112, "Internal EEPROM memory fault.", NULL},
    {0x06, 223, "Comm fault from Transponder to IMMO Control Module.", NULL},
    {0x07, 336, "Control circuit to VGLA, fault in VGLA or LED circuit (P600).", NULL},
    {0x32, 324, "VERLOG code missing.", NULL},
    {0x34, 121, "PIN programming failed.", NULL},
    {0x35, 225, "Key Code verification failed.", NULL},
    {0x36, 132, "No Key Codes in EEPROM.", NULL},
    {0x37, 122, "New replacement Immobilizer not yet programmed.", NULL},
    {0x38, 999, "?EEPROM error?.", NULL},
    {0x51, 311, "Comms with engine ECU, short to supply in comm link 2 or 4.", NULL},
    {0x52, 312, "Comms with engine ECU, short to ground in comm link 2 or 4.", NULL},
    {0x53, 214, "Comms from engine ECU, wrong MIN code (ie, Immobilizer is programmed for a different engine ECU).", NULL},
    {0x54, 321, "Initiating signal from ECM, missing.", NULL},
    {0x55, 326, "Reply signal from engine control module.", NULL},
    {0xF7, 333, "Control circuit to VGLA, fault in VGLA or LED circuit.", NULL},
    {0xF8, 336, "Control circuit to VGLA, fault in VGLA or LED circuit (P600).", NULL},
    {0, 0, NULL, NULL},
};
const struct dtc_table_entry dtc_list_51[] = {
    {0x01, 222, "Vehicle Speed Signal too high.", NULL},
    {0x02, 221, "Vehicle Speed Signal missing [usually due to bad ABS module solders] [P0500].", NULL},
    {0x03, 114, "Fuel Level sensor stuck / signal constant for 94 miles.", NULL},
    {0x04, 112, "Fuel Level signal short to ground.", NULL},
    {0x05, 113, "Low Fuel / Fuel Level signal interrupted.", NULL},
    {0x06, 121, "Engine Coolant Temperature signal faulty [see \"http://www.volvotips.com/index.php/850-2/volvo-850-s70-v70-c70-service-repair-manual/volvo-850-instrument-panel-service-repair-manual/\"].", NULL},
    {0x07, 123, "48-pulse output Speed Signal short to supply.", NULL},
    {0x08, 143, "48-pulse output Speed Signal short to ground.", NULL},
    {0x09, 131, "12-pulse output Speed Signal short to supply.", NULL},
    {0x0A, 141, "12-pulse output Speed Signal short to ground.", NULL},
    {0x0B, 132, "Engine RPM signal missing.", NULL},
    {0x0C, 124, "Engine RPM signal faulty.", NULL},
    {0x0D, 211, "D+ alternator voltage signal missing (or too low) for >= 10 sec when > 1000 RPM.", NULL},
    {0x0E, 133, "Fuel Level Signal To Trip Computer short to supply.", NULL},
    {0x0F, 142, "Ambient Temperature signal missing.", NULL},
    {0x10, 231, "COMBI microprocessor internal fault.", NULL},
    {0x11, 174, "Wide Disparity in Fuel Levels and/or Fuel Consumption signal missing [DTC rarely seen; (in 850 especially) DTC 11 Freeze Frame sometimes precedes a Low Fuel situation, occasionally follows a Fillup, and oftentimes persists after clearing COMBI DTCs].", NULL},
    {0x20, 232, "COMBI is not yet programmed.", NULL},
    {0, 0, NULL, NULL},
};
const struct dtc_table_entry dtc_list_58[] = {
    {0x01, 112, "Crash Sensor Module internal fault.", NULL},
    {0x02, 211, "Driver Airbag short circuit (in contact reel and/or in SRS harness to airbag, connectors, or igniter).", NULL},
    {0x03, 212, "Driver Airbag open circuit (in SRS harness, connector, or airbag).", NULL},
    {0x04, 213, "Driver Airbag short circuit to ground (in contact reel, airbag, wiring harness, or connectors).", NULL},
    {0x05, 214, "Driver Airbag short circuit to supply (in contact reel, airbag, wiring harness, or connectors).", NULL},
    {0x06, 221, "Passenger Airbag short circuit (in SRS harness to airbag, connectors, or igniter).", NULL},
    {0x07, 222, "Passenger Airbag open circuit (in SRS harness, connector, or airbag).", NULL},
    {0x08, 223, "Passenger Airbag short circuit to ground (in airbag, wiring harness, or connectors).", NULL},
    {0x09, 224, "Passenger Airbag short circuit to supply (in airbag, wiring harness, or connectors).", NULL},
    {0x0A, 231, "Left Seat Belt Tensioner short circuit (in SRS harness to tensioner, connectors, or igniter).", NULL},
    {0x0B, 232, "Left Seat Belt Tensioner open circuit (in SRS harness, connector, or tensioner).", NULL},
    {0x0C, 233, "Left Seat Belt Tensioner short circuit to ground (in tensioner, wiring harness, or connectors).", NULL},
    {0x0D, 234, "Left Seat Belt Tensioner short circuit to supply (in tensioner, wiring harness, or connectors).", NULL},
    {0x0E, 241, "Right Seat Belt Tensioner short circuit (in SRS harness to tensioner, connectors, or igniter).", NULL},
    {0x0F, 242, "Right Seat Belt Tensioner open circuit (in SRS harness, connector, or tensioner).", NULL},
    {0x10, 243, "Right Seat Belt Tensioner short circuit to ground (in tensioner, wiring harness, or connectors).", NULL},
    {0x11, 244, "Right Seat Belt Tensioner short circuit to supply (in tensioner, wiring harness, or connectors).", NULL},
    {0x52, 127, "SRS Warning Light short circuit to ground or open circuit [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"https://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"].", NULL},
    {0x53, 128, "SRS Warning Light short circuit to supply [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"https://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"].", NULL},
    {0x65, 127, "SRS Warning Light short circuit to ground or open circuit [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"https://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"].", NULL},
    {0x66, 128, "SRS Warning Light short circuit to supply [see sections \"FAULT CODE 1-2-7\" and \"SRS WARNING LIGHT WILL NOT COME ON\" in \"air bag restraint system.pdf\" of \"https://www.matthewsvolvosite.com/downloads/Volvo_850.zip\"].", NULL},
    {0x67, 129, "Battery Voltage signal too low.", NULL},
    {0x68, 213, "Driver Airbag signal too low.", NULL},
    {0x69, 214, "Driver Airbag signal too high.", NULL},
    {0x6A, 233, "Left Seat Belt Tensioner signal too low.", NULL},
    {0x6B, 234, "Left Seat Belt Tensioner signal too high.", NULL},
    {0x6C, 243, "Right Seat Belt Tensioner signal too low.", NULL},
    {0x6D, 244, "Right Seat Belt Tensioner signal too high.", NULL},
    {0x6E, 223, "Passenger Airbag signal too low.", NULL},
    {0x6F, 224, "Passenger Airbag signal too high.", NULL},
    {0x70, 313, "Left Rear Seat Belt Tensioner signal too low.", NULL},
    {0x71, 314, "Left Rear Seat Belt Tensioner signal too high.", NULL},
    {0x72, 323, "Right Rear Seat Belt Tensioner signal too low.", NULL},
    {0x73, 324, "Right Rear Seat Belt Tensioner signal too high.", NULL},
    {0x78, 211, "Driver Airbag signal faulty.", NULL},
    {0x79, 212, "Driver Airbag signal missing.", NULL},
    {0x7A, 231, "Left Seat Belt Tensioner signal faulty.", NULL},
    {0x7B, 232, "Left Seat Belt Tensioner signal missing.", NULL},
    {0x7C, 241, "Right Seat Belt Tensioner signal faulty.", NULL},
    {0x7D, 242, "Right Seat Belt Tensioner signal missing.", NULL},
    {0x7E, 221, "Passenger Airbag signal faulty.", NULL},
    {0x7F, 222, "Passenger Airbag signal missing.", NULL},
    {0x80, 311, "Left Rear Seat Belt Tensioner signal faulty.", NULL},
    {0x81, 312, "Left Rear Seat Belt Tensioner signal missing.", NULL},
    {0x82, 321, "Right Rear Seat Belt Tensioner signal faulty.", NULL},
    {0x83, 322, "Right Rear Seat Belt Tensioner signal missing.", NULL},
    {0x88, 210, "Driver Airbag fault.", NULL},
    {0x89, 230, "Left Seat Belt Tensioner fault.", NULL},
    {0x8A, 240, "Right Seat Belt Tensioner fault.", NULL},
    {0x8B, 220, "Passenger Airbag fault.", NULL},
    {0x8C, 310, "Left Rear Seat Belt Tensioner fault.", NULL},
    {0x8D, 320, "Right Rear Seat Belt Tensioner fault.", NULL},
    {0xC7, 114, "Control Module faulty signal.", NULL},
    {0, 0, NULL, NULL},
};
const struct ecu_info ecu_list[] = {
    { .addr = 0x01, .desc = "ABS", .dtc_prefix = "ABS" },
    { .addr = 0x11, .desc = "EFI", .dtc_prefix = "EFI" },
    { .addr = 0x29, .desc = "ECC", .dtc_prefix = "ECC" },
    { .addr = 0x41, .desc = "IMM", .dtc_prefix = "IMM" },
    { .addr = 0x51, .desc = "CI", .dtc_prefix = "CI" },
    { .addr = 0x58, .desc = "SRS", .dtc_prefix = "SRS" },
    {0, NULL, NULL, NULL}
};

const struct ecu_dtc_table_map_entry ecu_dtc_map[] = {
    {0x01, dtc_list_01},
    {0x11, dtc_list_11},
    {0x29, dtc_list_29},
    {0x41, dtc_list_41},
    {0x51, dtc_list_51},
    {0x58, dtc_list_58},
    {0, NULL},
};

#endif // __SCANTOOL_850_DTC_FROBBED__
