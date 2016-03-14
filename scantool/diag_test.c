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
 * Diag library test harness
 * This is a stand-alone program !
 * Hard-coded to use a DUMB interface on the current device with "DIAG_L1_RAW" proto...
  */

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "diag_err.h"
#include "diag_os.h"
#include "diag_iso14230.h"
#include "diag_tty.h"
#include "diag_l1.h"
#include "diag_l2.h"


#define BAUD 5
#define SLOWINIT
#define DO_ISO


#define TESTER_ID	0xF1

static int
do_l2_raw_test(int funcaddr, target_type destecu, int inittype);

uint8_t global_data[MAXRBUF];
int global_datalen;
char *port;

uint8_t	global_pids[0x100];	/* Pids supported by ECU */

int
main(int argc,  char **argv)
{

	if (argc != 2) {
		//must be called with one parameter
		printf("Error : correct usage is : %s [dev]\nwhere [dev] looks like /dev/ttyS0 etc.\n",argv[0]);
		return 0;
	}
	port=argv[1];	//point to the device name string

	diag_l0_debug = -1;
	diag_l1_debug = -1;
	diag_l2_debug = -1;

	if (diag_init()) {
		printf("error with diag_init\n");
		return 0;
	}

	target_type i;
	for (i=33 ; i < 0xFF; i++)
	{
		if (do_l2_raw_test(0, i, DIAG_L2_TYPE_SLOWINIT)) {
			printf("do_l2_raw_test error. Exiting.\n");
			return 0;
		}
	}

	return 0;
}


static void
l2_rcv(UNUSED(void *handle), struct diag_msg *msg)
{
	size_t len = msg->len;
	uint8_t *data = msg->data;

	/* Store the data */
	memcpy(global_data, data, len);
	global_datalen = len;

	printf("Got %ld bytes of data, src=0x%X, dest 0x%X !! - ", (long)len,
		msg->src, msg->dest);

	while (len)
	{
		printf("0x%X ", *data);
		len--; data++;
	}

	printf("\n");

}

/*
 * Layer 0 (L0) interface table
 */
//do_l2_raw_test : ret 0 if ok
static int
do_l2_raw_test(int funcaddr, target_type destecu, int inittype)
{
	int rv;
	struct diag_l2_conn *d_conn;
	struct diag_l0_device *dl0d = NULL;
	flag_type flags = 0;

#if 0	//XXX CFG_REWORK
	dl0d = diag_l2_open("DUMB", DIAG_L1_RAW);
#endif
	if (! dl0d) {
		printf("could not open %s\n",port);
		return -1;
	}
	printf("open dl0d = %p\n", (void *)dl0d);

	/*
	 * Initiate a RAW communications layer on the
	 * ISO9141 device we opened above
	 */
	if (funcaddr)
		flags |= DIAG_L2_TYPE_FUNCADDR;

	flags |= inittype;

#ifdef DO_ISO
	d_conn = diag_l2_StartCommunications(dl0d, DIAG_L2_PROT_ISO14230, flags,
		10400, destecu, 0xF1);
#else
	d_conn = diag_l2_StartCommunications(dl0d, DIAG_L2_PROT_RAW, flags,
		10400, destecu, 0xF1);
#endif


	if (d_conn) {
		printf("Connection to ECU established (con. %p)\n", (void *)d_conn);
printf("For %d %d\n", funcaddr, destecu);
	}
	else {
		diag_l2_close(dl0d);
		printf("Connection to ECU failed (con. %p)\n", (void *)d_conn);
		return -1;
	}

#ifndef DO_ISO


#ifdef SLOWINIT
	/* Now set it to 5 baud */
	ic.speed = 5;
	ic.databits = diag_databits_7;
	ic.stopbits = diag_stopbits_1;
	ic.parityflag = diag_par_n ;

	diag_l2_ioctl(d_conn, DIAG_IOCTL_SETSPEED, &ic);

	/* Send init msg */
	data[0] = destecu;
	msg.len = 1;
	msg.data = data;
#else	//not SLOWINIT

	/* Code block */
	{
		int i;
		uint8_t csum;
		struct diag_msg	msg;
		uint8_t data[100];
		struct diag_serial_settings ic;
		struct diag_l1_initbus_args in;

		memset(data, 0, sizeof(data));
		if (funcaddr)
			data[0]=0xC1;
		else
			data[0]=0x81;
		data[1]=destecu;
		data[2]=TESTER_ID;	/* Source- docs recommend F1, but my scanner uses that */
		data[3]=0x81;	/* Request */

	#define DATALEN 4

		csum = 0;
		for (i=0; i < DATALEN; i++)
			csum += data[i];
		data[DATALEN] = csum;

		msg.len = DATALEN + 1;
		msg.data = data;

		/* Do fast init stuff */
		in.type = inittype;
		diag_l2_ioctl(d_conn, DIAG_IOCTL_INITBUS, &in);
		/* And immediately send the StartCommunications msg */
	}
#endif //ifdef SLOWINIT
	diag_l2_send(d_conn, &msg);

#ifdef SLOWINIT
	/* Change to 10400 baud */
	ic.speed = 10400;
	ic.databits = diag_databits_8;
	ic.stopbits = diag_stopbits_1;
	ic.parityflag = diag_par_n ;

	diag_l2_ioctl(d_conn, DIAG_IOCTL_SETSPEED, &ic);
#endif //ifdef SLOWINIT

#endif /* ifndef DO_ISO */
	diag_os_millisleep(5000);

	/* See what we got */
	rv = diag_l2_recv(d_conn, 2000, l2_rcv, NULL);
	if (rv < 0)
		printf("diag_l2_recv returns %d\n", rv);
	else
	{
		printf("Got response, destecu %d\n", destecu);
		exit(0);
	}
	return 0;
}


UNUSED(static int do_l1_test(void));

static int
do_l1_test(void)
{
	struct diag_l0_device *dl0d = NULL;
	int rv;
	uint8_t buf[MAXRBUF];
	int rcvd;
	struct diag_serial_settings set;

#if 0	//XXX CFG_REWORK
	dl0d = diag_l1_open("DUMB", port, DIAG_L1_RAW);
#endif
	if (!dl0d)
		return diag_iseterr(DIAG_ERR_GENERAL);
	printf("open dl0d = %p\n", (void *)dl0d);

	set.speed = BAUD;
	set.databits = diag_databits_8;
	set.stopbits = diag_stopbits_2;
	set.parflag = diag_par_n;

	rv = diag_l1_setspeed(dl0d, &set);

	printf("setspeed rv = 0x%X\n", rv);

	printf("sleeping for 5\n");
	diag_os_millisleep(5000);

	diag_l1_send(dl0d, 0, "hello", 6, 4);

	printf("sent hello\n waiting for TX to finish\n");

	/* Wait for transmit to finish */
	if (BAUD < 500)
		diag_os_millisleep(15000);
	else
		diag_os_millisleep(5000);

	rcvd = diag_l1_recv(dl0d, 0, &buf[0], 100, 10);
	buf[rcvd] = 0;

	printf("got %d bytes %s\n", rcvd, buf);

	printf("sleeping for 5\n");
	diag_os_millisleep(5000);

	diag_l1_close(dl0d);
	(void) diag_os_close();

	return 0;
}
