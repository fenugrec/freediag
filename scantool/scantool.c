/*
 *    freediag - Vehicle Diagnostic Utility
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
 *
 * Mostly ODBII Compliant Scanner Program (SAE J1978)
 *
 * ODBII Scanners are defined in SAE J1978. References in this document
 * are to SAE J1978 Revised Feb1998. This document is available from
 * www.sae.org
 *
 * From Section 5. Required functions & support - the following are the basic
 * functions that the scan tool is required to support or provide
 *
 * a. Automatic hands-off determination of the communication interface user
 * b. Obtaining and displaying the status and results of vehicle on-board
 *    diagnostic evaluations
 * c. Obtaining & Displaying ODB II emissions related DTCs
 * d. Obtaining & Displaying ODB II emissions related current data
 * e. Obtaining & Displaying ODB II emissions related freeze frame data
 * f. Clearing the storage of (c) to (e)
 * g. Obtaining and displaying ODB II emissions related test params and
 *    results as described in SAE J1979
 * h. Provide a user manual and/or help facility
 *
 * Section 6 - Vehicle interface
 *    Communication Data Link and Physical Layers
 *        SAE J1850 interface
 *        ISO 9141-2 interface
 *        ISO 14230-4
 *
 * Section 7.3 - the scan tool must be capable of interfacing with a
 *    vehicle in which multiple modules may be used to support ODBII
 *    requirements
 *    The ODBII Scan tool must alert the user when multiple modules
 *    respond to the same request
 *    Ditto if different values
 *    The tool must provide the user with the ability to select for
 *    display as separate display items the responses received from
 *    multiple modules for the same data item
 *
 *
 * THIS DOESN'T SUPPORT Section 6 as we only have one interface
 * - It "copes" with 7.3 but doesn't tell user or allow user to select
 * which module to see responses from
 *
 *
 *************************************************************************
 *
 * This file contains the workhorse routines, ie all that execute the
 * J1979 (ODBII) protocol
 */

#include "diag.h"
#include "diag_dtc.h"
#include "diag_l1.h"
#include "diag_l2.h"
#include "diag_l3.h"

#include "scantool.h"
#include "scantool_cli.h"
#include "scantool_aif.h"

CVSID("$Id$");

/*
 * This is used to store the 1st message of a received set of messages
 * It's only 24 bytes long as this is plenty to store a J1979 message
 */
#if notdef
uint8_t global_data[MAXRBUF];
int global_datalen;
#endif

struct diag_l2_conn  *global_l2_conn;
struct diag_l3_conn  *global_l3_conn;

/*
 * Data received from each ecu
 */
ecu_data_t    ecu_info[MAX_ECU];
unsigned int ecu_count;        /* How many ecus are active */


/* Merge of all the suported mode1 pids by all the ECUs */
uint8_t    merged_mode1_info[0x100];
uint8_t    merged_mode5_info[0x100];

uint8_t    global_O2_sensors;    /* O2 sensors bit mask */

int        global_conmode;
int        global_protocol;

int        global_state;        /* See STATE_ definitions in .h file */

struct diag_l0_device *        global_l2_dl0d;        /* L2 dl0d */

/* Prototypes */
int print_single_dtc(databyte_type d0, databyte_type d1) ;
void do_j1979_getmodeinfo(int mode, int response_offset) ;

struct diag_l2_conn *do_common_start(int L1protocol, int L2protocol,
    uint32_t type, int bitrate, target_type target, source_type source );

int do_l3_md1pid0_rqst( struct diag_l2_conn *d_conn ) ;
void initialse_ecu_data(void);


struct diag_msg *
find_ecu_msg(int byte, databyte_type val)
{
    ecu_data_t *ep;
    struct diag_msg *rv = NULL;
    unsigned int i;

    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if (ep->rxmsg)
        {
            /* Some data arrived from this ecu */
            if (ep->rxmsg->data[byte] == val)
            {
                rv = ep->rxmsg;
                break;
            }
        }
    }
    return(rv);
}


/*
 * Message print out / debug routines
 */
static void
print_msg_header(FILE *fp, struct diag_msg *msg, int timestamp, int i)
{
    if (timestamp)
        fprintf(fp, "%ld.%04ld: ",
            (long)msg->rxtime.tv_sec, (long)msg->rxtime.tv_usec/100);
    fprintf(fp, "msg %02d src 0x%lx dest 0x%lx ", i, (long)msg->src, (long)msg->dest);
    fprintf(fp, "msg %02d: ", i);
}

static void
print_msg(FILE *fp, struct diag_msg *msg, int timestamp)
{
        struct diag_msg *tmsg;
    int i, j;

    for (tmsg = msg, i = 0; tmsg; tmsg = tmsg->next, i++)
    {
            print_msg_header(fp, tmsg, timestamp, i);
        for (j = 0; j < tmsg->len; j++)
                fprintf(fp, "0x%02x ", tmsg->data[j]);
        fprintf(fp, "\n");
    }
}

/*
 * ************
 * Basic routines to connect/interrogate an ECU
 * ************
 */

/*
 * Receive callback routines. If handle is 1 then we're in "watch"
 * mode (set by caller to recv()), else in normal data mode
 *
 * We get called by L3/L2 with all the messages received within the
 * window, i.e we can get responses from many ECUs that all relate to 
 * a single request [ISO9141/14230 uses timing windows to decide when
 * no more responses will arrive]
 *
 * We can [and do] get more than one ecu responding with different bits
 * of data on certain vehicles
 */
