//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// tunnel.c -- Document, keys, Amnezia, UAPI.
//
//================================================================

#include "tunnel.h"
#include "base.h"
#include "net.h"
#include "qcompress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
===============
doc_init
Zero a document.
===============
*/
void doc_init(doc_t *doc) {
	memset(doc, 0, sizeof(*doc));
}

/*
===============
free_entry
Release one field or raw line.
===============
*/
static void free_entry(entry_t *e) {
	free(e->raw);
	free(e->key);
	free(e->value);
	memset(e, 0, sizeof(*e));
}

/*
===============
doc_free
Release every section and the preamble.
===============
*/
void doc_free(doc_t *doc) {
	int		s;
	int		e;
	int		i;

	if (!doc) {
		return;
	}

	for (i = 0; i < doc->preamble_count; i++) {
		free(doc->preamble[i]);
	}

	free(doc->preamble);

	for (s = 0; s < doc->section_count; s++) {
		free(doc->sections[s].name);

		for (e = 0; e < doc->sections[s].count; e++) {
			free_entry(&doc->sections[s].entries[e]);
		}

		free(doc->sections[s].entries);
	}

	free(doc->sections);
	memset(doc, 0, sizeof(*doc));
}

/*
===============
add_preamble
Keep a line that sat above the first section.
===============
*/
static void add_preamble(doc_t *doc, const char *line) {
	int		cap;
	char	**next;

	if (doc->preamble_count >= doc->preamble_cap) {
		cap = doc->preamble_cap ? doc->preamble_cap * 2 : 8;
		next = (char **)realloc(doc->preamble, (size_t)cap * sizeof(char *));

		if (!next) {
			return;
		}

		doc->preamble = next;
		doc->preamble_cap = cap;
	}

	doc->preamble[doc->preamble_count++] = str_dup(line);
}

/*
===============
add_section
Append a named section. Returns NULL if the grow fails.
===============
*/
static section_t *add_section(doc_t *doc, const char *name) {
	section_t	*s;
	int				cap;
	section_t	*next;

	if (doc->section_count >= doc->section_cap) {
		cap = doc->section_cap ? doc->section_cap * 2 : 4;
		next = (section_t *)realloc(doc->sections, (size_t)cap * sizeof(section_t));

		if (!next) {
			return NULL;
		}

		doc->sections = next;
		doc->section_cap = cap;
	}

	s = &doc->sections[doc->section_count++];
	memset(s, 0, sizeof(*s));
	s->name = str_dup(name);
	return s;
}

/*
===============
add_entry
Append a field or a raw line. Drops the entry if the grow fails.
===============
*/
static void add_entry(section_t *s, entry_t entry) {
	int			cap;
	entry_t	*next;

	if (s->count >= s->cap) {
		cap = s->cap ? s->cap * 2 : 8;
		next = (entry_t *)realloc(s->entries, (size_t)cap * sizeof(entry_t));

		if (!next) {
			free_entry(&entry);
			return;
		}

		s->entries = next;
		s->cap = cap;
	}

	s->entries[s->count++] = entry;
}

/*
===============
doc_parse
Replace doc with the sections in text.
===============
*/
void doc_parse(doc_t *doc, const char *text) {
	const char		*p;
	int				saw;
	char			line[4096];
	const char		*eol;
	int				n;
	section_t	*section;
	char			*eq;
	int				kn;

	saw = 0;

	doc_free(doc);
	doc_init(doc);

	if (!text) {
		return;
	}

	p = text;

	while (*p) {
		eol = strchr(p, '\n');
		n = eol ? (int)(eol - p) : (int)strlen(p);
		char trimmed[4096];

		if (n > (int)sizeof(line) - 1) {
			n = (int)sizeof(line) - 1;
		}

		memcpy(line, p, (size_t)n);
		line[n] = '\0';

		if (n > 0 && line[n - 1] == '\r') {
			line[--n] = '\0';
		}

		snprintf(trimmed, sizeof(trimmed), "%s", line);
		str_trim(trimmed);

		if (trimmed[0] == '[' && trimmed[strlen(trimmed) - 1] == ']' && strlen(trimmed) > 2) {
			char name[256];

			saw = 1;
			snprintf(name, sizeof(name), "%s", trimmed + 1);
			name[strlen(name) - 1] = '\0';
			str_trim(name);
			add_section(doc, name);
		} else if (!saw) {
			add_preamble(doc, line);
		} else if (doc->section_count > 0) {
			entry_t entry;

			memset(&entry, 0, sizeof(entry));
			section = &doc->sections[doc->section_count - 1];

			if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
				entry.raw = str_dup(line);
				add_entry(section, entry);
			} else if ((eq = strchr(line, '=')) != NULL) {
				char key[256];
				char value[2048];

				kn = (int)(eq - line);

				if (kn > (int)sizeof(key) - 1) {
					kn = (int)sizeof(key) - 1;
				}

				memcpy(key, line, (size_t)kn);
				key[kn] = '\0';
				str_trim(key);
				snprintf(value, sizeof(value), "%s", eq + 1);
				str_trim(value);
				entry.is_field = 1;
				entry.key = str_dup(key);
				entry.value = str_dup(value);
				add_entry(section, entry);
			} else {
				entry.raw = str_dup(line);
				add_entry(section, entry);
			}
		}

		if (!eol) {
			break;
		}

		p = eol + 1;
	}
}

/*
===============
doc_render
Render the document. The caller frees the string. It ends with a newline.
===============
*/
char *doc_render(const doc_t *doc) {
	size_t				cap;
	size_t				used;
	char				*buf;
	int					s;
	int					e;
	int					last_empty;
	const entry_t	*en;

	cap = 1024;
	used = 0;

	buf = (char *)malloc(cap);
	last_empty = 1;

	if (!buf) {
		return NULL;
	}

	buf[0] = '\0';
#define APPEND(str) do { \
		size_t add = strlen(str); \
		if (used + add + 2 > cap) { \
			while (used + add + 2 > cap) { \
				cap *= 2; \
			} \
			buf = (char *)realloc(buf, cap); \
			if (!buf) { \
				return NULL; \
			} \
		} \
		memcpy(buf + used, str, add); \
		used += add; \
		buf[used] = '\0'; \
	} while (0)

	for (e = 0; e < doc->preamble_count; e++) {
		APPEND(doc->preamble[e] ? doc->preamble[e] : "");
		APPEND("\n");
		last_empty = doc->preamble[e] && doc->preamble[e][0] == '\0';
	}

	if (doc->preamble_count > 0 && !last_empty && doc->section_count > 0) {
		APPEND("\n");
		last_empty = 1;
	}

	for (s = 0; s < doc->section_count; s++) {
		char header[300];

		if (s > 0 && !last_empty) {
			APPEND("\n");
		}

		snprintf(header, sizeof(header), "[%s]\n", doc->sections[s].name ? doc->sections[s].name : "");
		APPEND(header);
		last_empty = 0;

		for (e = 0; e < doc->sections[s].count; e++) {
			en = &doc->sections[s].entries[e];

			if (en->is_field && en->key) {
				char row[2400];

				snprintf(row, sizeof(row), "%s = %s\n", en->key, en->value ? en->value : "");
				APPEND(row);
				last_empty = 0;
			} else {
				APPEND(en->raw ? en->raw : "");
				APPEND("\n");
				last_empty = !en->raw || en->raw[0] == '\0';
			}
		}
	}

	if (used == 0 || buf[used - 1] != '\n') {
		APPEND("\n");
	}
#undef APPEND
	return buf;
}

/*
===============
doc_interface
Index of the Interface section, or -1.
===============
*/
int doc_interface(const doc_t *doc) {
	int		i;

	for (i = 0; i < doc->section_count; i++) {
		if (str_icmp(doc->sections[i].name, "Interface") == 0) {
			return i;
		}
	}

	return -1;
}

/*
===============
doc_peers
Write Peer section indexes. Returns how many fit in out.
===============
*/
int doc_peers(const doc_t *doc, int *out, int cap) {
	int		i;
	int		n;

	n = 0;

	for (i = 0; i < doc->section_count; i++) {
		if (str_icmp(doc->sections[i].name, "Peer") == 0) {
			if (n < cap) {
				out[n] = i;
			}

			n++;
		}
	}

	return n < cap ? n : cap;
}

/*
===============
join_values
Collect every value of key. split breaks comma lists into one list.
===============
*/
static void join_values(const doc_t *doc, int section, const char *key, char *buf, int cap, int split) {
	int					i;
	int					first;
	const entry_t	*e;
	int					c;
	int					k;

	first = 1;

	if (!buf || cap <= 0) {
		return;
	}

	buf[0] = '\0';

	if (section < 0 || section >= doc->section_count) {
		return;
	}

	for (i = 0; i < doc->sections[section].count; i++) {
		e = &doc->sections[section].entries[i];
		char piece[2048];

		if (!e->is_field || !e->key || str_icmp(e->key, key) != 0) {
			continue;
		}

		snprintf(piece, sizeof(piece), "%s", e->value ? e->value : "");
		str_trim(piece);

		if (piece[0] == '\0') {
			continue;
		}

		if (split) {
			char items[32][256];
			c = split_commas(piece, items, 32);

			for (k = 0; k < c; k++) {
				if (!first && (int)strlen(buf) + 2 < cap) {
					strncat(buf, ", ", (size_t)cap - strlen(buf) - 1);
				}

				strncat(buf, items[k], (size_t)cap - strlen(buf) - 1);
				first = 0;
			}
		} else {
			if (!first && (int)strlen(buf) + 2 < cap) {
				strncat(buf, ", ", (size_t)cap - strlen(buf) - 1);
			}

			strncat(buf, piece, (size_t)cap - strlen(buf) - 1);
			first = 0;
		}
	}
}

