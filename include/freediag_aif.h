
/* Commands from the application to scantool */

#define FREEDIAG_AIF_NO_OP       0     /* Do Nothing             */
#define FREEDIAG_AIF_EXIT        1     /* Exit ScanTool          */
#define FREEDIAG_AIF_MONITOR     2     /* Monitor                */
#define FREEDIAG_AIF_WATCH       3     /* Watch diagnostic bus   */
#define FREEDIAG_AIF_CLEAR_DTC   4     /* Clear DTC's from ECU   */
#define FREEDIAG_AIF_ECUS        5     /* Show ECU information   */
#define FREEDIAG_AIF_SET         6     /* Set a parameter        */
#define FREEDIAG_AIF_TEST        7     /* Perform various tests  */
#define FREEDIAG_AIF_SCAN        8     /* Start Scan Process     */
#define FREEDIAG_AIF_DIAG        9     /* Extended diagnostics   */
#define FREEDIAG_AIF_VW         10     /* VW diagnostic protocol */
#define FREEDIAG_AIF_DYNO       11     /* Dyno functions         */
#define FREEDIAG_AIF_DEBUG      12     /* Display debug stuff    */
#define FREEDIAG_AIF_DISCONNECT 13     /* Disconnect from car    */

/* Sub-commands for 'SET' */

#define FREEDIAG_AIF_SET_PORT   0
#define FREEDIAG_AIF_SET_UNITS  1

/* Sub-commands for 'SET_UNITS' */

#define FREEDIAG_AIF_SET_UNITS_METRIC  0
#define FREEDIAG_AIF_SET_UNITS_US      1

/*
  Responses from scantool to application
*/

#define FREEDIAG_AIF_ERROR_RETURN  0
#define FREEDIAG_AIF_OK_RETURN     1

/*
  The maximum length of a Freediag/AIF command or response.
*/

#define FREEDIAG_AIF_INPUT_MAX 1024

