
/*
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if !defined(__VSERVICES_ETHERNET_PROTOCOL_H__)
#define __VSERVICES_ETHERNET_PROTOCOL_H__

#define VSERVICE_ETHERNET_PROTOCOL_NAME "com.ok-labs.ethernet"
typedef enum {
	VSERVICE_ETHERNET_BASE_REQ_OPEN,
	VSERVICE_ETHERNET_BASE_ACK_OPEN,
	VSERVICE_ETHERNET_BASE_NACK_OPEN,
	VSERVICE_ETHERNET_BASE_REQ_CLOSE,
	VSERVICE_ETHERNET_BASE_ACK_CLOSE,
	VSERVICE_ETHERNET_BASE_NACK_CLOSE,
	VSERVICE_ETHERNET_BASE_REQ_REOPEN,
	VSERVICE_ETHERNET_BASE_ACK_REOPEN,
	VSERVICE_ETHERNET_BASE_NACK_REOPEN,
	VSERVICE_ETHERNET_BASE_MSG_RESET,
	VSERVICE_ETHERNET_ETHERNET_MSG_PACKET,
	VSERVICE_ETHERNET_ETHERNET_MSG_ERROR,
	VSERVICE_ETHERNET_ETHERNET_MSG_UPDATE_MTU,
} vservice_ethernet_message_id_t;
typedef enum {
	VSERVICE_ETHERNET_NBIT_IN__COUNT
} vservice_ethernet_nbit_in_t;

typedef enum {
	VSERVICE_ETHERNET_NBIT_OUT__COUNT
} vservice_ethernet_nbit_out_t;

/* Notification mask macros */
#endif				/* ! __VSERVICES_ETHERNET_PROTOCOL_H__ */
