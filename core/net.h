//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// net.h -- Addresses, DNS packets, and bypass route plans.
//
//================================================================

#ifndef NET_H
#define NET_H

#include <stdint.h>
typedef struct ip_s {
	int v6;
	unsigned char bytes[16];
} ip_t;

typedef struct cidr_s {
	ip_t ip;
	int prefix;
} cidr_t;

typedef struct route_snap_s {
	char destination[128];
	char mask[64];
	char gateway[128];
	char interface_name[64];
	int has_destination;
	int has_mask;
	int has_gateway;
	int has_interface;
} route_snap_t;

int		ip_parse(const char *text, ip_t *out);
void	ip_format(const ip_t *ip, char *buf, int cap);
int		ip_equal(const ip_t *a, const ip_t *b);
int		ip_unspecified(const ip_t *ip);
int		ip_loopback(const ip_t *ip);
int		ip_multicast(const ip_t *ip);
int		ip_link_local(const ip_t *ip);
int		ip_broadcast(const ip_t *ip);
int		ip_routable(const ip_t *ip);
void	ip_mask(ip_t *ip, int prefix);

int		cidr_parse(const char *text, cidr_t *out);
void	cidr_format(const cidr_t *cidr, char *buf, int cap);
int		cidr_contains(const cidr_t *cidr, const ip_t *ip);
int		cidr_max_prefix(const cidr_t *cidr);

/* host from "host:port" or "[ipv6]:port". Returns 0 on "(none)" / empty. */
int		endpoint_host(const char *endpoint, char *buf, int cap);
int		endpoint_port(const char *endpoint, int *port);
int		endpoint_valid(const char *endpoint);
/* ip:port or [ip]:port using the port from the original endpoint. */
int		socket_endpoint(const char *ip, const char *original, char *buf, int cap);

void	route_snapshot(const char *text, route_snap_t *out);
int		route_exact(const cidr_t *cidr, const route_snap_t *snap);
/* On-link networks from one ifconfig blob. Writes cidr strings. */
int		ifconfig_networks(const char *text, char out[][64], int cap);


#define DNS_A 1
#define DNS_AAAA 28

typedef struct dns_rec_s {
	char address[64];
	uint32_t ttl;
} dns_rec_t;

typedef struct dns_reply_s {
	int rcode;
	int truncated;
	int saw_cname;
	dns_rec_t answers[32];
	int answer_count;
	dns_rec_t additional[32];
	int additional_count;
} dns_reply_t;

/* Lowercase hostname, or NULL-length 0 when rejected. Writes into buf. */
int		domain_normalize(const char *raw, char *buf, int cap);

/* Returns byte count, or 0. */
int		dns_query(uint16_t id, const char *name, uint16_t type, int edns, unsigned char *out, int cap);
int		dns_parse(const unsigned char *data, int len, uint16_t id, dns_reply_t *out);

/* Large Direct lists (thousands of /32s) must fit. Callers heap-allocate bypass_t. */
#define BYPASS_MAX 8192

typedef struct bypass_s {
	char routes[BYPASS_MAX][64];
	int route_count;
	char direct[BYPASS_MAX][64];
	int direct_count;
	char warnings[16][256];
	int warning_count;
} bypass_t;

/* policy_selected: 0 exceptions (list stays off the tunnel), 1 selected (only the list uses it). */
void bypass_plan(
	int policy_selected,
	const char **rules, int rule_count,
	const char **learned, int learned_count,
	const char **lan, int lan_count,
	int exclude_lan,
	const char **endpoints, int endpoint_count,
	bypass_t *out);

#endif
