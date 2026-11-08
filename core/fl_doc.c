//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_doc.c -- WireGuard config as sections and fields. Parse, edit, and render the text.
//
//===============================================================

#include "fl_doc.h"
#include "fl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
===============
fl_doc_init
Zero a document.
===============
*/
void fl_doc_init(fl_doc_t *doc) {
	memset(doc, 0, sizeof(*doc));
}

/*
===============
free_entry
Release one field or raw line.
===============
*/
static void free_entry(fl_entry_t *e) {
	free(e->raw);
	free(e->key);
	free(e->value);
	memset(e, 0, sizeof(*e));
}

/*
===============
fl_doc_free
Release every section and the preamble.
===============
*/
void fl_doc_free(fl_doc_t *doc) {
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
static void add_preamble(fl_doc_t *doc, const char *line) {
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

	doc->preamble[doc->preamble_count++] = fl_dup(line);
}

/*
===============
add_section
Append a named section. Returns NULL if the grow fails.
===============
*/
static fl_section_t *add_section(fl_doc_t *doc, const char *name) {
	fl_section_t	*s;
	int				cap;
	fl_section_t	*next;

	if (doc->section_count >= doc->section_cap) {
		cap = doc->section_cap ? doc->section_cap * 2 : 4;
		next = (fl_section_t *)realloc(doc->sections, (size_t)cap * sizeof(fl_section_t));

		if (!next) {
			return NULL;
		}

		doc->sections = next;
		doc->section_cap = cap;
	}

	s = &doc->sections[doc->section_count++];
	memset(s, 0, sizeof(*s));
	s->name = fl_dup(name);
	return s;
}

/*
===============
add_entry
Append a field or a raw line. Drops the entry if the grow fails.
===============
*/
static void add_entry(fl_section_t *s, fl_entry_t entry) {
	int			cap;
	fl_entry_t	*next;

	if (s->count >= s->cap) {
		cap = s->cap ? s->cap * 2 : 8;
		next = (fl_entry_t *)realloc(s->entries, (size_t)cap * sizeof(fl_entry_t));

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
fl_doc_parse
Replace doc with the sections in text.
===============
*/
void fl_doc_parse(fl_doc_t *doc, const char *text) {
	const char		*p;
	int				saw;
	char			line[4096];
	const char		*eol;
	int				n;
	fl_section_t	*section;
	char			*eq;
	int				kn;

	saw = 0;

	fl_doc_free(doc);
	fl_doc_init(doc);

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
		fl_trim(trimmed);

		if (trimmed[0] == '[' && trimmed[strlen(trimmed) - 1] == ']' && strlen(trimmed) > 2) {
			char name[256];

			saw = 1;
			snprintf(name, sizeof(name), "%s", trimmed + 1);
			name[strlen(name) - 1] = '\0';
			fl_trim(name);
			add_section(doc, name);
		} else if (!saw) {
			add_preamble(doc, line);
		} else if (doc->section_count > 0) {
			fl_entry_t entry;

			memset(&entry, 0, sizeof(entry));
			section = &doc->sections[doc->section_count - 1];

			if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
				entry.raw = fl_dup(line);
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
				fl_trim(key);
				snprintf(value, sizeof(value), "%s", eq + 1);
				fl_trim(value);
				entry.is_field = 1;
				entry.key = fl_dup(key);
				entry.value = fl_dup(value);
				add_entry(section, entry);
			} else {
				entry.raw = fl_dup(line);
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
fl_doc_render
Render the document. The caller frees the string. It ends with a newline.
===============
*/
char *fl_doc_render(const fl_doc_t *doc) {
	size_t				cap;
	size_t				used;
	char				*buf;
	int					s;
	int					e;
	int					last_empty;
	const fl_entry_t	*en;

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
fl_doc_interface
Index of the Interface section, or -1.
===============
*/
int fl_doc_interface(const fl_doc_t *doc) {
	int		i;

	for (i = 0; i < doc->section_count; i++) {
		if (fl_icmp(doc->sections[i].name, "Interface") == 0) {
			return i;
		}
	}

	return -1;
}

/*
===============
fl_doc_peers
Write Peer section indexes. Returns how many fit in out.
===============
*/
int fl_doc_peers(const fl_doc_t *doc, int *out, int cap) {
	int		i;
	int		n;

	n = 0;

	for (i = 0; i < doc->section_count; i++) {
		if (fl_icmp(doc->sections[i].name, "Peer") == 0) {
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
static void join_values(const fl_doc_t *doc, int section, const char *key, char *buf, int cap, int split) {
	int					i;
	int					first;
	const fl_entry_t	*e;
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

		if (!e->is_field || !e->key || fl_icmp(e->key, key) != 0) {
			continue;
		}

		snprintf(piece, sizeof(piece), "%s", e->value ? e->value : "");
		fl_trim(piece);

		if (piece[0] == '\0') {
			continue;
		}

		if (split) {
			char items[32][256];
			c = fl_split_commas(piece, items, 32);

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
fl_doc_value
First value of key, or the joined values when the key repeats.
===============
*/
void fl_doc_value(const fl_doc_t *doc, int section, const char *key, char *buf, int cap) {
	join_values(doc, section, key, buf, cap, 0);
}

/*
===============
fl_doc_addresses
Interface Address list.
===============
*/
void fl_doc_addresses(const fl_doc_t *doc, char *buf, int cap) {
	int		iface;

	iface = fl_doc_interface(doc);
	join_values(doc, iface, "Address", buf, cap, 1);
}

/*
===============
fl_doc_dns
Interface DNS list.
===============
*/
void fl_doc_dns(const fl_doc_t *doc, char *buf, int cap) {
	int		iface;

	iface = fl_doc_interface(doc);
	join_values(doc, iface, "DNS", buf, cap, 1);
}

/*
===============
fl_doc_peer_allowed
AllowedIPs of one peer.
===============
*/
void fl_doc_peer_allowed(const fl_doc_t *doc, int section, char *buf, int cap) {
	join_values(doc, section, "AllowedIPs", buf, cap, 1);
}

/*
===============
canonical_key
The spelling written back into the config.
===============
*/
static const char *canonical_key(const char *key) {
	if (fl_icmp(key, "privatekey") == 0) {
		return "PrivateKey";
	}

	if (fl_icmp(key, "address") == 0) {
		return "Address";
	}

	if (fl_icmp(key, "dns") == 0) {
		return "DNS";
	}

	if (fl_icmp(key, "mtu") == 0) {
		return "MTU";
	}

	if (fl_icmp(key, "listenport") == 0) {
		return "ListenPort";
	}

	if (fl_icmp(key, "publickey") == 0) {
		return "PublicKey";
	}

	if (fl_icmp(key, "presharedkey") == 0) {
		return "PresharedKey";
	}

	if (fl_icmp(key, "endpoint") == 0) {
		return "Endpoint";
	}

	if (fl_icmp(key, "allowedips") == 0) {
		return "AllowedIPs";
	}

	if (fl_icmp(key, "persistentkeepalive") == 0) {
		return "PersistentKeepalive";
	}

	if (fl_icmp(key, "table") == 0) {
		return "Table";
	}

	if (fl_icmp(key, "jc") == 0) {
		return "Jc";
	}

	if (fl_icmp(key, "jmin") == 0) {
		return "Jmin";
	}

	if (fl_icmp(key, "jmax") == 0) {
		return "Jmax";
	}

	if (fl_icmp(key, "s1") == 0) {
		return "S1";
	}

	if (fl_icmp(key, "s2") == 0) {
		return "S2";
	}

	if (fl_icmp(key, "s3") == 0) {
		return "S3";
	}

	if (fl_icmp(key, "s4") == 0) {
		return "S4";
	}

	if (fl_icmp(key, "h1") == 0) {
		return "H1";
	}

	if (fl_icmp(key, "h2") == 0) {
		return "H2";
	}

	if (fl_icmp(key, "h3") == 0) {
		return "H3";
	}

	if (fl_icmp(key, "h4") == 0) {
		return "H4";
	}

	if (fl_icmp(key, "i1") == 0) {
		return "I1";
	}

	if (fl_icmp(key, "i2") == 0) {
		return "I2";
	}

	if (fl_icmp(key, "i3") == 0) {
		return "I3";
	}

	if (fl_icmp(key, "i4") == 0) {
		return "I4";
	}

	if (fl_icmp(key, "i5") == 0) {
		return "I5";
	}

	if (fl_icmp(key, "headerprotectionkey") == 0) {
		return "HeaderProtectionKey";
	}

	if (fl_icmp(key, "contentpaddingaddition") == 0) {
		return "ContentPaddingAddition";
	}

	if (fl_icmp(key, "rekeyaftertime") == 0) {
		return "RekeyAfterTime";
	}

	if (fl_icmp(key, "rekeytimeout") == 0) {
		return "RekeyTimeout";
	}

	if (fl_icmp(key, "rejectaftertime") == 0) {
		return "RejectAfterTime";
	}

	if (fl_icmp(key, "keepalivetimeout") == 0) {
		return "KeepaliveTimeout";
	}

	if (fl_icmp(key, "maxhandshakeattempts") == 0) {
		return "MaxHandshakeAttempts";
	}

	if (fl_icmp(key, "randomtrailers") == 0) {
		return "RandomTrailers";
	}

	if (fl_icmp(key, "disablecookies") == 0) {
		return "DisableCookies";
	}

	return key;
}

/*
===============
fl_doc_set
Replace every value of key. An empty value deletes the key.
===============
*/
void fl_doc_set(fl_doc_t *doc, int section, const char *key, const char *value) {
	fl_section_t	*s;
	char			trimmed[2048];
	int				first;
	int				i;
	int				w;
	const char		*canon;
	fl_entry_t		entry;
	int				cap;
	fl_entry_t		*next;

	first = -1;

	if (section < 0 || section >= doc->section_count || !key) {
		return;
	}

	s = &doc->sections[section];
	snprintf(trimmed, sizeof(trimmed), "%s", value ? value : "");
	fl_trim(trimmed);
	canon = canonical_key(key);

	for (i = 0; i < s->count; i++) {
		if (s->entries[i].is_field && s->entries[i].key && fl_icmp(s->entries[i].key, key) == 0) {
			if (first < 0) {
				first = i;
			}
		}
	}

	if (first >= 0) {
		w = 0;

		for (i = 0; i < s->count; i++) {
			if (s->entries[i].is_field && s->entries[i].key && fl_icmp(s->entries[i].key, key) == 0) {
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
		entry.key = fl_dup(canon);
		entry.value = fl_dup(trimmed);

		if (first < 0) {
			add_entry(s, entry);
		} else {
			if (s->count >= s->cap) {
				cap = s->cap ? s->cap * 2 : 8;
				next = (fl_entry_t *)realloc(s->entries, (size_t)cap * sizeof(fl_entry_t));

				if (!next) {
					free_entry(&entry);
					return;
				}

				s->entries = next;
				s->cap = cap;
			}

			memmove(&s->entries[first + 1], &s->entries[first], (size_t)(s->count - first) * sizeof(fl_entry_t));
			s->entries[first] = entry;
			s->count++;
		}
	}
}

/*
===============
fl_doc_ensure_interface
Interface index, creating the section at the front when it is missing.
===============
*/
int fl_doc_ensure_interface(fl_doc_t *doc) {
	int				iface;
	fl_section_t	section;
	int				cap;
	fl_section_t	*next;

	iface = fl_doc_interface(doc);

	if (iface >= 0) {
		return iface;
	}

	memset(&section, 0, sizeof(section));
	section.name = fl_dup("Interface");

	if (doc->section_count >= doc->section_cap) {
		cap = doc->section_cap ? doc->section_cap * 2 : 4;
		next = (fl_section_t *)realloc(doc->sections, (size_t)cap * sizeof(fl_section_t));

		if (!next) {
			free(section.name);
			return -1;
		}

		doc->sections = next;
		doc->section_cap = cap;
	}

	memmove(&doc->sections[1], &doc->sections[0], (size_t)doc->section_count * sizeof(fl_section_t));
	doc->sections[0] = section;
	doc->section_count++;
	return 0;
}

/*
===============
fl_doc_add_peer
Append a Peer section and return its index.
===============
*/
int fl_doc_add_peer(fl_doc_t *doc) {
	add_section(doc, "Peer");
	return doc->section_count - 1;
}

/*
===============
fl_doc_blank
A new tunnel: one interface, one empty peer.
===============
*/
void fl_doc_blank(fl_doc_t *doc, const char *private_key) {
	int		iface;
	int		peer;

	fl_doc_free(doc);
	fl_doc_init(doc);
	iface = fl_doc_ensure_interface(doc);
	fl_doc_set(doc, iface, "PrivateKey", private_key);
	fl_doc_set(doc, iface, "Address", "10.8.0.2/32");
	fl_doc_set(doc, iface, "DNS", "1.1.1.1");
	peer = fl_doc_add_peer(doc);
	fl_doc_set(doc, peer, "PublicKey", "");
	fl_doc_set(doc, peer, "AllowedIPs", "0.0.0.0/0, ::/0");
	fl_doc_set(doc, peer, "Endpoint", "");
	fl_doc_set(doc, peer, "PersistentKeepalive", "25");
}

/*
===============
fl_split_commas
Split a comma list into trimmed tokens. Returns how many were written.
===============
*/
int fl_split_commas(const char *text, char out[][256], int cap) {
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
		fl_trim(out[count]);

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