void
j1979_data_rcv(void *handle, struct diag_msg *msg)
{
    int len = msg->len;
    uint8_t *data = msg->data;
    struct diag_msg *tmsg;
    unsigned int i;
    ecu_data_t    *ep;

    const char *O2_strings[] = {
        "Test 0", 
        "Rich to lean sensor threshold voltage",
        "Lean to rich sensor threshold voltage",
        "Low sensor voltage for switch time calc.",
        "High sensor voltage for switch time calc.",
        "Rich to lean sensor switch time",
        "Lean to rich sensor switch time",
        "Minimum sensor voltage for test cycle",
        "Maximum sensor voltage for test cycle",
        "Time between sensor transitions"
        };

    if (diag_cmd_debug > DIAG_DEBUG_DATA)
    {
        fprintf(stderr, "scantool: Got handle %p %d bytes of data, src %x, dest %x msgcnt %d\n",
            handle, len, msg->src, msg->dest, msg->mcnt);
    }

    /* Debug level for showing received data */
    if (diag_cmd_debug & DIAG_DEBUG_DATA)
    {
            print_msg(stdout, msg, 0);
        data = msg->data;
    }

    /* Deal with the diag type responses (send/recv/watch) */
    switch ((uint32_t)handle)
    {
    /* There is no difference between watch and decode ... */
    case RQST_HANDLE_WATCH:
    case RQST_HANDLE_DECODE:
        if (!(diag_cmd_debug & DIAG_DEBUG_DATA))
        {
            /* Print data (unless done already) */
                print_msg(stdout, msg, 0);
        }
        return;
    }

    /* All other responses are J1979 response messages */

    /* Clear out old messages */
    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if (ep->rxmsg)
        {
            /* Old msg. release it */
            diag_freemsg(ep->rxmsg);
            ep->rxmsg = NULL;
        }
    }

    /*
     * We may get more than one msg here, as more than one
     * ECU may respond.
     */
    for (tmsg = msg; tmsg; tmsg=tmsg->next)
    {
        uint8_t src = tmsg->src;
        struct diag_msg *rmsg;
        int found;
    
        for (i=0, ep=ecu_info, found=0; i<MAX_ECU;i++, ep++)
        {
            if (ep->valid)
            {
                if (ep->ecu_addr == src)
                {
                    found = 1;
                    break;
                }
            }
            else
            {
                ecu_count++;
                ep->valid = 1;
                ep->ecu_addr = src;
                found = 1;
                break;
            }
        }
        if (found == 0)
        {
            fprintf(stderr, "ERROR: Too many ECUs responded\n");
            fprintf(stderr, "ERROR: Info from ECU addr 0x%x ignored\n", src);
            return;
        }

        /* Ok, we now have the ecu_info for this message fragment */

        /* Attach the fragment to the ecu_info */
        rmsg = diag_dupsinglemsg(tmsg);    
        if (ep->rxmsg)
        {
            struct diag_msg *xmsg = ep->rxmsg;
            while (xmsg)
            {
                if (xmsg->next == NULL)
                {
                    xmsg->next = rmsg;
                    break;
                }
                xmsg = xmsg->next;
            }
        }
        else
        {
            ep->rxmsg = rmsg;
        }

        /*
         * Deal with readiness tests, ncms and O2 sensor tests
         * Note that ecu_count gets to the correct value from
         * the first response from the ECU which is the mode1pid0
         * response
         */
        data = msg->data;
        switch ((uint32_t)handle)
        {
        case RQST_HANDLE_READINESS:
            /* Handled in cmd_test_readiness() */
            break;

        case RQST_HANDLE_NCMS:
        case RQST_HANDLE_NCMS2:
            /*
             * Non Continuously Monitored System result
             * NCMS2 prints everything, NCMS prints just failed
             * tests
             */
            if (data[0] != 0x46)
            {
                fprintf(stderr, "Test 0x%02x failed %d\n",
                    data[1], data[2]);
                return;
            }
            if ((data[1] & 0x1f) == 0)
            {
                /* no Test support */
                return;
            }
            for (tmsg = msg , i = 0; tmsg; tmsg=tmsg->next, i++)
            {
                int val, lim;
                data = tmsg->data;
                len = tmsg->len;

                val = (data[3]*255) + data[4];
                lim = (data[5]*255) + data[6];

                if ((data[2] & 0x80) == 0)
                {
                    if ((uint32_t)handle
                        == RQST_HANDLE_NCMS2)
                    {
                        /* Only print fails */
                        if (val > lim)
                        {
                            fprintf(stderr, "Test 0x%x Component 0x%x FAILED ",
                            data[1], data[2] & 0x7f);
                            fprintf(stderr, "Max val %d Current Val %d\n",
                                lim, val);

                        }
                    }
                    else
                    {
                        /* Max value test */
                        fprintf(stderr, "Test 0x%x Component 0x%x ",
                            data[1], data[2] & 0x7f);
            
                        if (val > lim)
                            fprintf(stderr, "FAILED ");
                        else
                            fprintf(stderr, "Passed ");

                        fprintf(stderr, "Max val %d Current Val %d\n",
                            lim, val);
                    }
                }
                else
                {
                    if ((uint32_t)handle ==
                        RQST_HANDLE_NCMS2)
                    {
                        if (val < lim)
                        {
                            fprintf(stderr, "Test 0x%x Component 0x%x FAILED ",
                                data[1], data[2] & 0x7f);
                            fprintf(stderr, "Min val %d Current Val %d\n",
                                lim, val);
                        }
                    }
                    else
                    {
                        /* Min value test */
                        fprintf(stderr, "Test 0x%x Component 0x%x ",
                            data[1], data[2] & 0x7f);
                        if (val < lim)
                            fprintf(stderr, "FAILED ");
                        else
                            fprintf(stderr, "Passed ");

                        fprintf(stderr, "Min val %d Current Val %d\n",
                            lim, val);
                    }
                }
            }
            return;

        case RQST_HANDLE_O2S:
            if (ecu_count>1)
                fprintf(stderr, "ECU %d ", i);

            /* O2 Sensor test results */
            if (msg->data[0] != 0x45)
            {
                fprintf(stderr, "Test 0x%02x failed %d\n",
                    msg->data[1], msg->data[2]);
                return;
            }
            if ((data[1] & 0x1f) == 0)
            {
                /* No Test support */
            }
            else
            {
                int val = data[4];
                int min = data[5];
                int max = data[6];
                int failed ;

                if ((val < min) || (val > max))
                    failed = 1;
                else
                    failed = 0;

                switch (data[1])
                {
                case 1:    /* Constant values voltages */
                case 2:
                case 3:
                case 4:
                    fprintf(stderr, "%s: %f\n", O2_strings[data[1]],
                            data[4]/200.0);
                    break;
                case 5:
                case 6:
                case 9:
                    fprintf(stderr, "%s: actual %2.2f min %2.2f max %2.2f %s\n",
                        O2_strings[data[1]], data[4]/250.0,
                        data[5]/250.0, data[6]/250.0,
                        failed?"FAILED":"Passed" );
                    break;
                case 7:
                case 8:
                    fprintf(stderr, "%s: %f %f %f %s\n", O2_strings[data[1]],
                        data[4]/200.,
                        data[5]/200.,
                        data[6]/200.,
                        failed?"FAILED":"Passed" );
                    break;
                default:
                    fprintf(stderr, "Test %d: actual 0x%x min 0x%x max 0x%x %s\n",
                        data[1], data[4],
                        data[5], data[6],
                        failed?"FAILED":"Passed" );
                    break;
                }
            }
            return;
        }
    }
    return;
}


/*
 * Receive callback routines, for watching mode, call
 * L3 (in this case SAE J1979) decode routine, if handle is NULL
 * just print the data
 */
void
j1979_watch_rcv(void *handle, struct diag_msg *msg)
{
    struct diag_msg *tmsg;
    int i, j;

    for ( tmsg = msg , i = 0; tmsg; tmsg=tmsg->next, i++ )
    {
        fprintf(stderr, "%ld.%04ld: ", (long)tmsg->rxtime.tv_sec, (long)tmsg->rxtime.tv_usec/100);
        fprintf(stderr, "msg %02d src 0x%x dest 0x%x ", i, msg->src, msg->dest);
        fprintf(stderr, "msg %02d: ", i);

        if (handle != NULL) {
            char buf[256];    /* XXX Can we switch to stdargs for decoders? */
            fprintf(stderr, "%s\n",
                diag_l3_decode((struct diag_l3_conn *)handle, tmsg,
                buf, sizeof(buf)));
        }
        else
        {
            for (j=0; j<tmsg->len; j++)
                fprintf(stderr, "0x%02x ", tmsg->data[j]);
            fprintf(stderr, "\n");
        }
    }
}

