/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Tom Gundersen <teg@jklm.no>

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#pragma once

#include <arpa/inet.h>
#include <linux/rtnetlink.h>

#include "sd-event.h"
#include "sd-rtnl.h"
#include "sd-bus.h"
#include "sd-dhcp-client.h"
#include "udev.h"

#include "rtnl-util.h"
#include "hashmap.h"
#include "list.h"

typedef struct Bridge Bridge;
typedef struct Network Network;
typedef struct Link Link;
typedef struct Address Address;
typedef struct Route Route;
typedef struct Manager Manager;

typedef struct bridge_join_callback bridge_join_callback;

struct bridge_join_callback {
        sd_rtnl_message_handler_t callback;
        Link *link;

        LIST_FIELDS(bridge_join_callback, callbacks);
};

typedef enum BridgeState {
        BRIDGE_STATE_FAILED,
        BRIDGE_STATE_CREATING,
        BRIDGE_STATE_READY,
        _BRIDGE_STATE_MAX,
        _BRIDGE_STATE_INVALID = -1,
} BridgeState;

struct Bridge {
        Manager *manager;

        char *filename;

        char *description;
        char *name;

        Link *link;
        BridgeState state;

        LIST_HEAD(bridge_join_callback, callbacks);
};

struct Network {
        Manager *manager;

        char *filename;

        struct ether_addr *match_mac;
        char *match_path;
        char *match_driver;
        char *match_type;
        char *match_name;

        char *description;
        Bridge *bridge;
        bool dhcp;
        bool dhcp_dns;
        bool dhcp_mtu;
        bool dhcp_hostname;
        bool dhcp_domainname;

        LIST_HEAD(Address, static_addresses);
        LIST_HEAD(Route, static_routes);
        Address *dns;

        Hashmap *addresses_by_section;
        Hashmap *routes_by_section;

        LIST_FIELDS(Network, networks);
};

struct Address {
        Network *network;
        uint64_t section;

        unsigned char family;
        unsigned char prefixlen;
        char *label;

        struct in_addr netmask;

        union {
                struct in_addr in;
                struct in6_addr in6;
        } in_addr;

        LIST_FIELDS(Address, static_addresses);
};

struct Route {
        Network *network;
        uint64_t section;

        unsigned char family;
        unsigned char dst_family;
        unsigned char dst_prefixlen;

        union {
                struct in_addr in;
                struct in6_addr in6;
        } in_addr;

        union {
                struct in_addr in;
                struct in6_addr in6;
        } dst_addr;

        LIST_FIELDS(Route, static_routes);
};

typedef enum LinkState {
        LINK_STATE_JOINING_BRIDGE,
        LINK_STATE_SETTING_ADDRESSES,
        LINK_STATE_SETTING_ROUTES,
        LINK_STATE_CONFIGURED,
        LINK_STATE_FAILED,
        _LINK_STATE_MAX,
        _LINK_STATE_INVALID = -1
} LinkState;

struct Link {
        Manager *manager;

        uint64_t ifindex;
        char *ifname;
        struct ether_addr mac;

        unsigned flags;

        Network *network;

        Route *dhcp_route;
        Address *dhcp_address;
        Address *dns;
        uint16_t original_mtu;

        LinkState state;

        unsigned addr_messages;
        unsigned route_messages;

        sd_dhcp_client *dhcp;
};

struct Manager {
        sd_rtnl *rtnl;
        sd_event *event;
        sd_bus *bus;
        struct udev *udev;
        struct udev_monitor *udev_monitor;
        sd_event_source *udev_event_source;

        Hashmap *links;
        Hashmap *bridges;
        LIST_HEAD(Network, networks);

        usec_t network_dirs_ts_usec;
};

extern const char* const network_dirs[];

/* Manager */

int manager_new(Manager **ret);
void manager_free(Manager *m);

int manager_load_config(Manager *m);
bool manager_should_reload(Manager *m);

int manager_udev_enumerate_links(Manager *m);
int manager_udev_listen(Manager *m);

int manager_rtnl_listen(Manager *m);
int manager_bus_listen(Manager *m);

int manager_update_resolv_conf(Manager *m);

DEFINE_TRIVIAL_CLEANUP_FUNC(Manager*, manager_free);
#define _cleanup_manager_free_ _cleanup_(manager_freep)

/* Bridge */

int bridge_load(Manager *manager);

void bridge_free(Bridge *bridge);

DEFINE_TRIVIAL_CLEANUP_FUNC(Bridge*, bridge_free);
#define _cleanup_bridge_free_ _cleanup_(bridge_freep)

int bridge_get(Manager *manager, const char *name, Bridge **ret);
int bridge_set_link(Manager *m, Link *link);
int bridge_join(Bridge *bridge, Link *link, sd_rtnl_message_handler_t cb);

/* Network */

int network_load(Manager *manager);

void network_free(Network *network);

DEFINE_TRIVIAL_CLEANUP_FUNC(Network*, network_free);
#define _cleanup_network_free_ _cleanup_(network_freep)

