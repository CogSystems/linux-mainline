/*
 * drivers/net/vs_ethernet_common.c
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

#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <net/rtnetlink.h>

#include <vservices/types.h>
#include <vservices/buffer.h>
#include <vservices/protocol/ethernet/types.h>
#include <vservices/protocol/ethernet/common.h>
#include <vservices/protocol/ethernet/server.h>
#include <vservices/protocol/ethernet/client.h>

#include "vs_ethernet_common.h"
#include "vs_ethernet_wifi_link.h"

struct vs_eth_priv *netdev_to_vs_priv(struct net_device *dev)
{
	if (dev->ieee80211_ptr)
		return *(struct vs_eth_priv **)netdev_priv(dev);
	else
		return netdev_priv(dev);
}

int vservice_veth_recv_error(struct vs_eth_priv *priv,
		vservice_ethernet_frame_error_t type)
{
	struct net_device *dev = priv->netdev;

	dev->stats.rx_errors++;
	switch (type) {
	case VSERVICE_ETHERNET_BUFFER_OVERRUN:
		dev->stats.rx_frame_errors++;
		break;
	case VSERVICE_ETHERNET_FRAMING_ERROR:
		dev->stats.rx_over_errors++;
		break;
	case VSERVICE_ETHERNET_LINK_ERROR:
		dev->stats.rx_fifo_errors++;
		break;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(vservice_veth_recv_error);

static int vservice_veth_dev_open(struct net_device *dev)
{
	netif_start_queue(dev);

	return 0;
}

static int vservice_veth_dev_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

extern int vservice_veth_recv(struct vs_eth_priv *priv, struct vs_pbuf pbuf,
		struct vs_mbuf *mbuf)
{
	struct sk_buff *skb;
	struct net_device *dev = priv->netdev;
	int size = pbuf.size;

	if (size < ETH_HLEN || size > dev->mtu + ETH_HLEN + sizeof(u32)) {
		dev->stats.rx_frame_errors++;
		netdev_warn(priv->netdev, "invalid size %d", size);
		return vservice_veth_recv_error(priv,
				VSERVICE_ETHERNET_BUFFER_OVERRUN);
	}

	skb = netdev_alloc_skb(dev, size + 2);
	if (!skb) {
		dev->stats.rx_dropped++;
		netdev_dbg(priv->netdev, "skb NULL packet dropped");
		return 0;
	}

	skb_reserve(skb, 2);
	skb_put(skb, size);

	if (skb_store_bits(skb, 0, pbuf.data, size)) {
		dev->stats.rx_dropped++;
		dev_kfree_skb(skb);
		return 0;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	dev->last_rx = jiffies;
#endif
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_receive_skb(skb);
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += size;

	return priv->ops->free_packet(priv, &pbuf, mbuf);
}
EXPORT_SYMBOL_GPL(vservice_veth_recv);

static void vs_eth_update_mtu_work(struct work_struct *work)
{
	struct vs_eth_priv *update_mtu_work = container_of(work,
			struct vs_eth_priv, update_mtu_work);

	rtnl_lock();
	dev_set_mtu(update_mtu_work->netdev, update_mtu_work->new_mtu);
	rtnl_unlock();
}

int vs_eth_update_mtu(struct vs_eth_priv *priv, int new_mtu)
{
	/*
	 * We need to acquire the rtnl lock to set the mtu, but we can't do
	 * that from atomic context (message handlers), so schedule a work
	 * item to do it.
	 */
	priv->new_mtu = new_mtu;
	schedule_work(&priv->update_mtu_work);
	return 0;
}
EXPORT_SYMBOL_GPL(vs_eth_update_mtu);

static int vs_eth_xmit_free_packet(struct vs_eth_priv *priv,
		struct vs_mbuf *mbuf, struct vs_pbuf pbuf)
{
	return priv->ops->free_packet(priv, &pbuf, mbuf);
}