#ifdef WIN32
void
l2raw_data_rcv(void *handle,
struct diag_msg *msg)
#else
void
l2raw_data_rcv(void *handle __attribute__((unused)),
struct diag_msg *msg)
#endif
{
    /*
     * Layer 2 call back, just print the data, this is used if we
     * do a "read" and we haven't yet added a L3 protocol
     */
    struct diag_msg *tmsg;
    int i;
    int len;
    uint8_t *data;

    for ( tmsg = msg , i = 0; tmsg; tmsg=tmsg->next, i++ )
    {
        fprintf(stderr, "msg %02d src 0x%x dest 0x%x ", i, tmsg->src, tmsg->dest);
        fprintf(stderr, "msg %02d: ", i);
        len = tmsg->len;
        data = tmsg->data;
        while (len)
        {
            fprintf(stderr, "0x%02x ", *data);
            len--; data++;
        }
        fprintf(stderr, "\n");
    }
}

/*
 * Routine to check the bitmasks of PIDS received in response
 * to a mode 1 PID 0/0x20/0x40 request
 */
int
l2_check_pid_bits(uint8_t *data, int pid)
{
    int offset;
    int bit;

    pid--;        /* (bits start at 0, pids at 1) */
    /*
     * Bits 1-8 are in byte 1, 9-16 in byte 2 etc
     * Same code is for PID requests for 0x40 and 0x60
     */
    while (pid > 0x20)
        pid -= 0x20;
    offset = pid/8;

    bit = pid - (offset * 8);
    bit = 7 - bit;

    if (data[offset] & (1<<bit))
        return(1);

    return(0);
}

/*
 * Send a SAE J1979 request, and get a response, and part process it
 */
int
l3_do_j1979_rqst(struct diag_l3_conn *d_conn, int mode, int p1, int p2,
    int p3, int p4, int p5, int p6, int p7, void *handle)
{
    struct diag_msg    msg;
    uint8_t data[256];
    int rv;
    ecu_data_t *ep;
    unsigned int i;

    uint8_t *rxdata;
    struct diag_msg *rxmsg;

    /* Lengths of msg for each mode, 0 = this routine doesn't support */
    char mode_lengths[] = { 0, 2, 3, 1, 1, 3, 2, 1, 7, 2 };
#define J1979_MODE_MAX 9

    if (diag_cmd_debug > DIAG_DEBUG_DATA)
    {
        fprintf(stderr, "j1979_rqst: handle %p conn %p mode %x\n",
            handle, d_conn, mode);

    }

    /* Put in src/dest etc, L3 or L2 may override/ignore them */
    msg.src = set_testerid;
    msg.dest = set_destaddr;    /* Current set destination */

    /* XXX add funcmode flags */

    if (mode > J1979_MODE_MAX)
        return(-1);
    else
        msg.len = mode_lengths[mode];
    
    msg.data = data;
    data[0] = mode;
    data[1] = p1;
    data[2] = p2;
    data[3] = p3;
    data[4] = p4;
    data[5] = p5;
    data[6] = p6;
    data[7] = p7;
    diag_l3_send(d_conn, &msg);

    /* And get response(s) within a short while */
    rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);
    if (rv < 0)
    {
        fprintf(stderr, "Request failed, retrying...\n");
        diag_l3_send(d_conn, &msg);
        rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);
        if (rv < 0)
        {
            fprintf(stderr, "Retry failed, resynching...\n");
            rv = do_l3_md1pid0_rqst(global_l2_conn);
            if (rv < 0)
                fprintf(stderr, "Resync failed, connection to ECU may be lost!\n");
            return(rv);
        }
    }

    switch ((uint32_t)handle)
    {
    /* We dont process the info in watch/decode mode */
    case RQST_HANDLE_WATCH:
    case RQST_HANDLE_DECODE:
        return(rv);
    }

    /*
     * Go thru the ecu_data and see what was received.
     */
    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if (ep->rxmsg)
        {
            /* Some data arrived from this ecu */
            rxmsg = ep->rxmsg;
            rxdata = ep->rxmsg->data;

            switch (mode)
            {
            case 1:
                if (rxdata[0] != 0x41)
                {
                    ep->mode1_data[p1].type = TYPE_FAILED;
                    break;
                }
                memcpy(ep->mode1_data[p1].data, rxdata,
                    rxmsg->len);
                ep->mode1_data[p1].len = rxmsg->len;
                ep->mode1_data[p1].type = TYPE_GOOD;

                break;
            case 2:
                if (rxdata[0] != 0x42)
                {
                    ep->mode2_data[p1].type = TYPE_FAILED;
                    break;
                }
                memcpy(ep->mode2_data[p1].data, rxdata,
                    rxmsg->len);
                ep->mode2_data[p1].len = rxmsg->len;
                ep->mode2_data[p1].type = TYPE_GOOD;

                break;
            }    
        }
    }
    return(0);
}


/*
 * Send some data to the ECU (L3)
 */
int
l3_do_send(struct diag_l3_conn *d_conn, void *data, size_t len, void *handle)
{
    struct diag_msg    msg;
    int rv;


    /* Put in src/dest etc, L3 or L2 may override/ignore them */
    msg.src = set_testerid;
    msg.dest = set_destaddr;

    msg.len = len;    
    msg.data = (uint8_t *)data;
    diag_l3_send(d_conn, &msg);

    /* And get response(s) */
    rv = diag_l3_recv(d_conn, 300, j1979_data_rcv, handle);

    return (rv);
}
/*
 *  Same but L2 type
 */
int
l2_do_send(struct diag_l2_conn *d_conn, void *data, size_t len, void *handle)
{
    struct diag_msg    msg;
    int rv;

    /* Put in src/dest etc, L2 may override/ignore them */
    msg.src = set_testerid;
    msg.dest = set_destaddr;

    msg.len = len;    
    msg.data = (uint8_t *)data;
    diag_l2_send(d_conn, &msg);

    /* And get response(s) */
    rv = diag_l2_recv(d_conn, 300, l2raw_data_rcv, handle);

    return (rv);
}


/*
 * Clear data that is relevant to an ECU
 */
static int
clear_data(void)
{
    ecu_count = 0;
    memset(ecu_info, 0, sizeof(ecu_info));

    memset(merged_mode1_info, 0, sizeof(merged_mode1_info));
    memset(merged_mode5_info, 0, sizeof(merged_mode5_info));

    return(0);
}

/*
 * Common start routine used by all protocols
 * - initialises the diagnostic layer
 * - opens a Layer 2 device for the specified Layer 1 protocol
 * If necessary tries to poll the ECU
 * returns L2 file descriptor
 */
