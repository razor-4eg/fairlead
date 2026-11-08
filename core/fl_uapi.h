//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_uapi.h -- Userspace WireGuard messages and the peer status parsed out of them.
//
//===============================================================

#ifndef FL_UAPI_H
#define FL_UAPI_H

#include "fl_doc.h"

#define FL_PEER_MAX 8

typedef struct fl_peer_status_s {
	char public_key[128];
	char endpoint[160];
	char allowed[512];
	int handshake;
	unsigned long long rx;
	unsigned long long tx;
	int keepalive;
} fl_peer_status_t;

typedef struct fl_device_status_s {
	char interface_name[64];
	char public_key[128];
	int listen_port;
	fl_peer_status_t peers[FL_PEER_MAX];
	int peer_count;
} fl_device_status_t;

/* endpoints_by_section[section] is "ip:port" or NULL. Returns 0 on failure. */
int		fl_uapi_set(const fl_doc_t *doc, const char **endpoints_by_section, char *out, int cap);
void	fl_uapi_parse_get(const char *text, const char *iface, fl_device_status_t *out);

int		fl_dump_parse(const char *text, fl_device_status_t *out, int cap);

#endif
