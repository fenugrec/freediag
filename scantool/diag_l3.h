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

/** Layer 3 connection info
 */
struct diag_l3_conn {
	struct diag_l2_conn *d_l3l2_conn;
	int d_l3l2_flags;      /* Flags from L2 */
	uint32_t d_l3l1_flags; /* Flags from L1 */

	const struct diag_l3_proto *d_l3_proto;
	void *l3_int; // internal (private) data for each connection instance

	/* Callback into next layer (which passed us the handle) */
	void (*callback)(void *handle, struct diag_msg *msg);
	void *handle;

	/* Received messages */
	struct diag_msg *msg;

	/* time (in ms since an arbitrary reference) of last tx/rx , for managing periodic
	 * timers */
	unsigned long timer;

	/* Linked list held by main L3 code */
	struct diag_l3_conn *next;
};

/** L3 Protocol descriptor.
 *
 * Do not call member functions directly.
 * All functions must be implemented, except
 * 	_ioctl
 * 	_timer
 */

struct diag_l3_proto {
	const char *proto_name;

	// start, stop : initiate L3 comms, filling the given diag_l3_conn. Must return 0
	// if ok, <0 on error
	int (*diag_l3_proto_start)(struct diag_l3_conn *);
	int (*diag_l3_proto_stop)(struct diag_l3_conn *);

	// proto_send: ret 0 if ok
	int (*diag_l3_proto_send)(struct diag_l3_conn *, struct diag_msg *);

	// proto_recv: ret 0 if ok
	int (*diag_l3_proto_recv)(struct diag_l3_conn *, unsigned int,
				  void (*rcv_call_back)(void *handle, struct diag_msg *),
				  void *);

	// proto_ioctl (optional). ret 0 if ok; if not defined the ioctl is passed to L2.
	int (*diag_l3_proto_ioctl)(struct diag_l3_conn *, int cmd, void *data);

	// proto_request : send request and return a new message with the reply, and error
	// in *errval (0 if ok)
	struct diag_msg *(*diag_l3_proto_request)(struct diag_l3_conn *,
						  struct diag_msg *txmsg, int *errval);

	/* Decode msg to printable text in *buf */
	void (*diag_l3_proto_decode)(struct diag_l3_conn *, struct diag_msg *msg,
				     char *buf, const size_t bufsize);

	/* Timer (optional)
	 * If defined, this is called from diag_l3_timer()
	 * by the periodic callback (in diag_os); the ms argument
	 * is the difference (in ms) between [now] and [diag_l3_conn->timer].
	 * ret 0 if ok
	 */
	int (*diag_l3_proto_timer)(struct diag_l3_conn *, unsigned long ms);
};

/** Initialize L3 layer
 * Must be called once before using any L3 function
 */
void diag_l3_init(void);

/** De-initialize L3 layer
 * opposite of diag_l3_init(); call before unloading / exiting
 */
void diag_l3_end(void);

/** Start L3 connection
 *
 * must free() everything if it fails;
 * make sure to diag_l3_stop afterwards to free() the diag_l3_conn !
 * This adds the new l3 connection to the diag_l3_list linked-list
 */
struct diag_l3_conn *diag_l3_start(const char *protocol, struct diag_l2_conn *d_l2_conn);

/** Stop L3 connection
 *
 * must free() everything diag_l3_start alloc'd
 * and remove from diag_l3_list
 */
int diag_l3_stop(struct diag_l3_conn *d_l3_conn);

/** Send message
 *
 * @return 0 if ok
 */
int diag_l3_send(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg);

/** Receive message
 *
 * @return 0 if ok
 */
int diag_l3_recv(struct diag_l3_conn *d_l3_conn, unsigned int timeout,
		 void (*rcv_call_back)(void *handle, struct diag_msg *), void *handle);

/** Format given message as text
 *
 */
void diag_l3_decode(struct diag_l3_conn *d_l3_conn, struct diag_msg *msg, char *buf,
		    const size_t bufsize);

/** Send a request and return a new msg with the response.
 *
 * Caller must free that msg
 */
struct diag_msg *
diag_l3_request(struct diag_l3_conn *dl3c, struct diag_msg *txmsg, int *errval);

/** Send ioctl to the specified
 * L3
 *
 * ret 0 if ok
 */
int diag_l3_ioctl(struct diag_l3_conn *connection, unsigned int cmd, void *data);

/** Periodic timer dispatcher
 *
 * (call only from diag_os_*.c) *
 * This calls the diag_l3_proto_timer function of every
 * diag_l3_conn in the diag_l3_list linked-list.
 */
void diag_l3_timer(void);

/* Base implementations:
 * these are defined in diag_l3.c and perform no operation.
 */
int diag_l3_base_start(struct diag_l3_conn *);
int diag_l3_base_stop(struct diag_l3_conn *);
int diag_l3_base_send(struct diag_l3_conn *, struct diag_msg *);
int diag_l3_base_recv(struct diag_l3_conn *, unsigned int,
		      void (*rcv_call_back)(void *handle, struct diag_msg *), void *);
// XXX diag_l3_base_ioctl : there's no code for this one ?
// int diag_l3_base_ioctl(struct diag_l3_conn *, int cmd, void *data);
struct diag_msg *
diag_l3_base_request(struct diag_l3_conn *dl3c, struct diag_msg *txmsg, int *errval);

// diag_l3_debug : contains debugging message flags (see diag.h)
void diag_l3_debug_store(int d);
int diag_l3_debug_load(void);

extern struct diag_l3_conn *global_l3_conn;

/* List of supported L3 protocols; last element is NULL */
extern const struct diag_l3_proto *diag_l3_protocols[];

#if defined(__cplusplus)
}
#	endif
#endif
