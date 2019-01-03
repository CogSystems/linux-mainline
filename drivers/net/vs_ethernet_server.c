/*
 * drivers/net/vs_ethernet_server.c
 *
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vServices Ethernet server driver
 *
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <net/rtnetlink.h>

#include <vservices/types.h>
#include <vservices/buffer.h>
#include <vservices/service.h>
#include <vservices/wait.h>
#include <vservices/protocol/ethernet/types.h>
#include <vservices/protocol/ethernet/common.h>
#include <vservices/protocol/ethernet/server.h>

#include "vs_ethernet_common.h"

#define to_vs_eth_priv(x) container_of((x), struct vs_eth_priv, u.server)

static struct vs_mbuf* vs_eth_server_alloc_packet(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, gfp_t flags)
{
	return vs_server_ethernet_ethernet_alloc_packet(&priv->u.server,
			pbuf, flags);
}

static int vs_eth_server_free_packet(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, struct vs_mbuf *mbuf)
{
	return vs_server_ethernet_ethernet_free_packet(&priv->u.server,
			pbuf, mbuf);
}

static int vs_eth_server_send_packet(struct vs_eth_priv *priv,
		struct vs_pbuf pbuf, struct vs_mbuf *mbuf)
{
	return vs_server_ethernet_ethernet_send_packet(&priv->u.server,
			pbuf, mbuf);
}

static int vs_eth_server_send_update_mtu(struct vs_eth_priv *priv, int new_mtu)
{
	return vs_service_waiting_send(&priv->u.server,
			vs_server_ethernet_ethernet_send_update_mtu(
			&priv->u.server, new_mtu, GFP_KERNEL));
}

static const struct vs_eth_ops vs_eth_server_ops = {
	.alloc_packet	= vs_eth_server_alloc_packet,
	.free_packet	= vs_eth_server_free_packet,
	.send_packet	= vs_eth_server_send_packet,
	.update_mtu	= vs_eth_server_send_update_mtu,
};

static struct vs_server_ethernet_state *
vs_eth_server_alloc(struct vs_service_device *service)
{
	struct vs_eth_priv *priv;

	priv = vservice_veth_create(service, true);
	if (!priv)
		return NULL;
	priv->ops = &vs_eth_server_ops;

	return &priv->u.server;
}

static void vs_eth_server_release(struct vs_server_ethernet_state *state)
{
	return vservice_veth_release(to_vs_eth_priv(state));
}

static vs_server_response_type_t
vs_eth_server_send_open(struct vs_server_ethernet_state *state)
{
	struct vs_eth_priv *priv = to_vs_eth_priv(state);
	struct vs_service_device *service = priv->service;

	state->max_frame_size = vs_service_max_mbuf_size(service) -
		sizeof(vs_message_id_t) - sizeof(u32);

	vservice_veth_start(priv);

	return VS_SERVER_RESP_SUCCESS;
}

static vs_server_response_type_t
vs_eth_server_send_close(struct vs_server_ethernet_state *state)
{
	vservice_veth_stop(to_vs_eth_priv(state));

	return VS_SERVER_RESP_SUCCESS;
}

static void
vs_eth_server_send_closed(struct vs_server_ethernet_state *state)
{
	vservice_veth_stop(to_vs_eth_priv(state));
}

static int vs_eth_server_msg_error(struct vs_server_ethernet_state *state,
		vservice_ethernet_frame_error_t type)
{
	return vservice_veth_recv_error(to_vs_eth_priv(state), type);
}

static int vs_eth_server_msg_packet(struct vs_server_ethernet_state *state,
		struct vs_pbuf pbuf, struct vs_mbuf *mbuf)
{
	return vservice_veth_recv(to_vs_eth_priv(state), pbuf, mbuf);
}

static int vs_eth_server_msg_update_mtu(struct vs_server_ethernet_state *state,
		u32 new_mtu)
{
	return vs_eth_update_mtu(to_vs_eth_priv(state), new_mtu);
}

static int vs_eth_server_tx_ready(struct vs_server_ethernet_state *state)
{
	return vservice_veth_tx_ready(to_vs_eth_priv(state));
}

static struct vs_server_ethernet eth_server_driver = {
	.rx_atomic		= true,
	.alloc			= vs_eth_server_alloc,
	.release		= vs_eth_server_release,
	.open			= vs_eth_server_send_open,
	.close			= vs_eth_server_send_close,
	.tx_ready		= vs_eth_server_tx_ready,
	.closed			= vs_eth_server_send_closed,
	.ethernet = {
		.msg_error	= vs_eth_server_msg_error,
		.msg_packet	= vs_eth_server_msg_packet,
		.msg_update_mtu	= vs_eth_server_msg_update_mtu,
	},

	/* Large default quotas to help batching of packets */
	.in_quota_best		= 32,
	.out_quota_best		= 32,
};

static int __init vs_ethernet_server_init(void)
{
	return vservice_ethernet_server_register(&eth_server_driver,
			"eth_server_driver");
}

static void __exit vs_ethernet_server_exit(void)
{
	vservice_ethernet_server_unregister(&eth_server_driver);
}

module_init(vs_ethernet_server_init);
module_exit(vs_ethernet_server_exit);

MODULE_DESCRIPTION("OKL4 Virtual Services Ethernet Server Driver");
MODULE_AUTHOR("Open Kernel Labs, Inc");