int network_get(Manager *manager, struct udev_device *device, Network **ret);
int network_apply(Manager *manager, Network *network, Link *link);

int config_parse_bridge(const char *unit, const char *filename, unsigned line,
                        const char *section, unsigned section_line, const char *lvalue,
                        int ltype, const char *rvalue, void *data, void *userdata);

/* gperf */

const struct ConfigPerfItem* network_gperf_lookup(const char *key, unsigned length);

/* Route */
int route_new_static(Network *network, unsigned section, Route **ret);
int route_new_dynamic(Route **ret);
void route_free(Route *route);
int route_configure(Route *route, Link *link, sd_rtnl_message_handler_t callback);

DEFINE_TRIVIAL_CLEANUP_FUNC(Route*, route_free);
#define _cleanup_route_free_ _cleanup_(route_freep)

int config_parse_gateway(const char *unit, const char *filename, unsigned line,
                         const char *section, unsigned section_line, const char *lvalue,
                         int ltype, const char *rvalue, void *data, void *userdata);

int config_parse_destination(const char *unit, const char *filename, unsigned line,
                             const char *section, unsigned section_line, const char *lvalue,
                             int ltype, const char *rvalue, void *data, void *userdata);

/* Address */
int address_new_static(Network *network, unsigned section, Address **ret);
int address_new_dynamic(Address **ret);
void address_free(Address *address);
int address_configure(Address *address, Link *link, sd_rtnl_message_handler_t callback);
int address_drop(Address *address, Link *link, sd_rtnl_message_handler_t callback);

DEFINE_TRIVIAL_CLEANUP_FUNC(Address*, address_free);
#define _cleanup_address_free_ _cleanup_(address_freep)

int config_parse_dns(const char *unit, const char *filename, unsigned line,
                     const char *section, unsigned section_line, const char *lvalue,
                     int ltype, const char *rvalue, void *data, void *userdata);

int config_parse_address(const char *unit, const char *filename, unsigned line,
                         const char *section, unsigned section_line, const char *lvalue,
                         int ltype, const char *rvalue, void *data, void *userdata);

int config_parse_label(const char *unit, const char *filename, unsigned line,
                       const char *section, unsigned section_line, const char *lvalue,
                       int ltype, const char *rvalue, void *data, void *userdata);

/* Link */

int link_new(Manager *manager, struct udev_device *device, Link **ret);
void link_free(Link *link);
int link_add(Manager *manager, struct udev_device *device, Link **ret);
int link_configure(Link *link);

int link_update(Link *link, sd_rtnl_message *message);

DEFINE_TRIVIAL_CLEANUP_FUNC(Link*, link_free);
#define _cleanup_link_free_ _cleanup_(link_freep)

/* Macros which append INTERFACE= to the message */

#define log_full_link(level, link, fmt, ...) log_meta_object(level, __FILE__, __LINE__, __func__, "INTERFACE=", link->ifname, "%s: " fmt, link->ifname, ##__VA_ARGS__)
#define log_debug_link(link, ...)       log_full_link(LOG_DEBUG, link, ##__VA_ARGS__)
#define log_info_link(link, ...)        log_full_link(LOG_INFO, link, ##__VA_ARGS__)
#define log_notice_link(link, ...)      log_full_link(LOG_NOTICE, link, ##__VA_ARGS__)
#define log_warning_link(link, ...)     log_full_link(LOG_WARNING, link, ##__VA_ARGS__)
#define log_error_link(link, ...)       log_full_link(LOG_ERR, link, ##__VA_ARGS__)

#define log_struct_link(level, link, ...) log_struct(level, "INTERFACE=%s", link->ifname, __VA_ARGS__)

/* More macros which append INTERFACE= to the message */

#define log_full_bridge(level, bridge, fmt, ...) log_meta_object(level, __FILE__, __LINE__, __func__, "INTERFACE=", bridge->name, "%s: " fmt, bridge->name, ##__VA_ARGS__)
#define log_debug_bridge(bridge, ...)       log_full_bridge(LOG_DEBUG, bridge, ##__VA_ARGS__)
#define log_info_bridge(bridge, ...)        log_full_bridge(LOG_INFO, bridge, ##__VA_ARGS__)
#define log_notice_bridge(bridge, ...)      log_full_bridge(LOG_NOTICE, bridge, ##__VA_ARGS__)
#define log_warning_bridge(bridge, ...)     log_full_bridge(LOG_WARNING, bridge,## __VA_ARGS__)
#define log_error_bridge(bridge, ...)       log_full_bridge(LOG_ERR, bridge, ##__VA_ARGS__)

#define log_struct_bridge(level, bridge, ...) log_struct(level, "INTERFACE=%s", bridge->name, __VA_ARGS__)

#define BRIDGE(bridge) "INTERFACE=%s", bridge->name
#define ADDRESS_FMT_VAL(address)            \
        (address).s_addr & 0xFF,            \
        ((address).s_addr >> 8) & 0xFF,     \
        ((address).s_addr >> 16) & 0xFF,    \
        (address).s_addr >> 24