/*
===============
doc_value
First value of key, or the joined values when the key repeats.
===============
*/
void doc_value(const doc_t *doc, int section, const char *key, char *buf, int cap) {
	join_values(doc, section, key, buf, cap, 0);
}

/*
===============
doc_addresses
Interface Address list.
===============
*/
void doc_addresses(const doc_t *doc, char *buf, int cap) {
	int		iface;

	iface = doc_interface(doc);
	join_values(doc, iface, "Address", buf, cap, 1);
}

/*
===============
doc_dns
Interface DNS list.
===============
*/
void doc_dns(const doc_t *doc, char *buf, int cap) {
	int		iface;

	iface = doc_interface(doc);
	join_values(doc, iface, "DNS", buf, cap, 1);
}

/*
===============
doc_peer_allowed
AllowedIPs of one peer.
===============
*/
void doc_peer_allowed(const doc_t *doc, int section, char *buf, int cap) {
	join_values(doc, section, "AllowedIPs", buf, cap, 1);
}

/*
===============
canonical_key
The spelling written back into the config.
===============
*/
static const char *canonical_key(const char *key) {
	if (str_icmp(key, "privatekey") == 0) {
		return "PrivateKey";
	}

	if (str_icmp(key, "address") == 0) {
		return "Address";
	}

	if (str_icmp(key, "dns") == 0) {
		return "DNS";
	}

	if (str_icmp(key, "mtu") == 0) {
		return "MTU";
	}

	if (str_icmp(key, "listenport") == 0) {
		return "ListenPort";
	}

	if (str_icmp(key, "publickey") == 0) {
		return "PublicKey";
	}

	if (str_icmp(key, "presharedkey") == 0) {
		return "PresharedKey";
	}

	if (str_icmp(key, "endpoint") == 0) {
		return "Endpoint";
	}

	if (str_icmp(key, "allowedips") == 0) {
		return "AllowedIPs";
	}

	if (str_icmp(key, "persistentkeepalive") == 0) {
		return "PersistentKeepalive";
	}

	if (str_icmp(key, "table") == 0) {
		return "Table";
	}

	if (str_icmp(key, "jc") == 0) {
		return "Jc";
	}

	if (str_icmp(key, "jmin") == 0) {
		return "Jmin";
	}

	if (str_icmp(key, "jmax") == 0) {
		return "Jmax";
	}

	if (str_icmp(key, "s1") == 0) {
		return "S1";
	}

	if (str_icmp(key, "s2") == 0) {
		return "S2";
	}

	if (str_icmp(key, "s3") == 0) {
		return "S3";
	}

	if (str_icmp(key, "s4") == 0) {
		return "S4";
	}

	if (str_icmp(key, "h1") == 0) {
		return "H1";
	}

	if (str_icmp(key, "h2") == 0) {
		return "H2";
	}

	if (str_icmp(key, "h3") == 0) {
		return "H3";
	}

	if (str_icmp(key, "h4") == 0) {
		return "H4";
	}

	if (str_icmp(key, "i1") == 0) {
		return "I1";
	}

	if (str_icmp(key, "i2") == 0) {
		return "I2";
	}

	if (str_icmp(key, "i3") == 0) {
		return "I3";
	}

	if (str_icmp(key, "i4") == 0) {
		return "I4";
	}

	if (str_icmp(key, "i5") == 0) {
		return "I5";
	}

	if (str_icmp(key, "headerprotectionkey") == 0) {
		return "HeaderProtectionKey";
	}

	if (str_icmp(key, "contentpaddingaddition") == 0) {
		return "ContentPaddingAddition";
	}

	if (str_icmp(key, "rekeyaftertime") == 0) {
		return "RekeyAfterTime";
	}

	if (str_icmp(key, "rekeytimeout") == 0) {
		return "RekeyTimeout";
	}

	if (str_icmp(key, "rejectaftertime") == 0) {
		return "RejectAfterTime";
	}

	if (str_icmp(key, "keepalivetimeout") == 0) {
		return "KeepaliveTimeout";
	}

	if (str_icmp(key, "maxhandshakeattempts") == 0) {
		return "MaxHandshakeAttempts";
	}

	if (str_icmp(key, "randomtrailers") == 0) {
		return "RandomTrailers";
	}

	if (str_icmp(key, "disablecookies") == 0) {
		return "DisableCookies";
	}

	return key;
}

/*
===============
doc_set
Replace every value of key. An empty value deletes the key.
===============
*/
void doc_set(doc_t *doc, int section, const char *key, const char *value) {
	section_t	*s;
	char			trimmed[2048];
	int				first;
	int				i;
	int				w;
	const char		*canon;
	entry_t		entry;
	int				cap;
	entry_t		*next;

	first = -1;

	if (section < 0 || section >= doc->section_count || !key) {
		return;
	}

	s = &doc->sections[section];
	snprintf(trimmed, sizeof(trimmed), "%s", value ? value : "");
	str_trim(trimmed);
	canon = canonical_key(key);

	for (i = 0; i < s->count; i++) {
		if (s->entries[i].is_field && s->entries[i].key && str_icmp(s->entries[i].key, key) == 0) {
			if (first < 0) {
				first = i;
			}
		}
	}

	if (first >= 0) {
		w = 0;

		for (i = 0; i < s->count; i++) {
			if (s->entries[i].is_field && s->entries[i].key && str_icmp(s->entries[i].key, key) == 0) {
				free_entry(&s->entries[i]);
				continue;
			}

			s->entries[w++] = s->entries[i];
		}

		s->count = w;

		if (first > s->count) {
			first = s->count;
		}
	}

	if (trimmed[0] == '\0') {
		return;
	}

	{
		memset(&entry, 0, sizeof(entry));
		entry.is_field = 1;
		entry.key = str_dup(canon);
		entry.value = str_dup(trimmed);

		if (first < 0) {
			add_entry(s, entry);
		} else {
			if (s->count >= s->cap) {
				cap = s->cap ? s->cap * 2 : 8;
				next = (entry_t *)realloc(s->entries, (size_t)cap * sizeof(entry_t));

				if (!next) {
					free_entry(&entry);
					return;
				}

				s->entries = next;
				s->cap = cap;
			}

			memmove(&s->entries[first + 1], &s->entries[first], (size_t)(s->count - first) * sizeof(entry_t));
			s->entries[first] = entry;
			s->count++;
		}
	}
}

/*
===============
doc_ensure_interface
Interface index, creating the section at the front when it is missing.
===============
*/
int doc_ensure_interface(doc_t *doc) {
	int				iface;
	section_t	section;
	int				cap;
	section_t	*next;

	iface = doc_interface(doc);

	if (iface >= 0) {
		return iface;
	}

	memset(&section, 0, sizeof(section));
	section.name = str_dup("Interface");

	if (doc->section_count >= doc->section_cap) {
		cap = doc->section_cap ? doc->section_cap * 2 : 4;
		next = (section_t *)realloc(doc->sections, (size_t)cap * sizeof(section_t));

		if (!next) {
			free(section.name);
			return -1;
		}

		doc->sections = next;
		doc->section_cap = cap;
	}

	memmove(&doc->sections[1], &doc->sections[0], (size_t)doc->section_count * sizeof(section_t));
	doc->sections[0] = section;
	doc->section_count++;
	return 0;
}

/*
===============
doc_add_peer
Append a Peer section and return its index.
===============
*/
int doc_add_peer(doc_t *doc) {
	add_section(doc, "Peer");
	return doc->section_count - 1;
}

/*
===============
doc_blank
A new tunnel: one interface, one empty peer.
===============
*/
void doc_blank(doc_t *doc, const char *private_key) {
	int		iface;
	int		peer;

	doc_free(doc);
	doc_init(doc);
	iface = doc_ensure_interface(doc);
	doc_set(doc, iface, "PrivateKey", private_key);
	doc_set(doc, iface, "Address", "10.8.0.2/32");
	doc_set(doc, iface, "DNS", "1.1.1.1");
	peer = doc_add_peer(doc);
	doc_set(doc, peer, "PublicKey", "");
	doc_set(doc, peer, "AllowedIPs", "0.0.0.0/0, ::/0");
	doc_set(doc, peer, "Endpoint", "");
	doc_set(doc, peer, "PersistentKeepalive", "25");
}

/*
===============
split_commas
Split a comma list into trimmed tokens. Returns how many were written.
===============
*/
int split_commas(const char *text, char out[][256], int cap) {
	const char	*p;
	int			count;
	const char	*comma;
	int			n;

	p = text ? text : "";
	count = 0;

	while (*p && count < cap) {
		comma = strchr(p, ',');
		n = comma ? (int)(comma - p) : (int)strlen(p);

		if (n > 255) {
			n = 255;
		}

		memcpy(out[count], p, (size_t)n);
		out[count][n] = '\0';
		str_trim(out[count]);

		if (out[count][0] != '\0') {
			count++;
		}

		if (!comma) {
			break;
		}

		p = comma + 1;
	}

	return count;
}


#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

int curve25519_donna(unsigned char *mypublic, const unsigned char *secret, const unsigned char *basepoint);

/*
===============
b64_val
One base64 digit, or -1.
===============
*/
static int b64_val(char c) {
	if (c >= 'A' && c <= 'Z') {
		return c - 'A';
	}

	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	}

	if (c >= '0' && c <= '9') {
		return c - '0' + 52;
	}

	if (c == '+' || c == '-') {
		return 62;
	}

	if (c == '/' || c == '_') {
		return 63;
	}

	return -1;
}

