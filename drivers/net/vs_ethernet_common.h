/*
 * drivers/net/vs_ethernet_common.h
 *
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common client/server vServices Ethernet code.
 *
 */

/*
 * Note: The ethernet driver cannot currently be built for only client or
 * server if the generated protocol headers are being used.
 */
/* FIXME: Jira ticket SDK-2372 - ryanm. */

#ifndef _VS_ETHERNET_COMMON_H
#define _VS_ETHERNET_COMMON_H

#include <linux/workqueue.h>

#include <vservices/types.h>
#include <vservices/buffer.h>
#include <vservices/service.h>
#include <vservices/protocol/ethernet/types.h>
#include <vservices/protocol/ethernet/common.h>
#include <vservices/protocol/ethernet/server.h>
#include <vservices/protocol/ethernet/client.h>

struct net_device;
struct vs_eth_ops;

struct vs_eth_priv {
	union {
		struct vs_server_ethernet_state server;
		struct vs_client_ethernet_state client;
	} u;
	struct vs_service_device *service;
	struct net_device *netdev;
	bool is_server;
	struct work_struct start_work;
	struct work_struct update_mtu_work;
	const struct vs_eth_ops *ops;
	bool is_wifi;
	bool waiting_for_wifi;
	struct vs_eth_hookup *wifi_link;

	int new_mtu;
};

struct vs_eth_ops {
	struct vs_mbuf *(*alloc_packet)(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, gfp_t flags);
	int (*free_packet)(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, struct vs_mbuf *mbuf);
	int (*send_packet)(struct vs_eth_priv *priv,
		struct vs_pbuf pbuf, struct vs_mbuf *mbuf);
	int (*update_mtu)(struct vs_eth_priv *priv, int new_mtu);
};

extern struct vs_eth_priv *
vservice_veth_create(struct vs_service_device *parent, bool is_server);
extern void vservice_veth_release(struct vs_eth_priv *priv);

extern int vservice_veth_recv(struct vs_eth_priv *priv, struct vs_pbuf pbuf,
		struct vs_mbuf *mbuf);
extern int vservice_veth_recv_error(struct vs_eth_priv *priv,
		vservice_ethernet_frame_error_t type);

extern int vservice_veth_tx_ready(struct vs_eth_priv *priv);

extern void vservice_veth_start(struct vs_eth_priv *priv);

extern void vservice_veth_stop(struct vs_eth_priv *priv);

extern int vs_eth_update_mtu(struct vs_eth_priv *priv, int new_mtu);

#endif /* _VS_ETHERNET_COMMON_H */
