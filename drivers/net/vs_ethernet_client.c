/*
 * drivers/net/vs_ethernet_client.c
 *
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vServices Ethernet client driver
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
#include <vservices/protocol/ethernet/client.h>

#include "vs_ethernet_common.h"

#define to_vs_eth_priv(x) container_of((x), struct vs_eth_priv, u.client)

static struct vs_mbuf *vs_eth_client_alloc_packet(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, gfp_t flags)
{
	return vs_client_ethernet_ethernet_alloc_packet(&priv->u.client,
			pbuf, flags);
}

static int vs_eth_client_free_packet(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf, struct vs_mbuf *mbuf)
{
	return vs_client_ethernet_ethernet_free_packet(&priv->u.client,
			pbuf, mbuf);
}

static int vs_eth_client_send_packet(struct vs_eth_priv *priv,
		struct vs_pbuf pbuf, struct vs_mbuf *mbuf)
{
	return vs_client_ethernet_ethernet_send_packet(&priv->u.client,
			pbuf, mbuf);
}

static int vs_eth_client_send_update_mtu(struct vs_eth_priv *priv, int new_mtu)
{
	return vs_service_waiting_send(&priv->u.client,
			vs_client_ethernet_ethernet_send_update_mtu(
			&priv->u.client, new_mtu, GFP_KERNEL));
}

static const struct vs_eth_ops vs_eth_client_ops = {
	.alloc_packet	= vs_eth_client_alloc_packet,
	.free_packet	= vs_eth_client_free_packet,
	.send_packet	= vs_eth_client_send_packet,
	.update_mtu	= vs_eth_client_send_update_mtu,
};

bool is_wifi_connected(struct vs_client_ethernet_state *state)
{
	struct vs_eth_priv *priv = to_vs_eth_priv(state);

	return !priv->waiting_for_wifi;
}
EXPORT_SYMBOL_GPL(is_wifi_connected);

void vservice_eth_wifi_connected(struct net_device *ndev, bool connected)
{
	struct vs_eth_priv *priv = *(struct vs_eth_priv **)netdev_priv(ndev);

	priv->waiting_for_wifi = !connected;
	if (connected)
		vservice_veth_start(priv);
	else
		vservice_veth_stop(priv);
}
EXPORT_SYMBOL_GPL(vservice_eth_wifi_connected);

static struct vs_client_ethernet_state *
vs_eth_client_alloc(struct vs_service_device *service)
{
	struct vs_eth_priv *priv;

	priv = vservice_veth_create(service, false);
	if (!priv)
		return NULL;
	priv->ops = &vs_eth_client_ops;

	return &priv->u.client;
}

static void vs_eth_client_release(struct vs_client_ethernet_state *state)
{
	return vservice_veth_release(to_vs_eth_priv(state));
}

static void vs_eth_client_closed(struct vs_client_ethernet_state *state)
{
	vservice_veth_stop(to_vs_eth_priv(state));
}

static void vs_eth_client_opened(struct vs_client_ethernet_state *state)
{
	vservice_veth_start(to_vs_eth_priv(state));
}

static int vs_eth_client_msg_error(struct vs_client_ethernet_state *state,
		vservice_ethernet_frame_error_t type)
{
	if (!is_wifi_connected(state))
		return 0;
	return vservice_veth_recv_error(to_vs_eth_priv(state), type);
}

static int vs_eth_client_msg_packet(struct vs_client_ethernet_state *state,
		struct vs_pbuf pbuf, struct vs_mbuf *mbuf)
{
	if (!is_wifi_connected(state))
		return vs_client_ethernet_ethernet_free_packet(
				&to_vs_eth_priv(state)->u.client, &pbuf, mbuf);
	return vservice_veth_recv(to_vs_eth_priv(state), pbuf, mbuf);
}

static int vs_eth_client_msg_update_mtu(struct vs_client_ethernet_state *state,
		u32 new_mtu)
{
	return vs_eth_update_mtu(to_vs_eth_priv(state), new_mtu);
}

static int vs_eth_client_tx_ready(struct vs_client_ethernet_state *state)
{
	if (!is_wifi_connected(state))
		return 1;
	return vservice_veth_tx_ready(to_vs_eth_priv(state));
}

static struct vs_client_ethernet eth_client_driver = {
	.rx_atomic		= true,
	.alloc			= vs_eth_client_alloc,
	.release		= vs_eth_client_release,
	.opened			= vs_eth_client_opened,
	.closed			= vs_eth_client_closed,
	.tx_ready		= vs_eth_client_tx_ready,
	.ethernet = {
		.msg_error	= vs_eth_client_msg_error,
		.msg_packet	= vs_eth_client_msg_packet,
		.msg_update_mtu	= vs_eth_client_msg_update_mtu,
	}
};

static int __init vs_ethernet_client_init(void)
{
	return vservice_ethernet_client_register(&eth_client_driver,
			"eth_client_driver");
}

static void __exit vs_ethernet_client_exit(void)
{
	vservice_ethernet_client_unregister(&eth_client_driver);
}

module_init(vs_ethernet_client_init);
module_exit(vs_ethernet_client_exit);

MODULE_DESCRIPTION("OKL4 Virtual Services Ethernet Client Driver");
MODULE_AUTHOR("Open Kernel Labs, Inc");