static struct diag_l2_conn * do_l2_common_start(int L1protocol, int L2protocol,
    uint32_t type, int bitrate, target_type target, source_type source )
{
    int rv;
    struct diag_l0_device *dl0d;
    struct diag_l2_conn *d_conn = NULL;
    int l2flags;

    /* Clear out all ECU data as we're starting again */
    clear_data();

    rv = diag_init();
    if (rv != 0)
    {
        fprintf(stderr, "diag_init failed\n");
        return(NULL);
    }

    dl0d = diag_l2_open(set_interface, set_subinterface, L1protocol);
    if (dl0d == 0)
    {
        rv = diag_geterr();
        if ((rv != DIAG_ERR_BADIFADAPTER) &&
            (rv != DIAG_ERR_PROTO_NOTSUPP))
                fprintf(stderr, "Failed to open hardware interface\n");

        return((struct diag_l2_conn *)diag_pseterr(rv));
    }

    /* Now do the Layer 2 startcommunications */

    d_conn = diag_l2_StartCommunications(dl0d, L2protocol, type,
        bitrate, target, source);

    if (d_conn == NULL)
    {
        diag_l2_close(dl0d);
        return(NULL);
    }

    /*
     * Now Get the L2 flags, and if this is a network type where
     * startcommunications always works, we have to try and see if
     * the ECU is there
     *
     * Some interface types will always return success from StartComms()
     * but you only know if the ECU is on the network if you then can
     * send/receive data to it in the appropriate format.
     * For those type of interfaces we send a J1979 mode1 pid1 request
     * (since we are a scantool, thats the correct thing to send)
     */
    if (diag_l2_ioctl(d_conn, DIAG_IOCTL_GET_L2_FLAGS, &l2flags) != 0)
    {
        fprintf(stderr, "Failed to get Layer 2 flags\n");
        diag_l2_close(dl0d);
        return(NULL);
    }
    if (l2flags & DIAG_L2_FLAG_CONNECTS_ALWAYS)
    {
        rv = do_l3_md1pid0_rqst(d_conn);
        if (rv < 0)
        {
            /* Not actually there, close L2 and go */
            diag_l2_close(dl0d);
            return(NULL);
        }
    }
    return(d_conn);
}

/*
 * Send a mode1 pid1 request and wait for a response. This is used to
 * (a) See if there is an ECU there
 * (b) Error recovery if we get multiple timeouts on an ECU
 *
 * It cant use the normal request routine, since that calls this
 *
 *
 * XXX This is wrong and needs to talk to L3 not L2, so this breaks
 * on ISO9141 type interfaces
 */
int do_l3_md1pid0_rqst( struct diag_l2_conn *d_conn )
{
    struct diag_msg    msg;
    struct diag_msg    *rmsg;
    uint8_t data[256];
    int errval = DIAG_ERR_GENERAL;

    /* Create mode 1 pid 0 message */
    msg.src = set_testerid;
    msg.dest = set_destaddr;
    msg.len = 2;
    msg.data = data;
    data[0] = 1;
    data[1] = 0;

    // Do a L2 request ...
    rmsg = diag_l2_request(d_conn, &msg, &errval);
    if (rmsg != NULL)
    {
        /* Check its a Mode1 Pid0 response */    
        if (rmsg->len < 1)
        {
            return(diag_iseterr(DIAG_ERR_BADDATA));    
        }
// What is this???
/*         if (rmsg->data[0] != 0x41) */
/*         { */
/*             return(diag_iseterr(DIAG_ERR_BADDATA));     */
/*         } */

        return(0);
    }
    return(errval);
}

/*
 * 9141 init
 */
int 
do_l2_9141_start(int destaddr)
{
    struct diag_l2_conn *d_conn;

    d_conn = do_l2_common_start(DIAG_L1_ISO9141, DIAG_L2_PROT_ISO9141,
        DIAG_L2_TYPE_SLOWINIT, set_speed, (uint8_t)destaddr,
        set_testerid);

    if (d_conn == NULL)
        return(-1);

    /* Connected ! */
    global_l2_conn = d_conn;

    return(0);
}

/*
 * 14120 init
 */
int
do_l2_14230_start(int init_type)
{
    struct diag_l2_conn *d_conn;
    flag_type flags = 0;

    if (set_addrtype == 1)
        flags = DIAG_L2_TYPE_FUNCADDR;
    else
        flags = 0;
    flags |= DIAG_L2_IDLE_J1978;    /* Use J1978 idle msgs */

    flags |= (init_type & DIAG_L2_TYPE_INITMASK) ;

    d_conn = do_l2_common_start(DIAG_L1_ISO14230, DIAG_L2_PROT_ISO14230,
        flags, set_speed, set_destaddr, set_testerid);

    if (d_conn == NULL)
        return(-1);

    /* Connected ! */
    global_l2_conn = d_conn;

    return(0);
}

/*
 * J1850 init, J1850 interface type passed as l1_type
 */
static int
do_l2_j1850_start(int l1_type)
{
    flag_type flags = 0;
    struct diag_l2_conn *d_conn;

    d_conn = do_l2_common_start(l1_type, DIAG_L2_PROT_SAEJ1850,
        flags, set_speed, 0x6a, set_testerid);

    if (d_conn == NULL)
        return(-1);

    /* Connected ! */
    global_l2_conn = d_conn;

    return(0);
}

/*
 * Generic init, using parameters set by user
 */
int
do_l2_generic_start(void)
{
    struct diag_l2_conn *d_conn;
    struct diag_l0_device *dl0d;
    int rv;
    flag_type flags = 0;

    rv = diag_init();
    if (rv != 0)
    {
        fprintf(stderr, "diag_init failed\n");
        return diag_iseterr(rv);
    }

    /* Open interface using hardware type ISO14230 */
    dl0d = diag_l2_open(set_interface, set_subinterface, set_L1protocol);
    if (dl0d == 0)		//indicating an error
    {
        rv = diag_geterr();
        //if ((rv != DIAG_ERR_BADIFADAPTER) && (rv != DIAG_ERR_PROTO_NOTSUPP))
            fprintf(stderr, "Failed to open hardware interface protocol %d with %s on %s\n",
                set_L1protocol,set_interface,set_subinterface);
        return diag_iseterr(rv);
    }

    if (set_addrtype == 1)
        flags = DIAG_L2_TYPE_FUNCADDR;
    else
        flags = 0;

    flags |= (set_initmode & DIAG_L2_TYPE_INITMASK) ;

    d_conn = diag_l2_StartCommunications(dl0d, set_L2protocol,
        flags, set_speed, set_destaddr, set_testerid);

    if (d_conn == NULL)
    {
	rv=diag_geterr();
        diag_l2_close(dl0d);
        return diag_iseterr(rv);
    }

    /* Connected ! */
    
    global_l2_conn = d_conn;
    global_l2_dl0d = dl0d;    /* Saved for close */

    return(0);
}

/*
 * Gets the data for every supported test
 *
 * Returns <0 on failure, 0 on good and 1 on interrupted
 *
 * If Interruptible is 1, then this is interruptible by the stdin
 * becoming ready for read (using diag_os_ipending())
 *
 * It is used in "Interuptible" mode when doing "monitor" command
 */
