#ifndef _DIAG_L3_H_
#define _DIAG_L3_H_

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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *************************************************************************
 *
 * L3 header.
 *
 */

#if defined(__cplusplus)
extern "C" {
#endif

struct diag_l2_conn;
struct diag_msg;

/*
 * Layer 3 connection info
 */
struct diag_l3_conn
{
	struct diag_l2_conn	*d_l3l2_conn;
	int d_l3l2_flags;		/* Flags from L2 */
	uint32_t d_l3l1_flags;		/* Flags from L1 */

	int diag_l3_speed;		/* Speed */

	const struct diag_l3_proto *d_l3_proto;

	/* Callback into next layer (which passed us the handle) */
	void (*callback)(void *handle, struct diag_msg *msg);
	void *handle;

	/* Data buffer, and offset into it */
	uint8_t rxbuf[MAXRBUF];	/* Receive data buffer */
	int	rxoffset;

	/* Received messages */
	struct diag_msg	*msg;

	// Count in ms since an arbitrary reference (from diag_os_getms())
	unsigned long timer;

	/* Linked list held by main L3 code */
	struct diag_l3_conn	*next;


	/* Source address this connection is using */
	uint8_t	src;

};

/*
 * L3 Protocol look up table
 */

typedef struct diag_l3_proto
{
	const char *proto_name;

	//start, stop : initiate L3 comms, filling the given diag_l3_conn
	int (*diag_l3_proto_start)(struct diag_l3_conn *);
	int (*diag_l3_proto_stop)(struct diag_l3_conn *);
	//proto_recv: ret 0 if ok?
	int (*diag_l3_proto_send)(struct diag_l3_conn *, struct diag_msg *);
	//proto_recv: ret 0 if ok?
	int (*diag_l3_proto_recv)(struct diag_l3_conn *, int,
		void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *);
	int (*diag_l3_proto_ioctl)(struct diag_l3_conn *, int cmd, void *data);
	//proto_request : send request and return a new message with the reply.
	struct diag_msg * (*diag_l3_proto_request)(struct diag_l3_conn*,
		struct diag_msg* txmsg, int* errval);

/* Pretty text decode routine */
	char *(*diag_l3_proto_decode)(struct diag_l3_conn *,
		struct diag_msg *msg,
		char * buf,
		const size_t bufsize);

	/* Timer */
	//this function, if it exists, is called every time diag_l3_timer()
	//is called from the periodic callback (in diag_os); the ms argument
	//is the difference (in ms) between [now] and [diag_l3_conn->timer].
	//ret 0 if ok
	int (*diag_l3_proto_timer)(struct diag_l3_conn *, unsigned long ms);

} diag_l3_proto_t;


//diag_l3_start must free() everything if it fails;
struct diag_l3_conn * diag_l3_start(const char *protocol, struct diag_l2_conn *d_l2_conn);
//diag_l3_stop must free() everything diag_l3_start alloc()ed
int	diag_l3_stop(struct diag_l3_conn *d_l3_conn);
int	diag_l3_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg);
int	diag_l3_recv(struct diag_l3_conn *d_l3_conn, int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle);
char * diag_l3_decode(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg,
	char *buf, const size_t bufsize);
//_request : send the request in txmsg and return a new msg with the response.
//Caller must free that msg
struct diag_msg *diag_l3_request(struct diag_l3_conn *dl3c, struct diag_msg *txmsg,
		int *errval);

/* Base implementations:
 * these are defined in diag_l3.c and perform no operation.
 */
int diag_l3_base_start(struct diag_l3_conn *);
int diag_l3_base_stop(struct diag_l3_conn *);
int diag_l3_base_send(struct diag_l3_conn *, struct diag_msg *);
int diag_l3_base_recv(struct diag_l3_conn *, int,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *);
//XXX diag_l3_base_ioctl : there's no code for this one ?
//int diag_l3_base_ioctl(struct diag_l3_conn *, int cmd, void *data);
struct diag_msg * diag_l3_base_request(struct diag_l3_conn *dl3c,
	struct diag_msg* txmsg, int* errval);


/* Pretty text decode routine */
char *diag_l3_proto_decode(struct diag_l3_conn *, struct diag_msg *);

/* Regular timer routine: this is called regularly (diag_os) */
// and calls the diag_l3_proto_timer function of every
// diag_l3_conn in the diag_l3_list linked-list.
void diag_l3_timer(void);

// diag_l3_ioctl() : calls the diag_l3_proto_ioctl of the specified
// diag_l3_conn , AND its diag_l2_ioctl !? XXX why both ?
int diag_l3_ioctl(struct diag_l3_conn *connection, unsigned int cmd, void *data);

// diag_l3_debug : contains debugging message flags (see diag.h)
extern int diag_l3_debug;
extern struct diag_l3_conn *global_l3_conn;

#if defined(__cplusplus)
}
#endif
#endif