/*
===============
b64_decode
Decode standard or url base64. Returns the byte count, or -1.
===============
*/
int b64_decode(const char *text, unsigned char *out, int cap) {
	int		val;
	int		bits;
	int		n;
	int		d;

	val = 0;
	bits = 0;
	n = 0;

	if (!text || !out) {
		return -1;
	}

	for (; *text; text++) {
		if (*text == '=' || *text == '\n' || *text == '\r' || *text == ' ') {
			if (*text == '=') {
				break;
			}

			continue;
		}

		d = b64_val(*text);

		if (d < 0) {
			return -1;
		}

		val = (val << 6) | d;
		bits += 6;

		if (bits >= 8) {
			bits -= 8;

			if (n >= cap) {
				return -1;
			}

			out[n++] = (unsigned char)((val >> bits) & 0xff);
		}
	}

	return n;
}

/*
===============
b64_encode
Standard base64 with padding. Returns the character count, or -1.
===============
*/
int b64_encode(const unsigned char *in, int len, char *out, int cap) {
	static const char	tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int					i;
	int					n;
	int					remain;
	unsigned int		v;

	i = 0;
	n = 0;

	if (!out || cap <= 0) {
		return -1;
	}

	while (i < len) {
		remain = len - i;

		v = (unsigned int)in[i] << 16;

		if (remain > 1) {
			v |= (unsigned int)in[i + 1] << 8;
		}

		if (remain > 2) {
			v |= in[i + 2];
		}

		if (n + 4 >= cap) {
			return -1;
		}

		out[n++] = tab[(v >> 18) & 63];
		out[n++] = tab[(v >> 12) & 63];
		out[n++] = remain > 1 ? tab[(v >> 6) & 63] : '=';
		out[n++] = remain > 2 ? tab[v & 63] : '=';
		i += 3;
	}

	out[n] = '\0';
	return n;
}

/*
===============
key_is
True when text is a 32-byte WireGuard key.
===============
*/
int key_is(const char *text) {
	unsigned char	raw[33];
	int				n;
	char			buf[128];

	if (!text) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", text);
	str_trim(buf);
	n = b64_decode(buf, raw, 33);
	return n == 32;
}

/*
===============
key_hex
64 lowercase hex digits, or 0.
===============
*/
int key_hex(const char *b64, char *hex, int cap) {
	unsigned char	raw[32];
	char			buf[128];
	int				n;
	int				i;

	if (!hex || cap < 65 || !b64) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", b64);
	str_trim(buf);
	n = b64_decode(buf, raw, 32);

	if (n != 32) {
		return 0;
	}

	for (i = 0; i < 32; i++) {
		sprintf(hex + i * 2, "%02x", raw[i]);
	}

	hex[64] = '\0';
	return 1;
}

/*
===============
hex_b64
Hex back to base64. An empty hex string writes an empty key.
===============
*/
int hex_b64(const char *hex, char *b64, int cap) {
	unsigned char	raw[32];
	int				i;
	int				n;
	unsigned int	v;

	i = 0;
	n = 0;

	if (!hex || !b64) {
		return 0;
	}

	while (hex[i] && hex[i + 1] && n < 32) {
		if (sscanf(hex + i, "%2x", &v) != 1) {
			break;
		}

		raw[n++] = (unsigned char)v;
		i += 2;
	}

	if (n == 0) {
		b64[0] = '\0';
		return 1;
	}

	return b64_encode(raw, n, b64, cap) > 0;
}

/*
===============
public_from_raw
Curve25519 public key for a 32-byte secret.
===============
*/
static int public_from_raw(const unsigned char secret[32], char *public_b64, int cap) {
	unsigned char	base[32];
	unsigned char	pub[32];

	memset(base, 0, sizeof(base));
	base[0] = 9;
	curve25519_donna(pub, secret, base);
	return b64_encode(pub, 32, public_b64, cap) > 0;
}

/*
===============
key_public
Public key for a base64 private key.
===============
*/
int key_public(const char *private_b64, char *public_b64, int cap) {
	unsigned char	raw[32];
	char			buf[128];
	int				n;

	if (!private_b64 || !public_b64) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", private_b64);
	str_trim(buf);
	n = b64_decode(buf, raw, 32);

	if (n != 32) {
		return 0;
	}

	return public_from_raw(raw, public_b64, cap);
}