int
do_j1979_getdata(int interruptible)
{
    unsigned int i,j;
    int rv;
    struct diag_l3_conn *d_conn;
    ecu_data_t *ep;
    struct diag_msg *msg;

    d_conn = global_l3_conn;

    /*
     * Now get all the data supported
     */
    for (i=3; i<0x100; i++)
    {
        if (merged_mode1_info[i])
        {
	    fprintf(stderr, "Requesting Mode 1 Pid 0x%02x...\n", i);
            rv = l3_do_j1979_rqst(d_conn, 0x1, (int)i, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);
            if (rv < 0)
            {
                fprintf(stderr, "Mode 1 Pid 0x%02x request failed (%d)\n",
                    i, rv);
            }
            else
            {
                msg = find_ecu_msg(0, 0x41);
                if (msg == NULL)
                    fprintf(stderr, "Mode 1 Pid 0x%02x request no-data (%d)\n",
                    i, rv);
            }

            if (interruptible)
            {
                if (diag_os_ipending(fileno(stdin)))
                    return(1);
            }
        }
    }

    /* Get mode2/pid2 (DTC that caused freezeframe) */
    fprintf(stderr, "Requesting Mode 0x02 Pid 0x02 (Freeze frame DTCs)...\n", i);
    rv = l3_do_j1979_rqst(d_conn, 0x2, 2, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);

    if (rv < 0) 
    {
        fprintf(stderr, "Mode 0x02 Pid 0x02 request failed (%d)\n", rv);
        return(0);
    }
    msg = find_ecu_msg(0, 0x42);
    if (msg == NULL)
    {
        fprintf(stderr, "Mode 0x02 Pid 0x02 request no-data (%d)\n", rv);
        return(0);
    }

    /* Now go thru the ECUs that have responded with mode2 info */
    for (j=0, ep=ecu_info; j<ecu_count; j++, ep++)
    {
        if ( (ep->mode1_data[2].type == TYPE_GOOD) &&
            (ep->mode1_data[2].data[2] |
                ep->mode1_data[2].data[3]) )
        {
            for (i=3; i<=0x100; i++)
            {
                if (ep->mode2_info[i])
                {
		    fprintf(stderr, "Requesting Mode 0x02 Pid 0x%02x...\n", i);
                    rv = l3_do_j1979_rqst(d_conn, 0x2, (int)i, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);
                    if (rv < 0)
                    {
                        fprintf(stderr, "Mode 0x02 Pid 0x%02x request failed (%d)\n", i, rv);
                    }
                    msg = find_ecu_msg(0, 0x42);
                    if (msg == NULL)
                    {
                        fprintf(stderr, "Mode 0x02 Pid 0x%02x request no-data (%d)\n", i, rv);
                        return(0);
                    }
                    
                }
                if (interruptible)
                {
                    if (diag_os_ipending(fileno(stdin)))
                        return(1);
                }
            }
        }
    }
    return(0);
}

/*
 * Find out basic info from the ECU (what it supports, DTCs etc)
 *
 * This is the basic work horse routine
 */
void
do_j1979_basics()
{
    struct diag_l3_conn *d_conn;
    ecu_data_t *ep;
    unsigned int i;
    int o2monitoring = 0;

    d_conn = global_l3_conn;

    /*
     * Get supported PIDs and Tests etc
     */
    do_j1979_getpids();

    global_state = STATE_SCANDONE ;

    /*
     * Get current DTCs/MIL lamp status/Tests supported for this ECU
     * and test, and wait for those tests to complete
     */
    do_j1979_getdtcs();

    /*
     * Get data supported by ECU, non-interruptibly
     */
    do_j1979_getdata(0);

    /*
     * And now do stuff with that data
     */
    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if ( (ep->mode1_data[2].type == TYPE_GOOD) &&
            (ep->mode1_data[2].data[2] | ep->mode1_data[2].data[3]) )
        {
            fprintf(stderr, "ECU %d Freezeframe data exists, caused by DTC ",
                i);
            print_single_dtc(ep->mode1_data[2].data[2] , ep->mode1_data[2].data[3]);
            fprintf(stderr, "\n");
        }

        if (ep->mode1_data[0x1c].type == TYPE_GOOD)
        {
            fprintf(stderr, "ECU %d is ", i);
            switch(ep->mode1_data[0x1c].data[2])
            {
            case 1:
                fprintf(stderr, "OBD II (California ARB)");
                break;
            case 2:
                fprintf(stderr, "OBD (Federal EPA)");
                break;
            case 3:
                fprintf(stderr, "OBD and OBD II");
                break;
            case 4:
                fprintf(stderr, "OBD I");
                break;
            case 5:
                fprintf(stderr, "not OBD");
                break;
            case 6:
                fprintf(stderr, "EOBD (Europe)");
                break;
            default:
                fprintf(stderr, "unknown (%d)", ep->mode1_data[0x1c].data[2]);
                break;
            }
            fprintf(stderr, " compliant\n");
        }

        /*
         * If ECU supports Oxygen sensor monitoring, then do O2 sensor
         * stuff
         */
        if ( (ep->mode1_data[1].type == TYPE_GOOD) &&
            (ep->mode1_data[1].data[4] & 0x20) )
        {
            o2monitoring = 1;
        }
    }
    do_j1979_getO2sensors();
    if (o2monitoring > 0)
    {
        do_j1979_O2tests();
    }
    else
    {
        fprintf(stderr, "Oxygen (O2) sensor monitoring not supported\n");
    }
}

int
print_single_dtc(databyte_type d0, databyte_type d1)
{
    char buf[256];

    uint8_t db[2];
    db[0] = d0;
    db[1] = d1;

    /*
     * XXX Another place where a decode can just get a file pointer
     * and not a buffer and size
     */
    fprintf(stderr, "%s", 
        diag_dtc_decode(db, 2, set_vehicle, set_ecu, dtc_proto_j2012, buf,
        sizeof(buf)));

    return (0);
}

static void
print_dtcs(uint8_t *data)
{
    /* Print the DTCs just received */
    int i, j;

    for (i=0, j=1; i<3; i++, j+=2)
    {
        if ((data[j]==0) && (data[j+1]==0))
            continue;
        print_single_dtc(data[j], data[j+1]);
    }
}

/*
 * Get test results for constantly monitored systems
 */
void
do_j1979_cms()
{
    int rv;
    unsigned int i;
    struct diag_l3_conn *d_conn;
    struct diag_msg *msg;

    d_conn = global_l3_conn;

    fprintf(stderr, "Requesting Mode 7 (Current cycle emission DTCs)...\n");
    rv = l3_do_j1979_rqst(d_conn, 0x07, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);
    if (rv == DIAG_ERR_TIMEOUT)
    {
        /* Didn't get a response, this is valid if there are no DTCs */
        fprintf(stderr, "No DTCs stored.\n");
        return;
    }
    if (rv != 0)
    {
        fprintf(stderr, "Failed to get test results for continuously monitored systems\n");
        return;
    }

    fprintf(stderr, "Currently monitored DTCs: ");

    for (i=0; i<ecu_count;i++)
    {
        for (msg=ecu_info[i].rxmsg; msg; msg=msg->next)
        {
            print_dtcs(msg->data);
        }

    }

    fprintf(stderr, "\n");
    return;
}


/*
 * Get test results for non-constantly monitored systems
 */