static struct vs_mbuf *vs_eth_alloc_packet(struct vs_eth_priv *priv,
		struct vs_pbuf *pbuf)
{
	return priv->ops->alloc_packet(priv, pbuf, GFP_ATOMIC);
}

static int vs_eth_send_packet(struct vs_eth_priv *priv, struct vs_mbuf *mbuf,
		struct vs_pbuf pbuf)
{
	return priv->ops->send_packet(priv, pbuf, mbuf);
}

static int vservice_veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vs_eth_priv *priv = netdev_to_vs_priv(dev);
	struct vs_mbuf *mbuf;
	struct vs_pbuf pbuf;
	unsigned int size = skb->len;
	int ret;
	bool connected;

	if (priv->is_server)
		connected = VSERVICE_BASE_STATE_IS_RUNNING(
				priv->u.server.state.base);
	else
		connected = VSERVICE_BASE_STATE_IS_RUNNING(
				priv->u.client.state.base);

	if (!priv->is_server && dev->ieee80211_ptr) {
		const struct ethhdr *ethhdr = (const struct ethhdr *)skb->data;

		if (ethhdr->h_proto == cpu_to_be16(ETH_P_PAE)) {
			priv->wifi_link->xmit_eapol(skb, dev, priv->wifi_link);
			goto out;
		}
	}

	if (!connected) {
		ret = -ENOTCONN;
		goto free_skb;
	}

	if (size < ETH_HLEN || size > dev->mtu + ETH_HLEN + sizeof(u32)) {
		ret = -EMSGSIZE;
		goto free_skb;
	}

	mbuf = vs_eth_alloc_packet(priv, &pbuf);
	if (IS_ERR(mbuf)) {
		/* FIXME: Jira ticket SDK-3137 - anjaniv. */
		ret = NET_XMIT_DROP;
		netif_stop_queue(dev);
		goto free_skb;
	}

	if (vs_pbuf_resize(&pbuf, skb->len) < 0) {
		ret = -EBADMSG;
		goto free_pkt;
	}

	if (skb_copy_bits(skb, 0, pbuf.data, skb->len)) {
		ret = -EBADMSG;
		goto free_pkt;
	}

	ret = vs_eth_send_packet(priv, mbuf, pbuf);
	if (ret) {
		ret = -ECOMM;
		goto free_pkt;
	}
out:
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += size;
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;

free_pkt:
	if (vs_eth_xmit_free_packet(priv, mbuf, pbuf))
		netdev_warn(dev, "error freeing xmit packet.\n");

free_skb:
	dev_kfree_skb(skb);
	dev->stats.tx_dropped++;

	return ret;
}

static int ndo_set_mac_address(struct net_device *dev, void *mac)
{
	struct sockaddr *addr = mac;

	netif_stop_queue(dev);

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	netif_start_queue(dev);
	return 0;
}

static int vservice_veth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct vs_eth_priv *priv = netdev_to_vs_priv(dev);
	int err, max_mtu;

	if (priv->is_server)
		max_mtu = priv->u.server.max_frame_size - ETH_HLEN;
	else
		max_mtu = priv->u.client.max_frame_size - ETH_HLEN;

	if (new_mtu > max_mtu)
		return -EINVAL;

	/* Notify the other end of the change */
	err = priv->ops->update_mtu(priv, new_mtu);
	if (err)
		return err;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops vservice_veth_netdev_ops = {
	.ndo_open		= vservice_veth_dev_open,
	.ndo_stop		= vservice_veth_dev_close,
	.ndo_start_xmit		= vservice_veth_xmit,
	.ndo_set_mac_address	= ndo_set_mac_address,
	.ndo_change_mtu		= vservice_veth_change_mtu,
};

static void vservice_veth_setup(struct net_device *dev)
{
	dev->netdev_ops = &vservice_veth_netdev_ops;
	ether_setup(dev);
#ifdef CONFIG_NET_REMOTE_FILTER
	dev->priv_flags |= IFF_SOFTWARE;
#endif
}

