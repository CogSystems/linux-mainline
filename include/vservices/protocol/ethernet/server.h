
/*
 * Copyright (c) 2012-2018 General Dynamics
 * Copyright (c) 2014 Open Kernel Labs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if !defined(VSERVICES_SERVER_ETHERNET)
#define VSERVICES_SERVER_ETHERNET

struct vs_service_device;
struct vs_server_ethernet_state;

struct vs_server_ethernet {

	/*
	 * If set to false then the receive message handlers are run from
	 * workqueue context and are allowed to sleep. If set to true the
	 * message handlers are run from tasklet context and may not sleep.
	 */
	bool rx_atomic;

	/*
	 * If this is set to true along with rx_atomic, the driver is allowed
	 * to send messages from softirq contexts other than the receive
	 * message handlers, after calling vs_service_state_lock_bh. Otherwise,
	 * messages may only be sent from the receive message handlers, or
	 * from task context after calling vs_service_state_lock. This must
	 * not be set to true if rx_atomic is set to false.
	 */
	bool tx_atomic;

	/*
	 * These are the driver's recommended message quotas. They are used
	 * by the core service to select message quotas for services with no
	 * explicitly configured quotas.
	 */
	u32 in_quota_best;
	u32 out_quota_best;
    /** session setup **/
	struct vs_server_ethernet_state *(*alloc) (struct vs_service_device *
						   service);
	void (*release) (struct vs_server_ethernet_state * _state);

	struct vs_service_driver *driver;

/** Open, reopen, close and closed functions **/

	 vs_server_response_type_t(*open) (struct vs_server_ethernet_state *
					   _state);

	 vs_server_response_type_t(*reopen) (struct vs_server_ethernet_state *
					     _state);

	 vs_server_response_type_t(*close) (struct vs_server_ethernet_state *
					    _state);

	void (*closed) (struct vs_server_ethernet_state * _state);

/** Send/receive state callbacks **/
	int (*tx_ready) (struct vs_server_ethernet_state * _state);

	struct {
		int (*msg_packet) (struct vs_server_ethernet_state * _state,
				   struct vs_pbuf data, struct vs_mbuf * _mbuf);

		int (*msg_error) (struct vs_server_ethernet_state * _state,
				  vservice_ethernet_frame_error_t type);

		int (*msg_update_mtu) (struct vs_server_ethernet_state * _state,
				       uint32_t new_mtu);

	} ethernet;
};

struct vs_server_ethernet_state {
	vservice_ethernet_protocol_state_t state;
	uint32_t max_frame_size;
	struct {
		uint32_t max_frame_size;
	} ethernet;
	struct vs_service_device *service;
	bool released;
};

/** Complete calls for server core functions **/
extern int vs_server_ethernet_open_complete(struct vs_server_ethernet_state
					    *_state,
					    vs_server_response_type_t resp);

extern int vs_server_ethernet_close_complete(struct vs_server_ethernet_state
					     *_state,
					     vs_server_response_type_t resp);

extern int vs_server_ethernet_reopen_complete(struct vs_server_ethernet_state
					      *_state,
					      vs_server_response_type_t resp);

    /** interface ethernet **/
/* message packet */
extern struct vs_mbuf *vs_server_ethernet_ethernet_alloc_packet(struct
								vs_server_ethernet_state
								*_state,
								struct vs_pbuf
								*data,
								gfp_t flags);
extern int vs_server_ethernet_ethernet_getbufs_packet(struct
						      vs_server_ethernet_state
						      *_state,
						      struct vs_pbuf *data,
						      struct vs_mbuf *_mbuf);
extern int vs_server_ethernet_ethernet_free_packet(struct
						   vs_server_ethernet_state
						   *_state,
						   struct vs_pbuf *data,
						   struct vs_mbuf *_mbuf);
extern int vs_server_ethernet_ethernet_send_packet(struct
						   vs_server_ethernet_state
						   *_state, struct vs_pbuf data,
						   struct vs_mbuf *_mbuf);

	    /* message error */
extern int vs_server_ethernet_ethernet_send_error(struct
						  vs_server_ethernet_state
						  *_state,
						  vservice_ethernet_frame_error_t
						  type, gfp_t flags);

	    /* message update_mtu */
extern int vs_server_ethernet_ethernet_send_update_mtu(struct
						       vs_server_ethernet_state
						       *_state,
						       uint32_t new_mtu,
						       gfp_t flags);

/** Module registration **/

struct module;

extern int __vservice_ethernet_server_register(struct vs_server_ethernet
					       *server, const char *name,
					       struct module *owner);

static inline int vservice_ethernet_server_register(struct vs_server_ethernet
						    *server, const char *name)
{
#ifdef MODULE
	extern struct module __this_module;
	struct module *this_module = &__this_module;
#else
	struct module *this_module = NULL;
#endif

	return __vservice_ethernet_server_register(server, name, this_module);
}

extern int vservice_ethernet_server_unregister(struct vs_server_ethernet
					       *server);
#endif				/* ! VSERVICES_SERVER_ETHERNET */
