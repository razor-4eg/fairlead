//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_ip.h -- Addresses, networks, and endpoints. Also the text snapshots taken from route and ifconfig.
//
//===============================================================

#ifndef FL_IP_H
#define FL_IP_H

typedef struct fl_ip_s {
	int v6;
	unsigned char bytes[16];
} fl_ip_t;

typedef struct fl_cidr_s {
	fl_ip_t ip;
	int prefix;
} fl_cidr_t;

typedef struct fl_route_snap_s {
	char destination[128];
	char mask[64];
	char gateway[128];
	char interface_name[64];
	int has_destination;
	int has_mask;
	int has_gateway;
	int has_interface;
} fl_route_snap_t;

int		fl_ip_parse(const char *text, fl_ip_t *out);
void	fl_ip_format(const fl_ip_t *ip, char *buf, int cap);
int		fl_ip_equal(const fl_ip_t *a, const fl_ip_t *b);
int		fl_ip_unspecified(const fl_ip_t *ip);
int		fl_ip_loopback(const fl_ip_t *ip);
int		fl_ip_multicast(const fl_ip_t *ip);
int		fl_ip_link_local(const fl_ip_t *ip);
int		fl_ip_broadcast(const fl_ip_t *ip);
int		fl_ip_routable(const fl_ip_t *ip);
void	fl_ip_mask(fl_ip_t *ip, int prefix);

int		fl_cidr_parse(const char *text, fl_cidr_t *out);
void	fl_cidr_format(const fl_cidr_t *cidr, char *buf, int cap);
int		fl_cidr_contains(const fl_cidr_t *cidr, const fl_ip_t *ip);
int		fl_cidr_max_prefix(const fl_cidr_t *cidr);

/* host from "host:port" or "[ipv6]:port". Returns 0 on "(none)" / empty. */
int		fl_endpoint_host(const char *endpoint, char *buf, int cap);
int		fl_endpoint_port(const char *endpoint, int *port);
int		fl_endpoint_valid(const char *endpoint);
/* ip:port or [ip]:port using the port from the original endpoint. */
int		fl_socket_endpoint(const char *ip, const char *original, char *buf, int cap);

void	fl_route_snapshot(const char *text, fl_route_snap_t *out);
int		fl_route_exact(const fl_cidr_t *cidr, const fl_route_snap_t *snap);
/* On-link networks from one ifconfig blob. Writes cidr strings. */
int		fl_ifconfig_networks(const char *text, char out[][64], int cap);

#endif