void
do_j1979_ncms(int printall)
{
    int rv;
    struct diag_l3_conn *d_conn;
    unsigned int i, j;
    int supported;
    ecu_data_t *ep;

    uint8_t merged_mode6_info[0x100];

    d_conn = global_l3_conn;

    /* Merge all ECU mode6 info into one place*/
    memset(merged_mode6_info, 0, sizeof(merged_mode6_info));
    for (i=0, ep=ecu_info, supported = 0; i<ecu_count; i++, ep++)
    {
        for (j=0; j<sizeof(ep->mode6_info);j++)
            merged_mode6_info[j] |= ep->mode6_info[j] ;
        if (ep->mode6_info[0] != 0)
            supported = 1;
    }

    if (merged_mode6_info[0] == 0x00)
    {
        /* Either not supported, or tests havent been done */
        do_j1979_getmodeinfo(6, 3);
    }
    
    if (merged_mode6_info[0] == 0x00)
    {
        fprintf(stderr, "ECU doesn't support non-continuously monitored system tests\n");
        return;
    }

    /*
     * Now do the tests
     */
    for (i=0 ; i < 60; i++)
    {
        if ((merged_mode6_info[i]) && ((i & 0x1f) != 0))
        {
            /* Do test */
	    fprintf(stderr, "Requesting Mode 6 TestID 0x%02x...\n", i);
            rv = l3_do_j1979_rqst(d_conn, 6, (int)i, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00,
                (void *)(printall?RQST_HANDLE_NCMS:RQST_HANDLE_NCMS2));
            if (rv < 0)
            {
                fprintf(stderr, "Mode 6 Test ID 0x%d failed\n", i);
            }
        }
    }
    return;
}

/*
 * Get mode info
 */
void
do_j1979_getmodeinfo(int mode, int response_offset)
{
    int rv;
    struct diag_l3_conn *d_conn;
    int pid;
    unsigned int i, j;
    ecu_data_t *ep;
    int not_done;
    uint8_t *data;
    
    d_conn = global_l3_conn;

    /*
     * Test 0, 0x20, 0x40, 0x60 (etc) for each mode returns information
     * as to which tests are supported. Test 0 will return a bitmask 4
     * bytes long showing which of the tests 0->0x1f are supported. Test
     * 0x20 will show 0x20->0x3f etc
     */
    for (pid = 0; pid < 0x100; pid += 0x20)
    {
        /*
         * Do Mode 'mode' Pid 'pid' request to find out
         * what is supported
         */
        fprintf(stderr, "Exploring Mode 0x%02x supported PIDs (block 0x%02x)...\n", mode, pid);
        rv = l3_do_j1979_rqst(d_conn, mode, pid, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);
        if (rv != 0)
        {
            /* No response */
            break;
        }

        /* Process the results */
        for (j=0, ep=ecu_info, not_done = 0; j<ecu_count; j++, ep++)
        {
            if (ep->rxmsg == NULL)
                continue;
            if (ep->rxmsg->data[0] != (mode + 0x40))
                continue;

            /* Valid response for this request */

            /* Sort out where to store the received data */
            switch (mode)
            {
            case 1:
                data = ep->pids;
                break;
            case 2:
                data = ep->mode2_info;
                break;
            case 5:
                data = ep->mode5_info;
                break;
            case 6:
                data = ep->mode6_info;
                break;
            case 8:
                data = ep->mode8_info;
                break;
            case 9:
                data = ep->mode9_info;
                break;
            default:
                data = NULL;
                break;
            }
            if (data == NULL)
                break;

            data[0] = 1;    /* Pid 0, 0x20, 0x40 always supported */
            for (i=0 ; i<=0x20; i++)
            {
                if (l2_check_pid_bits(&ep->rxmsg->data[response_offset], (int)i))
                    data[i + pid] = 1;
            }
            if (data[0x20 + pid] == 1)
                not_done = 1;
        }

        /* Now, check if all ECUs said the next pid isnt supported */
        if (not_done == 0)
            break;
    }
    return;
}


/*
 * Get the supported PIDs and Tests (Mode 1, 2, 5, 6, 9)
 *
 * This doesnt get the data for those pids, just the info as to
 * what the ECU supports
 */
void
do_j1979_getpids()
{
    struct diag_l3_conn *d_conn;
    ecu_data_t *ep;
    unsigned int i, j;
    
    d_conn = global_l3_conn;

    do_j1979_getmodeinfo(1, 2);
    do_j1979_getmodeinfo(2, 2);
    do_j1979_getmodeinfo(5, 3);
    do_j1979_getmodeinfo(6, 3);
    do_j1979_getmodeinfo(8, 2);
    do_j1979_getmodeinfo(9, 3);

    /*
     * Combine all the supported Mode1 PIDS
     * from the ECUs into one bitmask, do same
     * for Mode5
     */
    memset(merged_mode1_info, 0, sizeof(merged_mode1_info));
    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        for (j=0; j<sizeof(ep->pids);j++)
            merged_mode1_info[j] |= ep->pids[j] ;
    }

    memset(merged_mode5_info, 0, sizeof(merged_mode5_info));
    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        for (j=0; j<sizeof(ep->mode5_info);j++)
            merged_mode5_info[j] |= ep->mode5_info[j] ;
    }
    return;
}

/*
 * Do the O2 tests for this O2 sensor
 */
void
do_j1979_O2tests()
{
    int i;

    if (merged_mode5_info[0] == 0)
    {
        fprintf(stderr, "Oxygen (O2) sensor tests not supported\n");
        return;
    }

    for (i=0; i<=7; i++)
    {
        if (global_O2_sensors & (1<<i))
            do_j1979_getO2tests(i);
    }
    return;
}


/*
 * Do O2 tests for O2Sensor
 *
 * O2sensor is the bit number
 */
void
do_j1979_getO2tests(int O2sensor)
{

    int rv;
    struct diag_l3_conn *d_conn;
    int i;

    uint8_t o2s = 1<<O2sensor ;

    d_conn = global_l3_conn;

    for (i=1 ; i<=0x1f; i++)
    {
        fprintf(stderr, "O2 Sensor %d Tests: -\n", O2sensor);
        if ((merged_mode5_info[i]) && ((i & 0x1f) != 0))
        {
            /* Do test for of i + testID */
	    fprintf(stderr, "Requesting Mode 0x05 TestID 0x%02x...\n", i);
            rv = l3_do_j1979_rqst(d_conn, 5, i, o2s,
                0x00, 0x00, 0x00, 0x00, 0x00,
                    (void *)RQST_HANDLE_O2S);
            if ((rv < 0) || (find_ecu_msg(0, 0x45)==NULL))
            {
                fprintf(stderr, "Mode 5 Test ID 0x%d failed\n", i);
            }
            /* Receive routine will have printed results */
        }
    }
}

/*
 * Get current DTCs/MIL lamp status/Tests supported for this ECU
 * and test, and wait for those tests to complete
 */