/*
===============
key_generate
A new key pair. Windows uses BCrypt, anything else /dev/urandom.
===============
*/
int key_generate(char *private_b64, int private_cap, char *public_b64, int public_cap) {
	unsigned char	raw[32];
	FILE			*f;

#ifdef _WIN32
	if (BCryptGenRandom(NULL, raw, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
		return 0;
	}
#else
	f = fopen("/dev/urandom", "rb");

	if (!f || fread(raw, 1, 32, f) != 32) {
		if (f) {
			fclose(f);
		}

		return 0;
	}

	fclose(f);
#endif
	if (b64_encode(raw, 32, private_b64, private_cap) <= 0) {
		return 0;
	}

	return public_from_raw(raw, public_b64, public_cap);
}



static const char *s_ordered_keys[] = {
	"Jc", "Jmin", "Jmax",
	"S1", "S2", "S3", "S4",
	"H1", "H2", "H3", "H4",
	"I1", "I2", "I3", "I4", "I5",
	"HeaderProtectionKey",
	"ContentPaddingAddition", "RekeyAfterTime", "RekeyTimeout", "RejectAfterTime",
	"KeepaliveTimeout", "MaxHandshakeAttempts",
	"RandomTrailers", "DisableCookies"
};
static const int s_ordered_count = 25;

/*
===============
key_index
Index in the Amnezia field order, or -1.
===============
*/
static int key_index(const char *key) {
	int		i;

	for (i = 0; i < s_ordered_count; i++) {
		if (strcmp(s_ordered_keys[i], key) == 0) {
			return i;
		}
	}

	return -1;
}

/*
===============
is_count
===============
*/
static int is_count(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 0 && i <= 2;
}

/*
===============
is_pad
===============
*/
static int is_pad(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 3 && i <= 6;
}

/*
===============
is_header
===============
*/
static int is_header(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 7 && i <= 10;
}

/*
===============
is_sig
===============
*/
static int is_sig(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 11 && i <= 15;
}

/*
===============
is_range
===============
*/
static int is_range(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 17 && i <= 22;
}

/*
===============
is_bool_key
===============
*/
static int is_bool_key(const char *k) {
	int		i;

	i = key_index(k);
	return i >= 23 && i <= 24;
}

/*
===============
uapi_name
The userspace name of an Amnezia field.
===============
*/
static const char *uapi_name(const char *key) {
	if (strcmp(key, "Jc") == 0) {
		return "jc";
	}

	if (strcmp(key, "Jmin") == 0) {
		return "jmin";
	}

	if (strcmp(key, "Jmax") == 0) {
		return "jmax";
	}

	if (strcmp(key, "S1") == 0) {
		return "s1";
	}

	if (strcmp(key, "S2") == 0) {
		return "s2";
	}

	if (strcmp(key, "S3") == 0) {
		return "s3";
	}

	if (strcmp(key, "S4") == 0) {
		return "s4";
	}

	if (strcmp(key, "H1") == 0) {
		return "h1";
	}

	if (strcmp(key, "H2") == 0) {
		return "h2";
	}

	if (strcmp(key, "H3") == 0) {
		return "h3";
	}

	if (strcmp(key, "H4") == 0) {
		return "h4";
	}

	if (strcmp(key, "I1") == 0) {
		return "i1";
	}

	if (strcmp(key, "I2") == 0) {
		return "i2";
	}

	if (strcmp(key, "I3") == 0) {
		return "i3";
	}

	if (strcmp(key, "I4") == 0) {
		return "i4";
	}

	if (strcmp(key, "I5") == 0) {
		return "i5";
	}

	if (strcmp(key, "HeaderProtectionKey") == 0) {
		return "header_protection_key";
	}

	if (strcmp(key, "ContentPaddingAddition") == 0) {
		return "content_padding_addition";
	}

	if (strcmp(key, "RekeyAfterTime") == 0) {
		return "rekey_after_time";
	}

	if (strcmp(key, "RekeyTimeout") == 0) {
		return "rekey_timeout";
	}

	if (strcmp(key, "RejectAfterTime") == 0) {
		return "reject_after_time";
	}

	if (strcmp(key, "KeepaliveTimeout") == 0) {
		return "keepalive_timeout";
	}

	if (strcmp(key, "MaxHandshakeAttempts") == 0) {
		return "max_handshake_attempts";
	}

	if (strcmp(key, "RandomTrailers") == 0) {
		return "random_trailers";
	}

	if (strcmp(key, "DisableCookies") == 0) {
		return "disable_cookies";
	}

	return NULL;
}

/*
===============
all_digits
A non-empty decimal string.
===============
*/
static int all_digits(const char *s) {
	if (!s || !s[0]) {
		return 0;
	}

	while (*s) {
		if (*s < '0' || *s > '9') {
			return 0;
		}

		s++;
	}

	return 1;
}

/*
===============
amnezia_uint_range
A number, or a-b, inside 0..maxv.
===============
*/
int amnezia_uint_range(const char *text, unsigned long long maxv) {
	char				buf[128];
	char				*dash;
	unsigned long long	a;
	unsigned long long	b;
	char				*end;

	if (!text) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", text);
	str_trim(buf);
	dash = strchr(buf, '-');

	if (dash && strchr(dash + 1, '-')) {
		return 0;
	}

	if (dash) {
		*dash = '\0';

		if (!all_digits(buf) || !all_digits(dash + 1)) {
			return 0;
		}

		a = strtoull(buf, &end, 10);

		if (*end) {
			return 0;
		}

		b = strtoull(dash + 1, &end, 10);

		if (*end) {
			return 0;
		}

		if (a > maxv || b > maxv || a > b) {
			return 0;
		}

		return 1;
	}

	if (!all_digits(buf)) {
		return 0;
	}

	a = strtoull(buf, &end, 10);

	if (*end || a > maxv) {
		return 0;
	}

	return 1;
}

/*
===============
bool_string
true or false, or NULL when the word is neither.
===============
*/
static const char *bool_string(const char *raw) {
	char	buf[32];

	snprintf(buf, sizeof(buf), "%s", raw ? raw : "");
	str_trim(buf);
	str_lower(buf);

	if (strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0 || strcmp(buf, "yes") == 0 || strcmp(buf, "on") == 0) {
		return "true";
	}

	if (strcmp(buf, "false") == 0 || strcmp(buf, "0") == 0 || strcmp(buf, "no") == 0 || strcmp(buf, "off") == 0) {
		return "false";
	}

	return NULL;
}

/*
===============
amnezia_present
Amnezia keys that have a value, in field order.
===============
*/
int amnezia_present(const doc_t *doc, const char **out, int cap) {
	int		iface;
	int		n;
	int		i;
	char	value[512];

	iface = doc_interface(doc);
	n = 0;

	if (iface < 0) {
		return 0;
	}

	for (i = 0; i < s_ordered_count; i++) {
		doc_value(doc, iface, s_ordered_keys[i], value, (int)sizeof(value));

		if (value[0] && n < cap) {
			out[n++] = s_ordered_keys[i];
		}
	}

	return n;
}

/*
===============
amnezia_is
True when any Amnezia key is set.
===============
*/
int amnezia_is(const doc_t *doc) {
	const char	*keys[32];

	return amnezia_present(doc, keys, 32) > 0;
}

/*
===============
uapi_value
Encode one Amnezia value for the userspace daemon.
===============
*/
static int uapi_value(const char *key, const char *raw, char *out, int cap) {
	char				buf[1024];
	unsigned long long	n;
	char				*end;
	const char			*b;

	snprintf(buf, sizeof(buf), "%s", raw);
	str_trim(buf);

	if (is_count(key)) {
		if (!all_digits(buf)) {
			return 0;
		}

		n = strtoull(buf, &end, 10);

		if (*end || n > 4294967295ull) {
			return 0;
		}

		snprintf(out, (size_t)cap, "%llu", n);
		return 1;
	}

	if (is_pad(key)) {
		if (!all_digits(buf)) {
			return 0;
		}

		n = strtoull(buf, &end, 10);

		if (*end || n > 65535ull) {
			return 0;
		}

		snprintf(out, (size_t)cap, "%llu", n);
		return 1;
	}

	if (is_header(key) || is_range(key)) {
		if (!amnezia_uint_range(buf, 4294967295ull)) {
			return 0;
		}

		snprintf(out, (size_t)cap, "%s", buf);
		return 1;
	}

	if (is_sig(key)) {
		if (strchr(buf, '\n') || strchr(buf, '\r') || strlen(buf) > 4096) {
			return 0;
		}

		snprintf(out, (size_t)cap, "%s", buf);
		return 1;
	}

	if (strcmp(key, "HeaderProtectionKey") == 0) {
		return key_hex(buf, out, cap);
	}

	if (is_bool_key(key)) {
		b = bool_string(buf);

		if (!b) {
			return 0;
		}

		snprintf(out, (size_t)cap, "%s", b);
		return 1;
	}

	return 0;
}

/*
===============
amnezia_uapi
Amnezia lines for a set message. Empty, and success, when the config is plain WireGuard.
===============
*/
int amnezia_uapi(const doc_t *doc, char *out, int cap) {
	int			iface;
	int			i;
	int			used;
	const char	*name;

	used = 0;

	if (!out || cap <= 0) {
		return 0;
	}

	out[0] = '\0';

	if (!amnezia_is(doc)) {
		return 1;
	}

	iface = doc_interface(doc);

	for (i = 0; i < s_ordered_count; i++) {
		char raw[1024];
		char value[1024];
		char line[1200];

		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		name = uapi_name(s_ordered_keys[i]);

		if (!name || !uapi_value(s_ordered_keys[i], raw, value, (int)sizeof(value))) {
			return 0;
		}

		snprintf(line, sizeof(line), "%s=%s\n", name, value);

		if (used + (int)strlen(line) >= cap) {
			return 0;
		}

		memcpy(out + used, line, strlen(line) + 1);
		used += (int)strlen(line);
	}

	return 1;
}

/*
===============
add_issue
Append a check message once.
===============
*/
static void add_issue(char out[][192], int *n, int cap, const char *msg) {
	int		i;

	if (*n >= cap) {
		return;
	}

	for (i = 0; i < *n; i++) {
		if (strcmp(out[i], msg) == 0) {
			return;
		}
	}

	snprintf(out[*n], 192, "%s", msg);
	(*n)++;
}

/*
===============
amnezia_issues
What is wrong with the Amnezia fields.
===============
*/
int amnezia_issues(const doc_t *doc, char out[][192], int cap) {
	int								iface;
	int								n;
	int								i;
	int								j;
	char							raw[1024];
	unsigned long long				counts_min;
	unsigned long long				counts_max;
	int								have_min;
	int								have_max;
	int								headers_ok;
	static const unsigned long long	defaults[4] = {1, 2, 3, 4};
	unsigned long long				v;
	char							*dash;
	int								overlap;
	char							hex[80];
	unsigned long long				pads[4];
	unsigned long long				headers[4][2];
	int								pads_n;
	int								bad;

	n = 0;

	counts_min = 0;
	counts_max = 0;
	have_min = 0;
	have_max = 0;
	headers_ok = 1;

	if (!amnezia_is(doc)) {
		return 0;
	}

	iface = doc_interface(doc);

	for (i = 0; i < 3; i++) {
		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		if (all_digits(raw)) {
			v = strtoull(raw, NULL, 10);

			if (v <= 4294967295ull) {
				if (i == 1) {
					counts_min = v;
					have_min = 1;
				}

				if (i == 2) {
					counts_max = v;
					have_max = 1;
				}

				continue;
			}
		}

		add_issue(out, &n, cap, "Jc, Jmin, and Jmax must be numbers, and Jmin cannot be greater than Jmax.");
	}

	if (have_min && have_max && counts_min > counts_max) {
		add_issue(out, &n, cap, "Jc, Jmin, and Jmax must be numbers, and Jmin cannot be greater than Jmax.");
	}

	for (i = 3; i <= 6; i++) {
		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		if (!all_digits(raw) || strtoull(raw, NULL, 10) > 65535ull) {
			add_issue(out, &n, cap, "S1, S2, S3, and S4 must be numbers from 0 to 65535.");
		}
	}

	for (i = 0; i < 4; i++) {
		doc_value(doc, iface, s_ordered_keys[7 + i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			headers[i][0] = headers[i][1] = defaults[i];
			continue;
		}

		if (!amnezia_uint_range(raw, 4294967295ull)) {
			headers_ok = 0;
			add_issue(out, &n, cap, "H1, H2, H3, and H4 must be a number or a range such as 123-456.");
		} else {
			char buf[128];

			snprintf(buf, sizeof(buf), "%s", raw);
			str_trim(buf);
			dash = strchr(buf, '-');

			if (dash) {
				*dash = '\0';
				headers[i][0] = strtoull(buf, NULL, 10);
				headers[i][1] = strtoull(dash + 1, NULL, 10);
			} else {
				headers[i][0] = headers[i][1] = strtoull(buf, NULL, 10);
			}
		}
	}

	if (headers_ok) {
		overlap = 0;

		for (i = 0; i < 4; i++) {
			for (j = i + 1; j < 4; j++) {
				if (headers[i][0] <= headers[j][1] && headers[j][0] <= headers[i][1]) {
					overlap = 1;
				}
			}
		}

		if (overlap) {
			add_issue(out, &n, cap, "H1, H2, H3, and H4 must not overlap. Unset headers stay at 1, 2, 3, and 4.");
		}
	}

	for (i = 11; i <= 15; i++) {
		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		if (strchr(raw, '\n') || strchr(raw, '\r') || strlen(raw) > 4096) {
			add_issue(out, &n, cap, "Signature packets must stay on one line.");
		}
	}

	doc_value(doc, iface, "HeaderProtectionKey", raw, (int)sizeof(raw));

	if (raw[0]) {
		pads_n = 0;
		bad = 0;

		if (!key_hex(raw, hex, (int)sizeof(hex))) {
			add_issue(out, &n, cap, "HeaderProtectionKey must be a WireGuard key.");
		}

		for (i = 3; i <= 6; i++) {
			char s[64];

			doc_value(doc, iface, s_ordered_keys[i], s, (int)sizeof(s));

			if (all_digits(s)) {
				pads[pads_n++] = strtoull(s, NULL, 10);
			}
		}

		if (pads_n != 4) {
			bad = 1;
		}

		for (i = 0; i < pads_n; i++) {
			if (pads[i] < 12) {
				bad = 1;
			}
		}

		if (bad) {
			add_issue(out, &n, cap, "Header protection needs S1, S2, S3, and S4 of at least 12.");
		}
	}

	for (i = 17; i <= 22; i++) {
		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (raw[0] && !amnezia_uint_range(raw, 4294967295ull)) {
			add_issue(out, &n, cap, "An AmneziaWG value must be a number or a range such as 20-30.");
		}
	}

	for (i = 23; i <= 24; i++) {
		doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (raw[0] && !bool_string(raw)) {
			add_issue(out, &n, cap, "RandomTrailers and DisableCookies must be true or false.");
		}
	}

	return n;
}

/*
===============
parse_int_field
A decimal integer, or 0.
===============
*/
static int parse_int_field(const char *text, int *out) {
	char	*end;
	long	v;

	if (!text || !text[0]) {
		return 0;
	}

	v = strtol(text, &end, 10);

	if (*end) {
		return 0;
	}

	*out = (int)v;
	return 1;
}

/*
===============
config_issues
What would stop this config from coming up.
===============
*/
int config_issues(const doc_t *doc, char out[][192], int cap) {
	int		n;
	int		iface;
	char	value[1024];
	int		count;
	int		peers[16];
	int		peer_count;
	int		i;
	int		mtu;
	int		peer;
	int		k;
	int		kv;
	int		en;

	n = 0;

	iface = doc_interface(doc);
	char items[16][256];

	if (iface < 0) {
		add_issue(out, &n, cap, "The config has no Interface section.");
		return n;
	}

	doc_value(doc, iface, "PrivateKey", value, (int)sizeof(value));

	if (!key_is(value)) {
		add_issue(out, &n, cap, "The private key must be a WireGuard key.");
	}

	doc_addresses(doc, value, (int)sizeof(value));
	count = split_commas(value, items, 16);

	if (count == 0) {
		add_issue(out, &n, cap, "Set an interface address, for example 10.8.0.2/32.");
	} else {
		for (i = 0; i < count; i++) {
			cidr_t cidr;

			if (!cidr_parse(items[i], &cidr)) {
				add_issue(out, &n, cap, "Set an interface address, for example 10.8.0.2/32.");
				break;
			}
		}
	}

	doc_value(doc, iface, "MTU", value, (int)sizeof(value));

	if (value[0]) {
		if (!parse_int_field(value, &mtu)) {
			add_issue(out, &n, cap, "MTU must be a number.");
		} else if (mtu < 1280 || mtu > 1500) {
			add_issue(out, &n, cap, "MTU is usually between 1280 and 1500.");
		}
	}

	peer_count = doc_peers(doc, peers, 16);

	if (peer_count == 0) {
		add_issue(out, &n, cap, "Add at least one Peer.");
	}

	for (i = 0; i < peer_count; i++) {
		peer = peers[i];
		char keep[64];

		doc_value(doc, peer, "PublicKey", value, (int)sizeof(value));

		if (!key_is(value)) {
			add_issue(out, &n, cap, "A peer has an invalid public key.");
		}

		doc_value(doc, peer, "PresharedKey", value, (int)sizeof(value));

		if (value[0] && !key_is(value)) {
			add_issue(out, &n, cap, "PresharedKey is set, but it is not a WireGuard key.");
		}

		doc_value(doc, peer, "Endpoint", value, (int)sizeof(value));

		if (!endpoint_valid(value)) {
			add_issue(out, &n, cap, "Endpoint must look like host:port.");
		}

		doc_peer_allowed(doc, peer, value, (int)sizeof(value));
		count = split_commas(value, items, 16);

		if (count == 0) {
			add_issue(out, &n, cap, "AllowedIPs must list networks, for example 0.0.0.0/0.");
		} else {
			for (k = 0; k < count; k++) {
				cidr_t cidr;

				if (!cidr_parse(items[k], &cidr)) {
					add_issue(out, &n, cap, "AllowedIPs must list networks, for example 0.0.0.0/0.");
					break;
				}
			}
		}

		doc_value(doc, peer, "PersistentKeepalive", keep, (int)sizeof(keep));

		if (keep[0]) {
			if (amnezia_is(doc)) {
				if (!amnezia_uint_range(keep, 65535)) {
					add_issue(out, &n, cap, "PersistentKeepalive must be a number or a range such as 20-30.");
				}
			} else {
				kv = 0;

				if (!parse_int_field(keep, &kv)) {
					add_issue(out, &n, cap, "PersistentKeepalive must be a number.");
				} else if (kv < 0 || kv > 65535) {
					add_issue(out, &n, cap, "PersistentKeepalive is out of range.");
				}
			}
		}
	}

	{
		char extra[16][192];
		en = amnezia_issues(doc, extra, 16);

		for (i = 0; i < en; i++) {
			add_issue(out, &n, cap, extra[i]);
		}
	}

	return n;
}

/*
===============
tunnel_destinations
Routes the tunnel should own. A default route becomes the two /1 halves.
===============
*/
int tunnel_destinations(const doc_t *doc, char families[][8], char dests[][64], int cap) {
	int		peers[16];
	int		peer_count;
	int		n;
	int		i;
	int		k;
	int		count;

	peer_count = doc_peers(doc, peers, 16);
	n = 0;

	for (i = 0; i < peer_count; i++) {
		char allowed[1024];
		char items[16][256];
		doc_peer_allowed(doc, peers[i], allowed, (int)sizeof(allowed));
		count = split_commas(allowed, items, 16);

		for (k = 0; k < count; k++) {
			cidr_t cidr;

			if (!cidr_parse(items[k], &cidr)) {
				continue;
			}

			if (!cidr.ip.v6 && cidr.prefix == 0) {
				if (n < cap) {
					snprintf(families[n], 8, "inet");
					snprintf(dests[n], 64, "0.0.0.0/1");
					n++;
				}

				if (n < cap) {
					snprintf(families[n], 8, "inet");
					snprintf(dests[n], 64, "128.0.0.0/1");
					n++;
				}
			} else if (cidr.ip.v6 && cidr.prefix == 0) {
				if (n < cap) {
					snprintf(families[n], 8, "inet6");
					snprintf(dests[n], 64, "::/1");
					n++;
				}

				if (n < cap) {
					snprintf(families[n], 8, "inet6");
					snprintf(dests[n], 64, "8000::/1");
					n++;
				}
			} else if (n < cap) {
				snprintf(families[n], 8, "%s", cidr.ip.v6 ? "inet6" : "inet");
				cidr_format(&cidr, dests[n], 64);
				n++;
			}
		}
	}

	return n;
}

/*
===============
b64url_decode
url base64, as used by vpn://.
===============
*/
static int b64url_decode(const char *text, unsigned char *out, int cap) {
	int			n;
	int			pad;
	const char	*p;

	char tmp[1 << 16];
	n = 0;

	for (p = text; *p && n < (int)sizeof(tmp) - 4; p++) {
		if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
			continue;
		}

		if (*p == '-') {
			tmp[n++] = '+';
		} else if (*p == '_') {
			tmp[n++] = '/';
		} else {
			tmp[n++] = *p;
		}
	}

	tmp[n] = '\0';
	pad = (4 - (n % 4)) % 4;

	while (pad--) {
		tmp[n++] = '=';
	}

	tmp[n] = '\0';
	return b64_decode(tmp, out, cap);
}

/*
===============
contains_ci
ASCII case-insensitive substring.
===============
*/
static int contains_ci(const char *hay, const char *needle) {
	size_t		n;
	const char	*p;
	size_t		i;
	char		a;
	char		b;

	n = strlen(needle);

	for (p = hay; *p; p++) {
		for (i = 0; i < n; i++) {
			a = p[i];
			b = needle[i];

			if (a >= 'A' && a <= 'Z') {
				a = (char)(a - 'A' + 'a');
			}

			if (b >= 'A' && b <= 'Z') {
				b = (char)(b - 'A' + 'a');
			}

			if (a != b) {
				break;
			}
		}

		if (i == n) {
			return 1;
		}
	}

	return 0;
}

/*
===============
supported_kind
A WireGuard or AmneziaWG container name we can import.
===============
*/
static int supported_kind(const char *k) {
	return k && (strcmp(k, "amnezia-awg") == 0 || strcmp(k, "amnezia-wireguard") == 0 || strcmp(k, "awg") == 0 || strcmp(k, "wireguard") == 0);
}

/*
===============
kind_rank
Sort key so the preferred container is tried first.
===============
*/
static int kind_rank(const char *kind, const char *preferred) {
	if (preferred && strcmp(kind, preferred) == 0 && supported_kind(kind)) {
		return 0;
	}

	if (strcmp(kind, "amnezia-awg") == 0 || strcmp(kind, "awg") == 0) {
		return 1;
	}

	if (strcmp(kind, "amnezia-wireguard") == 0 || strcmp(kind, "wireguard") == 0) {
		return 2;
	}

	return 99;
}

/*
===============
copy_str
Copy into a capped buffer.
===============
*/
static void copy_str(char *dst, int cap, const char *src) {
	if (!dst || cap <= 0) {
		return;
	}

	snprintf(dst, (size_t)cap, "%s", src ? src : "");
}

/*
===============
jstr
String value of a key, or NULL.
===============
*/
static const char *jstr(const json_t *obj, const char *key) {
	return json_string(json_get(obj, key));
}

/*
===============
fill_key
Set key when the section does not already have it.
===============
*/
static void fill_key(doc_t *doc, int section, const char *key, const char *value) {
	char	cur[1024];

	if (!value || !value[0]) {
		return;
	}

	doc_value(doc, section, key, cur, (int)sizeof(cur));

	if (cur[0]) {
		return;
	}

	doc_set(doc, section, key, value);
}

/*
===============
normalize_addresses
Add /32 or /128 where a share left the prefix off.
===============
*/
static void normalize_addresses(const char *text, char *out, int cap) {
	int		n;
	int		i;
	int		used;

	char items[8][256];
	n = split_commas(text, items, 8);
	used = 0;
	out[0] = '\0';

	for (i = 0; i < n; i++) {
		char piece[300];

		if (strchr(items[i], '/')) {
			snprintf(piece, sizeof(piece), "%s", items[i]);
		} else if (strchr(items[i], ':')) {
			snprintf(piece, sizeof(piece), "%s/128", items[i]);
		} else {
			snprintf(piece, sizeof(piece), "%s/32", items[i]);
		}

		if (used && used + 2 < cap) {
			out[used++] = ',';
			out[used++] = ' ';
			out[used] = '\0';
		}

		snprintf(out + used, (size_t)(cap - used), "%s", piece);
		used = (int)strlen(out);
	}
}

/*
===============
make_endpoint
host:port, with brackets when host is an IPv6 literal.
===============
*/
static void make_endpoint(const char *host, const char *port, char *out, int cap) {
	if (strchr(host, ':') && host[0] != '[') {
		snprintf(out, (size_t)cap, "[%s]:%s", host, port);
	} else {
		snprintf(out, (size_t)cap, "%s:%s", host, port);
	}
}

/*
===============
render_proto
Turn one share object into a WireGuard config.
===============
*/
static int render_proto(const json_t *proto, const char **dns, int dns_count, const char *host, const char *port, const char *embedded, char *config, int cap) {
	doc_t		doc;
	int				iface;
	int				peers[4];
	int				peer;
	char			addr[256];
	char			dnsjoin[256];
	char			allowed_join[256];
	int				i;
	char			*rendered;
	const char		*priv;
	const char		*pub;
	const char		*ip;
	const json_t	*allowed;
	const char		*parts[8];
	int				c;
	const char		*eh;
	const char		*ep;
	char			endpoint[256];
	char			cur[256];

	doc_init(&doc);

	if (embedded && contains_ci(embedded, "[interface]")) {
		doc_parse(&doc, embedded);
	}

	iface = doc_ensure_interface(&doc);
	priv = jstr(proto, "client_priv_key");

	if (!priv) {
		priv = jstr(proto, "PrivateKey");
	}

	fill_key(&doc, iface, "PrivateKey", priv);
	doc_value(&doc, iface, "Address", addr, (int)sizeof(addr));

	if (!addr[0]) {
		ip = jstr(proto, "client_ip");

		if (!ip) {
			ip = jstr(proto, "Address");
		}

		if (ip) {
			normalize_addresses(ip, addr, (int)sizeof(addr));
			doc_set(&doc, iface, "Address", addr);
		}
	}

	doc_dns(&doc, dnsjoin, (int)sizeof(dnsjoin));

	if (!dnsjoin[0] && dns_count > 0) {
		dnsjoin[0] = '\0';

		for (i = 0; i < dns_count; i++) {
			if (i) {
				strncat(dnsjoin, ", ", sizeof(dnsjoin) - strlen(dnsjoin) - 1);
			}

			strncat(dnsjoin, dns[i], sizeof(dnsjoin) - strlen(dnsjoin) - 1);
		}

		doc_set(&doc, iface, "DNS", dnsjoin);
	}

	fill_key(&doc, iface, "MTU", jstr(proto, "mtu") ? jstr(proto, "mtu") : jstr(proto, "MTU"));

	for (i = 0; i < s_ordered_count; i++) {
		fill_key(&doc, iface, s_ordered_keys[i], jstr(proto, s_ordered_keys[i]));
	}

	if (doc_peers(&doc, peers, 4) == 0) {
		doc_add_peer(&doc);
	}

	peer = doc_peers(&doc, peers, 4) > 0 ? peers[0] : -1;

	if (peer < 0) {
		doc_free(&doc);
		return 0;
	}

	pub = jstr(proto, "server_pub_key");

	if (!pub) {
		pub = jstr(proto, "PublicKey");
	}

	fill_key(&doc, peer, "PublicKey", pub);
	fill_key(&doc, peer, "PresharedKey", jstr(proto, "psk_key") ? jstr(proto, "psk_key") : jstr(proto, "PresharedKey"));
	doc_peer_allowed(&doc, peer, allowed_join, (int)sizeof(allowed_join));

	if (!allowed_join[0]) {
		allowed = json_get(proto, "allowed_ips");
		c = json_strings(allowed, parts, 8);

		allowed_join[0] = '\0';

		if (allowed && allowed->type == JSON_ARR) {
			for (i = 0; i < c; i++) {
				if (i) {
					strncat(allowed_join, ", ", sizeof(allowed_join) - strlen(allowed_join) - 1);
				}

				strncat(allowed_join, parts[i], sizeof(allowed_join) - strlen(allowed_join) - 1);
			}
		} else if (allowed && allowed->type == JSON_STR && json_string(allowed)) {
			snprintf(allowed_join, sizeof(allowed_join), "%s", json_string(allowed));
		}

		if (allowed_join[0]) {
			doc_set(&doc, peer, "AllowedIPs", allowed_join);
		}
	}

	fill_key(&doc, peer, "PersistentKeepalive", jstr(proto, "persistent_keep_alive") ? jstr(proto, "persistent_keep_alive") : jstr(proto, "PersistentKeepalive"));
	{
		eh = jstr(proto, "hostName");
		ep = jstr(proto, "port");

		if (!eh) {
			eh = host;
		}

		if (!ep) {
			ep = port;
		}

		doc_value(&doc, peer, "Endpoint", cur, (int)sizeof(cur));

		if (!cur[0] && eh && ep) {
			make_endpoint(eh, ep, endpoint, (int)sizeof(endpoint));
			doc_set(&doc, peer, "Endpoint", endpoint);
		}
	}

	doc_value(&doc, iface, "PrivateKey", addr, (int)sizeof(addr));
	doc_value(&doc, peer, "PublicKey", allowed_join, (int)sizeof(allowed_join));

	if (!addr[0] || !allowed_join[0]) {
		doc_free(&doc);
		return 0;
	}

	rendered = doc_render(&doc);
	doc_free(&doc);

	if (!rendered) {
		return 0;
	}

	if ((int)strlen(rendered) >= cap) {
		free(rendered);
		return 0;
	}

	memcpy(config, rendered, strlen(rendered) + 1);
	free(rendered);
	return 1;
}

/*
===============
unwrap_last
last_config as an object, or as an embedded conf.
===============
*/
static int unwrap_last(arena_t *arena, const json_t *last, const json_t **obj, const char **conf) {
	const char	*text;
	json_t	*parsed;

	*obj = NULL;
	*conf = NULL;

	if (!last) {
		return 0;
	}

	if (last->type == JSON_OBJ) {
		*obj = last;
		return 1;
	}

	text = json_string(last);

	if (!text) {
		return 0;
	}

	if (text[0] == '{') {
		parsed = json_parse(arena, text);

		if (!parsed || parsed->type != JSON_OBJ) {
			return 0;
		}

		*obj = parsed;
		return 1;
	}

	if (contains_ci(text, "[interface]")) {
		*conf = text;
		return 1;
	}

	return 0;
}

/*
===============
from_object
Walk a share and write the first config we can run.
===============
*/
static int from_object(const json_t *object, const char **dns_in, int dns_in_count, const char *host, const char *port, const char *name_in, char *config, int config_cap, char *name, int name_cap) {
	const char		*servers[8];
	int				server_count;
	const char		*next_host;
	const char		*next_port;
	const char		*next_name;
	const json_t	*containers;
	const char		*preferred;
	int				i;
	arena_t		local;
	const char		*extra[8];
	const json_t	*d1;
	const json_t	*d2;
	int				c;
	int				looks;
	const char		*embedded;
	const json_t	*ranked[16];
	const char		*kinds[16];
	int				rc;
	const json_t	*child;
	int				saw;
	int				j;
	const char		*tk;
	const json_t	*tv;
	const char		*kind;
	const char		*protocol;
	const json_t	*box;
	const json_t	*last;
	const json_t	*proto;
	const char		*conf;
	const char		*box_port;
	const char		*mh;
	const char		*mp;

	server_count = 0;

	for (i = 0; i < dns_in_count && server_count < 8; i++) {
		servers[server_count++] = dns_in[i];
	}

	{
		d1 = json_get(object, "dns1");
		d2 = json_get(object, "dns2");
		c = json_strings(d1, extra, 8);

		if (c > 0) {
			server_count = 0;

			for (i = 0; i < c && server_count < 8; i++) {
				servers[server_count++] = extra[i];
			}

			c = json_strings(d2, extra, 8);

			for (i = 0; i < c && server_count < 8; i++) {
				servers[server_count++] = extra[i];
			}
		}
	}

	next_host = jstr(object, "hostName") ? jstr(object, "hostName") : host;
	next_port = jstr(object, "port") ? jstr(object, "port") : port;
	next_name = jstr(object, "description");

	if (!next_name) {
		next_name = jstr(object, "name");
	}

	if (!next_name) {
		next_name = name_in;
	}

	if (next_name) {
		copy_str(name, name_cap, next_name);
	}

	containers = json_get(object, "containers");

	if (!containers) {
		looks = jstr(object, "client_priv_key") || jstr(object, "PrivateKey");
		embedded = jstr(object, "config");

		if (!embedded) {
			embedded = jstr(object, "nativeConfig");
		}

		if (!looks && embedded && contains_ci(embedded, "[interface]")) {
			looks = 1;
		}

		if (looks) {
			return render_proto(object, servers, server_count, next_host, next_port, embedded, config, config_cap) ? IMPORT_WG : IMPORT_UNREADABLE;
		}

		if (jstr(object, "hostName") || jstr(object, "defaultContainer") || jstr(object, "description")) {
			return IMPORT_UNSUPPORTED;
		}

		return IMPORT_UNREADABLE;
	}

	if (containers->type != JSON_ARR) {
		return IMPORT_UNREADABLE;
	}

	preferred = jstr(object, "defaultContainer");

	if (preferred && !supported_kind(preferred)) {
		return IMPORT_UNSUPPORTED;
	}

	{
		rc = 0;
		saw = 0;

		for (child = containers->child; child && rc < 16; child = child->next) {
			if (child->type != JSON_OBJ) {
				continue;
			}

			kinds[rc] = jstr(child, "container") ? jstr(child, "container") : "";
			ranked[rc] = child;
			rc++;
		}

		for (i = 1; i < rc; i++) {
			j = i;

			while (j > 0 && kind_rank(kinds[j], preferred) < kind_rank(kinds[j - 1], preferred)) {
				tk = kinds[j];
				tv = ranked[j];

				kinds[j] = kinds[j - 1];
				ranked[j] = ranked[j - 1];
				kinds[j - 1] = tk;
				ranked[j - 1] = tv;
				j--;
			}
		}

		arena_init(&local, 8192);

		for (i = 0; i < rc; i++) {
			kind = kinds[i];
			proto = NULL;
			conf = NULL;

			if (!supported_kind(kind)) {
				continue;
			}

			saw = 1;
			protocol = (strcmp(kind, "amnezia-wireguard") == 0 || strcmp(kind, "wireguard") == 0) ? "wireguard" : "awg";
			box = json_get(ranked[i], protocol);

			if (!box || box->type != JSON_OBJ) {
				continue;
			}

			box_port = jstr(box, "port") ? jstr(box, "port") : next_port;
			last = json_get(box, "last_config");

			if (!last) {
				continue;
			}

			if (!unwrap_last(&local, last, &proto, &conf)) {
				arena_free(&local);
				return IMPORT_UNREADABLE;
			}

			if (proto) {
				mh = jstr(proto, "hostName") ? jstr(proto, "hostName") : next_host;
				mp = jstr(proto, "port") ? jstr(proto, "port") : box_port;
				embedded = jstr(proto, "config");

				if (!embedded) {
					embedded = jstr(proto, "nativeConfig");
				}

				if (render_proto(proto, servers, server_count, mh, mp, embedded, config, config_cap)) {
					arena_free(&local);
					return IMPORT_WG;
				}
			} else if (conf) {
				if (render_proto(box, servers, server_count, next_host, box_port, conf, config, config_cap)) {
					arena_free(&local);
					return IMPORT_WG;
				}
			}
		}

		arena_free(&local);
		return saw ? IMPORT_UNREADABLE : IMPORT_UNSUPPORTED;
	}
}

/*
===============
decode_json_text
Import one JSON document.
===============
*/
static int decode_json_text(const char *text, char *config, int config_cap, char *name, int name_cap) {
	arena_t	arena;
	json_t	*obj;
	int			rc;

	arena_init(&arena, 65536);
	obj = json_parse(&arena, text);

	if (!obj || obj->type != JSON_OBJ) {
		arena_free(&arena);
		return IMPORT_UNREADABLE;
	}

	name[0] = '\0';
	rc = from_object(obj, NULL, 0, NULL, NULL, NULL, config, config_cap, name, name_cap);
	arena_free(&arena);
	return rc;
}

/*
===============
import_normalize
Conf, JSON, or vpn:// in. A WireGuard config out, plus a name when the share has one.
===============
*/
int import_normalize(const char *text, char *config, int config_cap, char *name, int name_cap) {
	char			trimmed[65536];
	const char		*p;
	unsigned char	raw[65536];
	int				n;
	int				len;
	char			low[8];
	char			body[65536];
	unsigned char	inflated[65536];
	size_t			got;
	char			*s;

	if (!config || config_cap <= 0) {
		return IMPORT_UNREADABLE;
	}

	config[0] = '\0';

	if (name && name_cap > 0) {
		name[0] = '\0';
	}

	if (!text) {
		return IMPORT_UNREADABLE;
	}

	p = text;

	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p++;
	}

	if ((unsigned char)p[0] == 0xef) {
		p += 3;
	}

	snprintf(trimmed, sizeof(trimmed), "%s", p);
	{
		len = (int)strlen(trimmed);

		while (len > 0 && (trimmed[len - 1] == ' ' || trimmed[len - 1] == '\t' || trimmed[len - 1] == '\n' || trimmed[len - 1] == '\r')) {
			trimmed[--len] = '\0';
		}
	}

	if (strlen(trimmed) > 6) {
		memcpy(low, trimmed, 6);
		low[6] = '\0';
		str_lower(low);

		if (strcmp(low, "vpn://") == 0) {
			n = b64url_decode(trimmed + 6, raw, (int)sizeof(raw));

			if (n < 0) {
				return IMPORT_UNREADABLE;
			}

			raw[n < (int)sizeof(raw) ? n : (int)sizeof(raw) - 1] = 0;
			{
				snprintf(body, sizeof(body), "%s", (char *)raw);
				str_trim(body);

				if (body[0] == '{') {
					return decode_json_text(body, config, config_cap, name, name_cap);
				}

				if (contains_ci(body, "[interface]")) {
					snprintf(config, (size_t)config_cap, "%s", body);
					return IMPORT_WG;
				}
			}

			{
				got = fairlead_quncompress(raw, (size_t)n, inflated, sizeof(inflated) - 1);

				if (got == 0) {
					return IMPORT_UNREADABLE;
				}

				inflated[got] = 0;
				{
					snprintf(body, sizeof(body), "%s", (char *)inflated);
					{
						s = body;

						while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') {
							s++;
						}

						if (s != body) {
							memmove(body, s, strlen(s) + 1);
						}

						{
							len = (int)strlen(body);

							while (len > 0 && (body[len - 1] == ' ' || body[len - 1] == '\n' || body[len - 1] == '\r')) {
								body[--len] = '\0';
							}
						}
					}

					if (body[0] == '{') {
						return decode_json_text(body, config, config_cap, name, name_cap);
					}

					if (contains_ci(body, "[interface]")) {
						snprintf(config, (size_t)config_cap, "%s", body);
						return IMPORT_WG;
					}
				}
			}

			return IMPORT_UNREADABLE;
		}
	}

	if (trimmed[0] == '{') {
		return decode_json_text(trimmed, config, config_cap, name, name_cap);
	}

	snprintf(config, (size_t)config_cap, "%s", text);
	return IMPORT_WG;
}



