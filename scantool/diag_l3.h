#ifndef _DIAG_L3_H_
#define _DIAG_L3_H_

/*
 *	freediag - Vehicle Diagnostic Utility
 *
 * CVSID $Id$
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
	int d_l3l1_flags;		/* Flags from L1 */
	
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

	/* General purpose timer */
	struct timeval	timer;

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

	int (*diag_l3_proto_start)(struct diag_l3_conn *);
	int (*diag_l3_proto_stop)(struct diag_l3_conn *);
	int (*diag_l3_proto_send)(struct diag_l3_conn *, struct diag_msg *);
	int (*diag_l3_proto_recv)(struct diag_l3_conn *, int,
		void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *);
	int (*diag_l3_proto_ioctl)(struct diag_l3_conn *, int cmd, void *data);

/* Pretty text decode routine */
	char *(*diag_l3_proto_decode)(struct diag_l3_conn *, 
		struct diag_msg *,
		char *,
		const size_t);

	/* Timer */
	void (*diag_l3_proto_timer)(struct diag_l3_conn *, int ms);

} diag_l3_proto_t;


struct diag_l3_conn * diag_l3_start(const char *protocol, struct diag_l2_conn *d_l2_conn);
int	diag_l3_stop(struct diag_l3_conn *d_l3_conn);
int	diag_l3_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg);
int	diag_l3_recv(struct diag_l3_conn *d_l3_conn, int timeout,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *handle);
char * diag_l3_decode(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg,
	char *buf, const size_t bufsize);

/* Base implementations:
 */
int diag_l3_base_start(struct diag_l3_conn *);
int diag_l3_base_stop(struct diag_l3_conn *);
int diag_l3_base_send(struct diag_l3_conn *, struct diag_msg *);
int diag_l3_base_recv(struct diag_l3_conn *, int,
	void (* rcv_call_back)(void *handle ,struct diag_msg *) , void *);
int diag_l3_base_ioctl(struct diag_l3_conn *, int cmd, void *data);

/* Pretty text decode routine */
char *diag_l3_proto_decode(struct diag_l3_conn *, struct diag_msg *);

void diag_l3_timer(void);	/* Regular timer routine */

int diag_l3_ioctl(struct diag_l3_conn *connection, int cmd, void *data);

extern int diag_l3_debug;
extern struct diag_l3_conn *global_l3_conn;

#if defined(__cplusplus)
}
#endif
#endif