int
do_j1979_getdtcs()
{
    int rv;
    struct diag_l3_conn *d_conn;
    struct diag_msg *msg;
    ecu_data_t *ep;
    unsigned int i;
    int num_dtcs, readiness, mil;

    d_conn = global_l3_conn;

    if (merged_mode1_info[1] == 0)
    {
        fprintf(stderr, "ECU(s) do not support DTC#/test query - can't do tests\n");
        return(0);
    }

    fprintf(stderr, "Requesting Mode 0x01 PID 0x01 (Current DTCs)...\n", i);
    rv = l3_do_j1979_rqst(d_conn, 1, 1, 0,
            0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);

    if ((rv < 0) || (find_ecu_msg(0, 0x41)==NULL))
    {
        fprintf(stderr, "Mode 1 Pid 1 request failed %d\n", rv);
        return(-1);
    }

    /* Go thru the received messages, and see readiness/MIL light */
    mil = 0; readiness = 0, num_dtcs = 0;

    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x41))
        {
            /* Go thru received msgs looking for DTC responses */
            if ( (ep->mode1_data[1].data[3] & 0xf0) ||
                ep->mode1_data[1].data[5] )
                    readiness = 1;

            if (ep->mode1_data[1].data[2] & 0x80)
                mil = 1;

            num_dtcs += ep->mode1_data[1].data[2] & 0x7f;
        }

    }
    if (readiness == 1)
        fprintf(stderr, "Not all readiness tests have completed\n");
    if (mil == 1)
        fprintf(stderr, "MIL light ON, ");
    else
        fprintf(stderr, "MIL light OFF, ");

    fprintf(stderr, "%d stored DTC%c\n", num_dtcs, (num_dtcs==1)?' ':'s');
    
    if (num_dtcs)
    {
        /*
         * Do Mode3 command to get DTCs
         */

        fprintf(stderr, "Requesting Mode 0x03 (Emission DTCs)...\n");
        rv = l3_do_j1979_rqst(d_conn, 3, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);
        if ((rv < 0) || (find_ecu_msg(0, 0x43)==NULL))
        {
            fprintf(stderr, "ECU would not return DTCs\n");
            return(-1);
        }

        /* Go thru received msgs looking for DTC responses */
        for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
        {
            if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x43))
            {
                for (msg=ep->rxmsg; msg; msg=msg->next)
                {
                    print_dtcs(ep->rxmsg->data);
                }
                fprintf(stderr, "\n");
            }
        }
    }
    return(0);
}

/*
 * Get supported DTCS
 */
int
do_j1979_getO2sensors()
{
    int rv;
    struct diag_l3_conn *d_conn;
    unsigned int i, j;
    int num_sensors;
    ecu_data_t *ep;

    d_conn = global_l3_conn;

    global_O2_sensors = 0;
    num_sensors = 0;

    fprintf(stderr, "Requesting Mode 0x01 PID 0x13 (O2 sensors location)...\n");
    rv = l3_do_j1979_rqst(d_conn, 1, 0x13, 0,
                0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);

    if ((rv < 0) || (find_ecu_msg(0, 0x41)==NULL))
    {
        fprintf(stderr, "Mode 1 Pid 0x13 request failed %d\n", rv);
        return(0);
    }

    for (i=0, ep=ecu_info; i<ecu_count; i++, ep++)
    {
        if ((ep->rxmsg) && (ep->rxmsg->data[0] == 0x41))
        {
            /* Maintain bitmap of sensors */
            global_O2_sensors |= ep->rxmsg->data[2];
            /* And count additional sensors on this ECU */
            for (j=0; j<=7; j++)
            {
                if (ep->rxmsg->data[2] & (1<<j))
                    num_sensors++;
            }
        }
    }

    fprintf(stderr, "%d Oxygen (O2) sensors in vehicle\n", num_sensors);

    return(0);
}

int
diag_cleardtc(void)
{
    /* Clear DTCs */
    struct diag_l3_conn *d_conn;
    int rv;
    struct diag_msg    *rxmsg;

    d_conn = global_l3_conn;
    fprintf(stderr, "Requesting Mode 0x04 (Clear DTCs)...\n");
    rv = l3_do_j1979_rqst(d_conn, 0x04, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, (void *)0);

    rxmsg = find_ecu_msg(0, 0x44);

    if (rxmsg == NULL)
    {
        fprintf(stderr, "ClearDTC requested failed - no appropriate response\n");
        return(-1);
    }

    return (rv);
}

typedef int (start_fn)(int);

struct protocol {
    const char     *desc;
    start_fn *start;
    int      flags;
    int      protoID;
    int      conmode;
};

const struct protocol protocols[] = {
    {"SAEJ1850-VPW",  do_l2_j1850_start, DIAG_L1_J1850_VPW,     PROTOCOL_SAEJ1850, 0},
    {"SAEJ1850-PWM",  do_l2_j1850_start, DIAG_L1_J1850_PWM,     PROTOCOL_SAEJ1850, 0},
    {"ISO14230_FAST", do_l2_14230_start, DIAG_L2_TYPE_FASTINIT, PROTOCOL_ISO14230, DIAG_L2_TYPE_FASTINIT},
    {"ISO9141",       do_l2_9141_start,  0x33,                  PROTOCOL_ISO9141,  DIAG_L2_TYPE_SLOWINIT},
    {"ISO14230_SLOW", do_l2_14230_start, DIAG_L2_TYPE_SLOWINIT, PROTOCOL_ISO14230, DIAG_L2_TYPE_SLOWINIT},
};

/*
 * Connect to ECU by trying all protocols
 * - We do the fast initialising protocols before the slow ones
 */
int
ecu_connect(void)
{
    int connected=0;
    int rv = -1;
    const struct protocol *p;

    fprintf(stderr, "\n");

    for (p = protocols; !connected && p < &protocols[ARRAY_SIZE(protocols)]; p++)
    {
        fprintf(stderr,"Trying %s:\n", p->desc);
        rv = p->start(p->flags);
        if (rv == 0)
        {
                global_conmode = p->conmode;
            global_protocol = p->protoID;
            connected = 1;
                fprintf(stderr, "%s Connected.\n", p->desc);
        }
        else
                fprintf(stderr, "%s Failed!\n", p->desc);

	fprintf(stderr, "\n");
    }

    fprintf(stderr, "\n");

    /*
     * Connected, now add J1979 protocol
     */
    if (connected)
    {
        struct diag_l3_conn *d_l3_conn;

        global_state = STATE_CONNECTED;

        d_l3_conn = diag_l3_start("SAEJ1979", global_l2_conn);
        if (d_l3_conn == NULL)
        {
            fprintf(stderr, "Failed to enable SAEJ1979 mode\n");
            rv = -1;
        }
        global_l3_conn = d_l3_conn;

        global_state = STATE_L3ADDED;

    }

    if (diag_cmd_debug > 0)
        fprintf(stderr, "debug: L2 connection ID %p, L3 ID %p\n",
            global_l2_conn, global_l3_conn);

    return (rv);
}

/*
 * Initialise
 */
static int
do_init(void)
{
    clear_data();

    return(0);
}

/*
 * Explain command line usage
 */