/*
===============
append
Add a line to a UAPI buffer. Returns the new length, or -1.
===============
*/
static int append(char *out, int cap, int used, const char *line) {
	int		n;

	n = (int)strlen(line);

	if (used + n + 1 >= cap) {
		return -1;
	}

	memcpy(out + used, line, (size_t)n);
	used += n;
	out[used] = '\0';
	return used;
}

/*
===============
uapi_set
The set=1 message for this config. endpoints_by_section[section] overrides Endpoint.
===============
*/
int uapi_set(const doc_t *doc, const char **endpoints_by_section, char *out, int cap) {
	int		iface;
	char	private_key[256];
	char	hex[80];
	char	line[512];
	char	awg[2048];
	int		peers[16];
	int		peer_count;
	int		used;
	int		i;
	char	portbuf[64];
	int		port;
	int		ok;
	int		peer;
	int		nitems;
	int		k;
	int		value;
	int		numeric;

	used = 0;

	if (!out || cap < 16) {
		return 0;
	}

	out[0] = '\0';
	iface = doc_interface(doc);

	if (iface < 0) {
		return 0;
	}

	doc_value(doc, iface, "PrivateKey", private_key, (int)sizeof(private_key));

	if (!key_hex(private_key, hex, (int)sizeof(hex))) {
		return 0;
	}

	snprintf(line, sizeof(line), "set=1\nprivate_key=%s\n", hex);
	used = append(out, cap, used, line);

	if (used < 0) {
		return 0;
	}

	doc_value(doc, iface, "ListenPort", portbuf, (int)sizeof(portbuf));
	{
		port = 0;
		ok = 1;

		if (portbuf[0]) {
			for (i = 0; portbuf[i]; i++) {
				if (portbuf[i] < '0' || portbuf[i] > '9') {
					ok = 0;
				} else {
					port = port * 10 + (portbuf[i] - '0');
				}
			}

			if (ok && port >= 0 && port <= 65535 && portbuf[0]) {
				snprintf(line, sizeof(line), "listen_port=%d\n", port);
				used = append(out, cap, used, line);

				if (used < 0) {
					return 0;
				}
			}
		}
	}

	if (!amnezia_uapi(doc, awg, (int)sizeof(awg))) {
		return 0;
	}

	if (awg[0]) {
		used = append(out, cap, used, awg);

		if (used < 0) {
			return 0;
		}
	}

	used = append(out, cap, used, "replace_peers=true\n");

	if (used < 0) {
		return 0;
	}

	peer_count = doc_peers(doc, peers, 16);

	for (i = 0; i < peer_count; i++) {
		peer = peers[i];
		char pub[256];
		char psk[256];
		char keep[64];
		char allowed[1024];

		char items[16][256];
		doc_value(doc, peer, "PublicKey", pub, (int)sizeof(pub));

		if (!key_hex(pub, hex, (int)sizeof(hex))) {
			return 0;
		}

		snprintf(line, sizeof(line), "public_key=%s\n", hex);
		used = append(out, cap, used, line);

		if (used < 0) {
			return 0;
		}

		doc_value(doc, peer, "PresharedKey", psk, (int)sizeof(psk));

		if (psk[0]) {
			if (!key_hex(psk, hex, (int)sizeof(hex))) {
				return 0;
			}

			snprintf(line, sizeof(line), "preshared_key=%s\n", hex);
			used = append(out, cap, used, line);

			if (used < 0) {
				return 0;
			}
		}

		if (endpoints_by_section && peer < 64 && endpoints_by_section[peer] && endpoints_by_section[peer][0]) {
			snprintf(line, sizeof(line), "endpoint=%s\n", endpoints_by_section[peer]);
			used = append(out, cap, used, line);

			if (used < 0) {
				return 0;
			}
		}

		doc_value(doc, peer, "PersistentKeepalive", keep, (int)sizeof(keep));
		{
			value = 0;
			numeric = keep[0] != '\0';

			for (k = 0; keep[k]; k++) {
				if (keep[k] < '0' || keep[k] > '9') {
					numeric = 0;
				} else {
					value = value * 10 + (keep[k] - '0');
				}
			}

			if (numeric && value >= 0 && value <= 65535) {
				snprintf(line, sizeof(line), "persistent_keepalive_interval=%d\n", value);
				used = append(out, cap, used, line);
			} else if (amnezia_is(doc) && amnezia_uint_range(keep, 65535)) {
				snprintf(line, sizeof(line), "persistent_keepalive_interval=%s\n", keep);
				used = append(out, cap, used, line);
			}

			if (used < 0) {
				return 0;
			}
		}

		used = append(out, cap, used, "replace_allowed_ips=true\n");

		if (used < 0) {
			return 0;
		}

		doc_peer_allowed(doc, peer, allowed, (int)sizeof(allowed));
		nitems = split_commas(allowed, items, 16);

		for (k = 0; k < nitems; k++) {
			cidr_t cidr;
			char formatted[64];

			if (!cidr_parse(items[k], &cidr)) {
				return 0;
			}

			cidr_format(&cidr, formatted, (int)sizeof(formatted));
			snprintf(line, sizeof(line), "allowed_ip=%s\n", formatted);
			used = append(out, cap, used, line);

			if (used < 0) {
				return 0;
			}
		}
	}

	used = append(out, cap, used, "\n");
	return used > 0;
}

