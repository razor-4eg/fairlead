//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// tunnel.h -- WireGuard document, keys, Amnezia, and UAPI status.
//
//================================================================

#ifndef TUNNEL_H
#define TUNNEL_H

typedef struct entry_s {
	int is_field;
	char *raw;
	char *key;
	char *value;
} entry_t;

typedef struct section_s {
	char *name;
	entry_t *entries;
	int count;
	int cap;
} section_t;

typedef struct doc_s {
	char **preamble;
	int preamble_count;
	int preamble_cap;
	section_t *sections;
	int section_count;
	int section_cap;
} doc_t;

void	doc_init(doc_t *doc);
void	doc_free(doc_t *doc);
void	doc_parse(doc_t *doc, const char *text);
/* Caller frees the result. Always ends with a newline. */
char	*doc_render(const doc_t *doc);

int		doc_interface(const doc_t *doc);
int		doc_peers(const doc_t *doc, int *out, int cap);
void	doc_value(const doc_t *doc, int section, const char *key, char *buf, int cap);
void	doc_set(doc_t *doc, int section, const char *key, const char *value);
int		doc_ensure_interface(doc_t *doc);
int		doc_add_peer(doc_t *doc);

void	doc_addresses(const doc_t *doc, char *buf, int cap);
void	doc_dns(const doc_t *doc, char *buf, int cap);
void	doc_peer_allowed(const doc_t *doc, int section, char *buf, int cap);
void	doc_blank(doc_t *doc, const char *private_key);

/* Split a comma list into trimmed tokens. Returns count. */
int		split_commas(const char *text, char out[][256], int cap);

int		b64_decode(const char *text, unsigned char *out, int cap);
int		b64_encode(const unsigned char *in, int len, char *out, int cap);
int		key_is(const char *text);
/* 64 hex chars, no NUL counted in the 64. Returns 0 on failure. */
int		key_hex(const char *b64, char *hex, int cap);
int		hex_b64(const char *hex, char *b64, int cap);
int		key_public(const char *private_b64, char *public_b64, int cap);
int		key_generate(char *private_b64, int private_cap, char *public_b64, int public_cap);

enum {
	IMPORT_WG = 0,
	IMPORT_UNSUPPORTED = 1,
	IMPORT_UNREADABLE = 2
};

int		amnezia_is(const doc_t *doc);
int		amnezia_uint_range(const char *text, unsigned long long maxv);
/* 1 and writes lines (possibly empty). 0 if a present value cannot be encoded. */
int		amnezia_uapi(const doc_t *doc, char *out, int cap);
int		amnezia_present(const doc_t *doc, const char **out, int cap);
int		amnezia_issues(const doc_t *doc, char out[][192], int cap);
int		config_issues(const doc_t *doc, char out[][192], int cap);

/* config receives the WireGuard text (original text when it is already one).
   name may be empty. Both buffers are always terminated. */
int		import_normalize(const char *text, char *config, int config_cap, char *name, int name_cap);

/* Internet halves used as tunnel routes for 0.0.0.0/0 and ::/0. */
int		tunnel_destinations(const doc_t *doc, char families[][8], char dests[][64], int cap);

#define PEER_MAX 8

typedef struct peer_status_s {
	char public_key[128];
	char endpoint[160];
	char allowed[512];
	int handshake;
	unsigned long long rx;
	unsigned long long tx;
	int keepalive;
} peer_status_t;

typedef struct device_status_s {
	char interface_name[64];
	char public_key[128];
	int listen_port;
	peer_status_t peers[PEER_MAX];
	int peer_count;
} device_status_t;

/* endpoints_by_section[section] is "ip:port" or NULL. Returns 0 on failure. */
int		uapi_set(const doc_t *doc, const char **endpoints_by_section, char *out, int cap);
void	uapi_parse_get(const char *text, const char *iface, device_status_t *out);

int		dump_parse(const char *text, device_status_t *out, int cap);

#endif