static void vservice_eth_start_work(struct work_struct *work)
{
	struct vs_eth_priv *priv = container_of(work, struct vs_eth_priv,
			start_work);
	struct net_device *netdev = priv->netdev;

	netif_carrier_on(netdev);

	rtnl_lock();
	call_netdevice_notifiers(NETDEV_CHANGEADDR, netdev);
	rtnl_unlock();

	netif_start_queue(netdev);
}

static LIST_HEAD(vs_wifi_client_eth_links_list);
static DEFINE_MUTEX(vs_wifi_client_eth_links_list_lock);

/*
 * Find the link between veth and vwifi clients by the service name.
 * If the link hasn't yet been created then it will created automatically.
 */
struct vs_eth_hookup *vservice_eth_get_ethlink(const char *name)
{
	struct vs_eth_hookup *vs_eth_link;
	bool found = false;

	mutex_lock(&vs_wifi_client_eth_links_list_lock);
	list_for_each_entry(vs_eth_link, &vs_wifi_client_eth_links_list, list) {
		if (!strcmp(name, vs_eth_link->name)) {
			found = true;
			break;
		}
	}

	if (found) {
		kref_get(&vs_eth_link->refcount);
	} else {
		vs_eth_link = kzalloc(sizeof(struct vs_eth_hookup), GFP_KERNEL);
		if (!vs_eth_link) {
			mutex_unlock(&vs_wifi_client_eth_links_list_lock);
			return NULL;
		}
		vs_eth_link->name = name;
		mutex_init(&vs_eth_link->lock);
		kref_init(&vs_eth_link->refcount);
		list_add(&vs_eth_link->list, &vs_wifi_client_eth_links_list);
	}

	mutex_unlock(&vs_wifi_client_eth_links_list_lock);
	return vs_eth_link;
}
EXPORT_SYMBOL_GPL(vservice_eth_get_ethlink);

static void vs_eth_hookup_release(struct kref *ref)
{
	struct vs_eth_hookup *data = container_of(ref, struct vs_eth_hookup,
			refcount);

	lockdep_assert_held(&vs_wifi_client_eth_links_list_lock);

	list_del(&data->list);
	kfree(data);
}

void vs_eth_hookup_put(struct vs_eth_hookup *hookup, bool reset_wifi)
{
	mutex_lock(&vs_wifi_client_eth_links_list_lock);
	kref_put(&hookup->refcount, vs_eth_hookup_release);

	mutex_unlock(&vs_wifi_client_eth_links_list_lock);

	if (reset_wifi)
		vs_service_reset_nosync(hookup->wifi_service);

}
EXPORT_SYMBOL_GPL(vs_eth_hookup_put);

#define VWIFI_MAGIC_PREFIX "wifi_"

static char *vs_wifi_base_service_name(char *name)
{
	return &name[strlen(VWIFI_MAGIC_PREFIX)];
}

struct net_device *vs_eth_create(const char *name, size_t priv_size,
		struct device *parent, bool is_server,
		struct vs_eth_priv *priv)
{
	struct vs_service_device *service = to_vs_service_device(parent);
	struct net_device *dev;
	struct vs_eth_priv *local_priv;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	dev = alloc_netdev(priv_size, name, vservice_veth_setup);
#else
	dev = alloc_netdev(priv_size, name, NET_NAME_UNKNOWN, vservice_veth_setup);
#endif
	if (!dev)
		return NULL;

	random_ether_addr(dev->dev_addr);
	if (is_server)
		dev->dev_addr[5] &= ~1;
	else
		dev->dev_addr[5] |= 1;

	SET_NETDEV_DEV(dev, parent);
	if (priv)
		local_priv = priv;
	else
		local_priv = netdev_priv(dev);

	local_priv->netdev = dev;

	/*
	 * Set the initial MTU value based to the maximum size the service
	 * can support for the packet message.
	 */
	/* FIXME: Jira ticket SDK-3521 - ryanm. */
	dev->mtu = vs_service_max_mbuf_size(service) -
			sizeof(vs_message_id_t) - sizeof(u32) - ETH_HLEN;
	local_priv->new_mtu = dev->mtu;

