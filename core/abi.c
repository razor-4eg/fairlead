//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// abi.c -- Windows DLL exports for the UI (Windows only).
//
//================================================================

#include "tunnel.h"
#include "net.h"
#include "base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CORE_API __declspec(dllexport)

typedef struct buf_s {
	char	*data;
	int		len;
	int		cap;
} buf_t;

/*
===============
buf_add
Append raw text to a growable buffer.
===============
*/
static void buf_add(buf_t *b, const char *text) {
	int		n;
	int		cap;

	if (!text) {
		return;
	}

	n = (int)strlen(text);

	if (b->len + n + 1 > b->cap) {
		cap = b->cap ? b->cap * 2 : 256;

		while (cap < b->len + n + 1) {
			cap *= 2;
		}

		b->data = (char *)realloc(b->data, (size_t)cap);
		b->cap = cap;
	}

	memcpy(b->data + b->len, text, (size_t)n + 1);
	b->len += n;
}

/*
===============
buf_esc
Append a JSON string.
===============
*/
static void buf_esc(buf_t *b, const char *text) {
	const unsigned char	*p;
	char				tmp[8];

	buf_add(b, "\"");

	for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
		if (*p == '"' || *p == '\\') {
			tmp[0] = '\\';
			tmp[1] = (char)*p;
			tmp[2] = 0;
			buf_add(b, tmp);
		} else if (*p == '\n') {
			buf_add(b, "\\n");
		} else if (*p == '\r') {
			buf_add(b, "\\r");
		} else if (*p == '\t') {
			buf_add(b, "\\t");
		} else if (*p < 32) {
			snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
			buf_add(b, tmp);
		} else {
			tmp[0] = (char)*p;
			tmp[1] = 0;
			buf_add(b, tmp);
		}
	}

	buf_add(b, "\"");
}

/*
===============
put_field
One JSON object field.
===============
*/
static void put_field(buf_t *b, int *first, const char *key, const char *value) {
	if (!*first) {
		buf_add(b, ",");
	}

	*first = 0;
	buf_esc(b, key);
	buf_add(b, ":");
	buf_esc(b, value ? value : "");
}

/*
===============
abi_free
Free a string this DLL handed back.
===============
*/
CORE_API void abi_free(void *p) {
	free(p);
}

/*
===============
abi_inspect
JSON the window uses to draw one tunnel.
===============
*/
CORE_API char *abi_inspect(const char *config) {
	doc_t	doc;
	buf_t		b;
	char		value[1024];
	char		endpoint[256];
	char		address[512];
	char		pub[128];
	const char	*present[32];
	int			peers[4];
	int			iface;
	int			peer;
	int			npeer;
	int			ni;
	int			np;
	int			i;
	int			first;
	const char	*iface_keys[] = {"PrivateKey", "Address", "DNS", "MTU", "ListenPort"};
	const char	*peer_keys[] = {"PublicKey", "PresharedKey", "Endpoint", "AllowedIPs", "PersistentKeepalive"};
	char		issues[8][192];

	peer = -1;
	memset(&b, 0, sizeof(b));
	doc_init(&doc);

	doc_parse(&doc, config ? config : "");
	iface = doc_interface(&doc);
	npeer = doc_peers(&doc, peers, 4);

	if (npeer > 0) {
		peer = peers[0];
	}

	endpoint[0] = 0;
	address[0] = 0;
	pub[0] = 0;

	if (peer >= 0) {
		doc_value(&doc, peer, "Endpoint", endpoint, (int)sizeof(endpoint));
	}

	doc_addresses(&doc, address, (int)sizeof(address));

	if (iface >= 0) {
		doc_value(&doc, iface, "PrivateKey", value, (int)sizeof(value));
		key_public(value, pub, (int)sizeof(pub));
	}

	ni = config_issues(&doc, issues, 8);
	np = amnezia_present(&doc, present, 32);

	buf_add(&b, "{\"endpoint\":");
	buf_esc(&b, endpoint[0] ? endpoint : "no endpoint");

	buf_add(&b, ",\"address\":");
	buf_esc(&b, address);

	buf_add(&b, ",\"publicKey\":");
	buf_esc(&b, pub);

	buf_add(&b, amnezia_is(&doc) ? ",\"amnezia\":true" : ",\"amnezia\":false");
	buf_add(&b, ",\"issues\":[");

	for (i = 0; i < ni; i++) {
		if (i) {
			buf_add(&b, ",");
		}

		buf_esc(&b, issues[i]);
	}

	buf_add(&b, "],\"interface\":{");
	first = 1;

	if (iface >= 0) {
		for (i = 0; i < 5; i++) {
			doc_value(&doc, iface, iface_keys[i], value, (int)sizeof(value));
			put_field(&b, &first, iface_keys[i], value);
		}

		for (i = 0; i < np; i++) {
			doc_value(&doc, iface, present[i], value, (int)sizeof(value));
			put_field(&b, &first, present[i], value);
		}
	}

	buf_add(&b, "},\"peer\":{");
	first = 1;

	if (peer >= 0) {
		for (i = 0; i < 5; i++) {
			doc_value(&doc, peer, peer_keys[i], value, (int)sizeof(value));
			put_field(&b, &first, peer_keys[i], value);
		}
	}

	buf_add(&b, "},\"amneziaKeys\":[");

	for (i = 0; i < np; i++) {
		if (i) {
			buf_add(&b, ",");
		}

		buf_esc(&b, present[i]);
	}

	buf_add(&b, "]}");

	doc_free(&doc);
	return b.data ? b.data : _strdup("{}");
}