static void do_usage ()
{
    fprintf ( stderr, "FreeDiag ScanTool:\n\n" ) ;
    fprintf ( stderr, "  Usage -\n" ) ;
    fprintf ( stderr, "    scantool [-h][-a|-c]\n\n" ) ;
    fprintf ( stderr, "  Where:\n" ) ;
    fprintf ( stderr, "    -h   -- Display this help message\n" ) ;
    fprintf ( stderr, "    -a   -- Start in Application/Interface mode\n" ) ;
    fprintf ( stderr, "            (some other program provides the\n" ) ;
    fprintf ( stderr, "            user interface)\n" ) ;
    fprintf ( stderr, "    -c   -- Start in command-line interface mode\n" ) ;
    fprintf ( stderr, "            (this is the default)\n" ) ;
    fprintf ( stderr, "\n" ) ;
}


#ifdef WIN32
static void
format_o2(char *buf,
int english,
const struct pid *p,
response_t *data,
int n)
#else
static void
format_o2(char *buf,
int english __attribute__((unused)),
const struct pid *p,
response_t *data,
int n)
#endif
{
        double v = DATA_SCALED(p, DATA_1(p, n, data));
        int t = DATA_1(p, n + 1, data);

        if (t == 0xff)
                sprintf(buf, p->fmt1, v);
        else
                sprintf(buf, p->fmt2, v, t * p->scale2 + p->offset2);
}

#ifdef WIN32
static void
format_aux(char *buf,
int english,
const struct pid *p,
response_t *data,
int n)
#else
static void
format_aux(char *buf,
int english __attribute__((unused)),
const struct pid *p,
response_t *data,
int n)
#endif
{
        sprintf(buf, (DATA_RAW(p, n, data) & 1) ? "PTO Active" : "----");
}


#ifdef WIN32
static void
format_fuel(char *buf,
int english,
const struct pid *p,
response_t *data,
int n)
#else
static void
format_fuel(char *buf,
int english __attribute__((unused)),
const struct pid *p,
response_t *data,
int n)
#endif
{
        int s = DATA_1(p, n, data);

        switch (s) {
            case 1 << 0: sprintf(buf, "Open");          break;
            case 1 << 1: sprintf(buf, "Closed");        break;
            case 1 << 2: sprintf(buf, "Open-Driving");  break;
            case 1 << 3: sprintf(buf, "Open-Fault");    break;
            case 1 << 4: sprintf(buf, "Closed-Fault");  break;
            default:     sprintf(buf, "Open(rsvd)");    break;
        }

        /* XXX Fuel system 2 status */
}


static void
format_data(char *buf, int english, const struct pid *p, response_t *data, int n)
{
        double v;

        v = DATA_SCALED(p, DATA_RAW(p, n, data));
        if (english && p->fmt2)
                sprintf(buf, p->fmt2, DATA_ENGLISH(p, v));
        else
                sprintf(buf, p->fmt1, v);
}


/* conversion factors from the "units" package */

static const struct pid pids[] = {
    {0x03, "Fuel System Status", format_fuel, 2,
		"", 0.0, 0.0,
		"", 0.0, 0.0},
    {0x04, "Calculated Load Value", format_data, 1,
		"%5.1f%%", (100.0/255), 0.0,
		"", 0.0, 0.0},
	{0x05, "Engine Coolant Temperature", format_data, 1,
		"%3.0fC",    1,          -40,
		"%3.0fF",      1.8, 32},
    {0x06, "Short term fuel trim Bank 1", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
    {0x07, "Long  term fuel trim Bank 1", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
    {0x08, "Short term fuel trim Bank 2", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
    {0x09, "Long  term fuel trim Bank 2", format_data, 1,
		"%5.1f%%", (100.0/128), -100,
		"", 0.0, 0.0},
    {0x0a, "Fuel Pressure", format_data, 1,
		"%3.0fkPaG", 3.0,            0.0,
		"%4.1fpsig",   0.14503774, 0.0},
    {0x0b, "Intake Manifold Pressure", format_data, 1,
		"%3.0fkPaA", 1.0,            0.0,
		"%4.1finHg",   0.29529983, 0.0},
    {0x0c, "Engine RPM", format_data, 2,
		"%5.0fRPM",  0.25, 0.0,
		"", 0.0, 0.0},
    {0x0d, "Vehicle Speed", format_data, 1,
		"%3.0fkm/h", 1.0,            0.0,
		"%3.0fmph",    0.62137119, 0.0},
    {0x0e, "Ignition timing advance Cyl #1", format_data, 1,
		"%4.1f deg", 0.5,        -64.0,
		"", 0.0, 0.0},
    {0x0f, "Intake Air Temperature", format_data, 1,
		"%3.0fC",    1.0,          -40.0,
		"%3.0fF",      1.8, 32.0},
    {0x10, "Air Flow Rate", format_data, 2,
		"%6.2fgm/s", 0.01,         0.0,
		"%6.1flb/min", 0.13227736, 0.0},
    {0x11, "Absolute Throttle Position", format_data, 1,
		"%5.1f%%", (100.0/255), 0.0,
		"", 0.0, 0.0},
    /* XXX 0x12 Commanded secondary air status */
    /* XXX 0x13 Oxygen sensor locations */
    {0x14, "Bank 1 Sensor 1 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x15, "Bank 1 Sensor 2 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x16, "Bank 1 Sensor 3 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x17, "Bank 1 Sensor 4 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x18, "Bank 2 Sensor 1 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x19, "Bank 2 Sensor 2 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x1a, "Bank 2 Sensor 3 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x1b, "Bank 2 Sensor 4 Voltage/Trim", format_o2,   2,
		"%5.3fV",    0.005,        0.0,
		"%5.3fV/%5.1f%%", (100.0/128), -100.0},
    {0x1e, "Auxiliary Input Status", format_aux,  1,
		"", 0.0, 0.0,
		"", 0.0, 0.0},
};


const struct pid *get_pid ( int i )
{
  if ( i < 0 || i >= ARRAY_SIZE(pids) )
    return NULL ;

  return & pids [ i ] ;
}


/*
 * Main
 */
#ifdef WIN32
int
main(int argc, char **argv)
#else
int
main(int argc __attribute__((unused)), char **argv)
#endif
{
    int user_interface = 1 ;
    int i ;

    for ( i = 1 ; i < argc ; i++ )
    {
        if ( argv [ i ][ 0 ] == '-' || argv [ i ][ 0 ] == '+' )
            switch ( argv [ i ][ 1 ] )
            {
                case 'c' : user_interface = 1 ; break ;
                case 'a' : user_interface = 0 ; break ;
                case 'h' : do_usage () ; exit ( 0 ) ;
                default  : do_usage () ; exit ( 1 ) ;
            }
        else
        {
          do_usage () ;
          exit ( 1 ) ;
        }
    }

    /* Input buffer */

    do_init();

    progname = strrchr(argv[0], '/');
    progname = (progname)?(progname+1):argv[0];

    if ( user_interface )
      enter_cli ( progname ) ;
    else
      enter_aif ( progname ) ;

    /* Done */
    exit(0);
}

#ifdef WIN32
int
cmd_up(int argc, char **argv)
#else
int
cmd_up(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
    return (CMD_UP);
}

#ifdef WIN32
int
cmd_exit(int argc, char **argv)
#else
int
cmd_exit(int argc __attribute__((unused)), char **argv __attribute__((unused)))
#endif
{
	return (CMD_EXIT);
}