	return dev;
}
EXPORT_SYMBOL_GPL(vs_eth_create);

struct vs_eth_priv *vservice_veth_create(struct vs_service_device *service,
		bool is_server)
{
	struct device *parent = &service->dev;
	struct net_device *dev;
	struct vs_eth_priv *priv;
	int err;

	if (!is_server && !strncmp(VWIFI_MAGIC_PREFIX, service->name,
			strlen(VWIFI_MAGIC_PREFIX))) {
		struct vs_eth_hookup *hookup;

		hookup = vservice_eth_get_ethlink(
				vs_wifi_base_service_name(service->name));
		if (!hookup)
			return NULL;
		mutex_lock(&hookup->lock);
		if (hookup->eth_priv) {
			/*
			 * If there is a eth_priv already set it means this is
			 * a duplicate service name, so error out.
			 */
			pr_err("A Virtual Services Ethernet client with the "
					"service name '%s' already exists.",
					service->name);
			mutex_unlock(&hookup->lock);
			vs_eth_hookup_put(hookup, false);
			return NULL;

		} else {
			hookup->eth_priv = kzalloc(sizeof(struct vs_eth_priv),
					GFP_KERNEL);
			if (!hookup->eth_priv) {
				mutex_unlock(&hookup->lock);
				vs_eth_hookup_put(hookup, false);
				return NULL;
			}
		}
		hookup->eth_priv->wifi_link = hookup;
		hookup->eth_priv->is_wifi = true;
		hookup->eth_priv->waiting_for_wifi = true;
		priv = hookup->eth_priv;
		if (hookup->veth_ready)
			hookup->veth_ready(hookup);
		mutex_unlock(&hookup->lock);
		goto out;
	}

	/* FIXME: Jira ticket SDK-4052 - jdgordon. */
	dev = vs_eth_create("veth%d", sizeof(struct vs_eth_priv),
			parent, is_server, NULL);
	if (!dev)
		return NULL;

	err = register_netdev(dev);
	if (err)
		goto err;
	priv = netdev_priv(dev);
	netif_carrier_off(dev);

out:
	priv->is_server = is_server;
	INIT_WORK(&priv->start_work, vservice_eth_start_work);
	INIT_WORK(&priv->update_mtu_work, vs_eth_update_mtu_work);

	priv->service = service;
	return priv;

err:
	free_netdev(dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(vservice_veth_create);

extern int vservice_veth_tx_ready(struct vs_eth_priv *priv)
{
	netif_start_queue(priv->netdev);

	return 0;
}
EXPORT_SYMBOL_GPL(vservice_veth_tx_ready);

void vservice_veth_release(struct vs_eth_priv *priv)
{
	if (priv->is_wifi) {
		priv->wifi_link->eth_priv = NULL;
		vs_eth_hookup_put(priv->wifi_link, true);
		kfree(priv);
	} else {
		unregister_netdev(priv->netdev);
		free_netdev(priv->netdev);
	}
}
EXPORT_SYMBOL_GPL(vservice_veth_release);

void vservice_veth_start(struct vs_eth_priv *priv)
{
	if (priv->is_wifi && priv->waiting_for_wifi)
		return;
	/*
	 * We cannot acquire the rtnl_lock from atomic context, so schedule
	 * a work item to do it for us.
	 */
	schedule_work(&priv->start_work);
}
EXPORT_SYMBOL_GPL(vservice_veth_start);

void vservice_veth_stop(struct vs_eth_priv *priv)
{
	struct net_device *netdev = priv->netdev;

	if (priv->is_wifi && priv->waiting_for_wifi)
		return;
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
}
EXPORT_SYMBOL_GPL(vservice_veth_stop);

MODULE_DESCRIPTION("OKL4 Virtual Services Ethernet Common Driver");
MODULE_AUTHOR("Open Kernel Labs, Inc");