/*
===============
uapi_parse_get
Peer status from a get=1 reply.
===============
*/
void uapi_parse_get(const char *text, const char *iface, device_status_t *out) {
	const char			*p;
	char				device_key[128];
	char				pending[128];
	int					have_pending;
	int					in_peer;
	peer_status_t	peer;
	int					allowed_count;
	char				*eq;
	const char			*eol;
	int					n;
	char				*key;
	char				*value;
	int					a;

	have_pending = 0;
	in_peer = 0;

	char allowed[32][64];
	allowed_count = 0;

	memset(out, 0, sizeof(*out));
	snprintf(out->interface_name, sizeof(out->interface_name), "%s", iface ? iface : "");
	device_key[0] = '\0';
	pending[0] = '\0';
	memset(&peer, 0, sizeof(peer));

	if (!text) {
		return;
	}

	p = text;

	while (*p) {
		char line[512];

		eol = strchr(p, '\n');
		n = eol ? (int)(eol - p) : (int)strlen(p);

		if (n > (int)sizeof(line) - 1) {
			n = (int)sizeof(line) - 1;
		}

		memcpy(line, p, (size_t)n);
		line[n] = '\0';

		if (n > 0 && line[n - 1] == '\r') {
			line[n - 1] = '\0';
		}

		eq = strchr(line, '=');

		if (eq) {
			key = line;
			value = eq + 1;

			*eq = '\0';

			if (strcmp(key, "listen_port") == 0) {
				out->listen_port = atoi(value);
			} else if (strcmp(key, "public_key") == 0) {
				if (in_peer && out->peer_count < PEER_MAX) {
					peer.allowed[0] = '\0';

					for (a = 0; a < allowed_count; a++) {
						if (a) {
							strncat(peer.allowed, ", ", sizeof(peer.allowed) - strlen(peer.allowed) - 1);
						}

						strncat(peer.allowed, allowed[a], sizeof(peer.allowed) - strlen(peer.allowed) - 1);
					}

					out->peers[out->peer_count++] = peer;
					in_peer = 0;
					allowed_count = 0;
				}

				if (!have_pending) {
					snprintf(pending, sizeof(pending), "%s", value);
					have_pending = 1;
				} else if (device_key[0] == '\0') {
					hex_b64(pending, device_key, (int)sizeof(device_key));
					snprintf(pending, sizeof(pending), "%s", value);
					have_pending = 1;
				} else {
					snprintf(pending, sizeof(pending), "%s", value);
					have_pending = 1;
				}
			} else if (strcmp(key, "preshared_key") == 0) {
				memset(&peer, 0, sizeof(peer));
				hex_b64(have_pending ? pending : "", peer.public_key, (int)sizeof(peer.public_key));
				have_pending = 0;
				pending[0] = '\0';
				in_peer = 1;
				allowed_count = 0;
			} else if (strcmp(key, "endpoint") == 0) {
				snprintf(peer.endpoint, sizeof(peer.endpoint), "%s", value);
			} else if (strcmp(key, "last_handshake_time_sec") == 0) {
				peer.handshake = atoi(value);
			} else if (strcmp(key, "rx_bytes") == 0) {
				peer.rx = strtoull(value, NULL, 10);
			} else if (strcmp(key, "tx_bytes") == 0) {
				peer.tx = strtoull(value, NULL, 10);
			} else if (strcmp(key, "persistent_keepalive_interval") == 0) {
				peer.keepalive = atoi(value);
			} else if (strcmp(key, "allowed_ip") == 0) {
				if (value[0] && allowed_count < 32) {
					snprintf(allowed[allowed_count], 64, "%s", value);
					allowed_count++;
				}
			}
		}

		if (!eol) {
			break;
		}

		p = eol + 1;
	}

	if (in_peer && out->peer_count < PEER_MAX) {
		peer.allowed[0] = '\0';

		for (a = 0; a < allowed_count; a++) {
			if (a) {
				strncat(peer.allowed, ", ", sizeof(peer.allowed) - strlen(peer.allowed) - 1);
			}

			strncat(peer.allowed, allowed[a], sizeof(peer.allowed) - strlen(peer.allowed) - 1);
		}

		out->peers[out->peer_count++] = peer;
	}

	if (device_key[0] == '\0' && have_pending) {
		hex_b64(pending, device_key, (int)sizeof(device_key));
	}

	snprintf(out->public_key, sizeof(out->public_key), "%s", device_key);
}