/*
===============
apply_object
Write each string field of a JSON object into the section.
===============
*/
static void apply_object(doc_t *doc, int section, const json_t *obj) {
	const json_t	*p;
	const char		*value;

	if (!obj || section < 0) {
		return;
	}

	for (p = obj->child; p; p = p->next) {
		value = json_string(p);

		if (p->key && value) {
			doc_set(doc, section, p->key, value);
		}
	}
}

/*
===============
abi_apply
Patch interface and peer fields and return the rendered config.
===============
*/
CORE_API char *abi_apply(const char *config, const char *fields_json) {
	doc_t	doc;
	arena_t	arena;
	json_t	*root;
	char		*rendered;
	int			iface;
	int			peers[4];
	int			peer;

	doc_init(&doc);
	doc_parse(&doc, config ? config : "");

	arena_init(&arena, 1 << 16);
	root = fields_json ? json_parse(&arena, fields_json) : NULL;
	iface = doc_ensure_interface(&doc);

	if (root) {
		apply_object(&doc, iface, json_get(root, "interface"));
	}

	if (doc_peers(&doc, peers, 4) == 0) {
		doc_add_peer(&doc);
	}

	doc_peers(&doc, peers, 4);
	peer = peers[0];

	if (root) {
		apply_object(&doc, peer, json_get(root, "peer"));
	}

	rendered = doc_render(&doc);
	doc_free(&doc);
	arena_free(&arena);

	return rendered ? rendered : _strdup("");
}

/*
===============
abi_blank
A new config with a fresh key pair.
===============
*/
CORE_API char *abi_blank(void) {
	doc_t	doc;
	char		priv[128];
	char		pub[128];
	char		*rendered;

	key_generate(priv, (int)sizeof(priv), pub, (int)sizeof(pub));

	doc_init(&doc);
	doc_blank(&doc, priv);

	rendered = doc_render(&doc);
	doc_free(&doc);

	return rendered ? rendered : _strdup("");
}

/*
===============
abi_keypair
Write a new private and public key.
===============
*/
CORE_API int abi_keypair(char *priv, int priv_cap, char *pub, int pub_cap) {
	return key_generate(priv, priv_cap, pub, pub_cap);
}

/*
===============
abi_import
Normalize pasted text. The return code is IMPORT_*.
===============
*/
CORE_API int abi_import(const char *text, char *config, int config_cap, char *name, int name_cap) {
	return import_normalize(text ? text : "", config, config_cap, name, name_cap);
}

/*
===============
abi_classify
One route line: an address, a domain, or an error.
===============
*/
CORE_API int abi_classify(const char *line, char *kind, int kind_cap, char *value, int value_cap, char *note, int note_cap, char *error, int error_cap) {
	char		copy[512];
	char		*sp;
	cidr_t	cidr;

	if (kind && kind_cap) {
		kind[0] = 0;
	}

	if (value && value_cap) {
		value[0] = 0;
	}

	if (note && note_cap) {
		note[0] = 0;
	}

	if (error && error_cap) {
		error[0] = 0;
	}

	snprintf(copy, sizeof(copy), "%s", line ? line : "");
	str_trim(copy);

	if (!copy[0]) {
		return 0;
	}

	sp = copy;

	while (*sp && *sp != ' ' && *sp != '\t') {
		sp++;
	}

	if (*sp) {
		*sp++ = 0;

		while (*sp == ' ' || *sp == '\t') {
			sp++;
		}

		if (note && note_cap) {
			snprintf(note, (size_t)note_cap, "%s", sp);
		}
	}

	if (cidr_parse(copy, &cidr)) {
		if (cidr.prefix == 0) {
			if (error && error_cap) {
				snprintf(error, (size_t)error_cap, "That network is too broad and would disable the whole tunnel.");
			}

			if (kind && kind_cap) {
				snprintf(kind, (size_t)kind_cap, "error");
			}

			return 0;
		}

		cidr_format(&cidr, value, value_cap);

		if (kind && kind_cap) {
			snprintf(kind, (size_t)kind_cap, "ip");
		}

		return 1;
	}

	if (strchr(copy, '*')) {
		if (error && error_cap) {
			snprintf(error, (size_t)error_cap, "Wildcards such as *.example.com are not supported.");
		}

		if (kind && kind_cap) {
			snprintf(kind, (size_t)kind_cap, "error");
		}

		return 0;
	}

	if (domain_normalize(copy, value, value_cap)) {
		if (kind && kind_cap) {
			snprintf(kind, (size_t)kind_cap, "domain");
		}

		return 1;
	}

	if (error && error_cap) {
		snprintf(error, (size_t)error_cap, "Enter an IP, a network, or a domain.");
	}

	if (kind && kind_cap) {
		snprintf(kind, (size_t)kind_cap, "error");
	}

	return 0;
}

