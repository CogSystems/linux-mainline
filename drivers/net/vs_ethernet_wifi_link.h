/*
 * drivers/net/vs_ethernet_wifi_link.h
 *
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Virtual Services Wi-Fi/veth link headers.
 *
 */
#ifndef _VS_ETHERNET_WIFI_LINK_H_
#define _VS_ETHERNET_WIFI_LINK_H_

struct vs_eth_hookup {
	const char			*name;

	struct vs_eth_priv		*eth_priv;
	struct vs_service_device	*wifi_service;
	int (*xmit_eapol)(struct sk_buff *skb, struct net_device *dev,
			struct vs_eth_hookup *link);
	void (*veth_ready)(struct vs_eth_hookup *link);

	struct list_head		list;
	struct kref			refcount;
	struct mutex			lock;
};

struct vs_eth_hookup *vservice_eth_get_ethlink(const char *name);
void vs_eth_hookup_put(struct vs_eth_hookup *hookup, bool reset_wifi);

struct net_device *vs_eth_create(const char *name, size_t priv_size,
		struct device *parent, bool is_server, struct vs_eth_priv *priv);
void vservice_eth_wifi_connected(struct net_device *ndev, bool connected);

#endif