/*
===============
dump_parse
Devices from tab-separated wg dump lines.
===============
*/
int dump_parse(const char *text, device_status_t *out, int cap) {
	const char	*p;
	int			count;
	int			fc;
	char		*tok;
	const char	*eol;
	int			n;
	int			dev;
	int			i;
	int			saved_count;

	count = 0;

	if (!text || !out || cap <= 0) {
		return 0;
	}

	p = text;

	while (*p) {
		char line[1024];
		char *fields[12];
		fc = 0;

		eol = strchr(p, '\n');
		n = eol ? (int)(eol - p) : (int)strlen(p);
		dev = -1;

		if (n > (int)sizeof(line) - 1) {
			n = (int)sizeof(line) - 1;
		}

		memcpy(line, p, (size_t)n);
		line[n] = '\0';

		if (n > 0 && line[n - 1] == '\r') {
			line[n - 1] = '\0';
		}

		tok = line;

		while (fc < 12) {
			fields[fc++] = tok;
			tok = strchr(tok, '\t');

			if (!tok) {
				break;
			}

			*tok++ = '\0';
		}

		if (fields[0][0] != '\0') {
			for (i = 0; i < count; i++) {
				if (strcmp(out[i].interface_name, fields[0]) == 0) {
					dev = i;
				}
			}

			if (fc >= 9) {
				peer_status_t peer;

				memset(&peer, 0, sizeof(peer));
				snprintf(peer.public_key, sizeof(peer.public_key), "%s", fields[1]);
				snprintf(peer.endpoint, sizeof(peer.endpoint), "%s", fields[3]);
				snprintf(peer.allowed, sizeof(peer.allowed), "%s", fields[4]);
				peer.handshake = atoi(fields[5]);
				peer.rx = strtoull(fields[6], NULL, 10);
				peer.tx = strtoull(fields[7], NULL, 10);
				peer.keepalive = atoi(fields[8]);

				if (dev < 0 && count < cap) {
					dev = count++;
					memset(&out[dev], 0, sizeof(out[dev]));
					snprintf(out[dev].interface_name, sizeof(out[dev].interface_name), "%s", fields[0]);
				}

				if (dev >= 0 && out[dev].peer_count < PEER_MAX) {
					out[dev].peers[out[dev].peer_count++] = peer;
				}
			} else if (fc >= 5) {
				peer_status_t saved[PEER_MAX];
				saved_count = 0;

				if (dev >= 0) {
					saved_count = out[dev].peer_count;
					memcpy(saved, out[dev].peers, sizeof(saved));
				}

				if (dev < 0 && count < cap) {
					dev = count++;
				}

				if (dev >= 0) {
					memset(&out[dev], 0, sizeof(out[dev]));
					snprintf(out[dev].interface_name, sizeof(out[dev].interface_name), "%s", fields[0]);
					snprintf(out[dev].public_key, sizeof(out[dev].public_key), "%s", fields[2]);
					out[dev].listen_port = atoi(fields[3]);
					out[dev].peer_count = saved_count;
					memcpy(out[dev].peers, saved, sizeof(saved));
				}
			}
		}

		if (!eol) {
			break;
		}

		p = eol + 1;
	}

	return count;
}

