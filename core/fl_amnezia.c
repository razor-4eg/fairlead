//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_amnezia.c -- AmneziaWG fields, config checks, and import of a conf, a JSON share, or a vpn:// key.
//
//===============================================================

#include "fl_amnezia.h"
#include "fl.h"
#include "fl_ip.h"
#include "fl_json.h"
#include "fl_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t fairlead_quncompress(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_cap);

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
fl_amnezia_uint_range
A number, or a-b, inside 0..maxv.
===============
*/
int fl_amnezia_uint_range(const char *text, unsigned long long maxv) {
	char				buf[128];
	char				*dash;
	unsigned long long	a;
	unsigned long long	b;
	char				*end;

	if (!text) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", text);
	fl_trim(buf);
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
	fl_trim(buf);
	fl_lower(buf);

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
fl_amnezia_present
Amnezia keys that have a value, in field order.
===============
*/
int fl_amnezia_present(const fl_doc_t *doc, const char **out, int cap) {
	int		iface;
	int		n;
	int		i;
	char	value[512];

	iface = fl_doc_interface(doc);
	n = 0;

	if (iface < 0) {
		return 0;
	}

	for (i = 0; i < s_ordered_count; i++) {
		fl_doc_value(doc, iface, s_ordered_keys[i], value, (int)sizeof(value));

		if (value[0] && n < cap) {
			out[n++] = s_ordered_keys[i];
		}
	}

	return n;
}

/*
===============
fl_amnezia_is
True when any Amnezia key is set.
===============
*/
int fl_amnezia_is(const fl_doc_t *doc) {
	const char	*keys[32];

	return fl_amnezia_present(doc, keys, 32) > 0;
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
	fl_trim(buf);

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
		if (!fl_amnezia_uint_range(buf, 4294967295ull)) {
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
		return fl_key_hex(buf, out, cap);
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
fl_amnezia_uapi
Amnezia lines for a set message. Empty, and success, when the config is plain WireGuard.
===============
*/
int fl_amnezia_uapi(const fl_doc_t *doc, char *out, int cap) {
	int			iface;
	int			i;
	int			used;
	const char	*name;

	used = 0;

	if (!out || cap <= 0) {
		return 0;
	}

	out[0] = '\0';

	if (!fl_amnezia_is(doc)) {
		return 1;
	}

	iface = fl_doc_interface(doc);

	for (i = 0; i < s_ordered_count; i++) {
		char raw[1024];
		char value[1024];
		char line[1200];

		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

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
fl_amnezia_issues
What is wrong with the Amnezia fields.
===============
*/
int fl_amnezia_issues(const fl_doc_t *doc, char out[][192], int cap) {
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

	if (!fl_amnezia_is(doc)) {
		return 0;
	}

	iface = fl_doc_interface(doc);

	for (i = 0; i < 3; i++) {
		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

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
		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		if (!all_digits(raw) || strtoull(raw, NULL, 10) > 65535ull) {
			add_issue(out, &n, cap, "S1, S2, S3, and S4 must be numbers from 0 to 65535.");
		}
	}

	for (i = 0; i < 4; i++) {
		fl_doc_value(doc, iface, s_ordered_keys[7 + i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			headers[i][0] = headers[i][1] = defaults[i];
			continue;
		}

		if (!fl_amnezia_uint_range(raw, 4294967295ull)) {
			headers_ok = 0;
			add_issue(out, &n, cap, "H1, H2, H3, and H4 must be a number or a range such as 123-456.");
		} else {
			char buf[128];

			snprintf(buf, sizeof(buf), "%s", raw);
			fl_trim(buf);
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
		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (!raw[0]) {
			continue;
		}

		if (strchr(raw, '\n') || strchr(raw, '\r') || strlen(raw) > 4096) {
			add_issue(out, &n, cap, "Signature packets must stay on one line.");
		}
	}

	fl_doc_value(doc, iface, "HeaderProtectionKey", raw, (int)sizeof(raw));

	if (raw[0]) {
		pads_n = 0;
		bad = 0;

		if (!fl_key_hex(raw, hex, (int)sizeof(hex))) {
			add_issue(out, &n, cap, "HeaderProtectionKey must be a WireGuard key.");
		}

		for (i = 3; i <= 6; i++) {
			char s[64];

			fl_doc_value(doc, iface, s_ordered_keys[i], s, (int)sizeof(s));

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
		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

		if (raw[0] && !fl_amnezia_uint_range(raw, 4294967295ull)) {
			add_issue(out, &n, cap, "An AmneziaWG value must be a number or a range such as 20-30.");
		}
	}

	for (i = 23; i <= 24; i++) {
		fl_doc_value(doc, iface, s_ordered_keys[i], raw, (int)sizeof(raw));

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
fl_config_issues
What would stop this config from coming up.
===============
*/
int fl_config_issues(const fl_doc_t *doc, char out[][192], int cap) {
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

	iface = fl_doc_interface(doc);
	char items[16][256];

	if (iface < 0) {
		add_issue(out, &n, cap, "The config has no Interface section.");
		return n;
	}

	fl_doc_value(doc, iface, "PrivateKey", value, (int)sizeof(value));

	if (!fl_key_is(value)) {
		add_issue(out, &n, cap, "The private key must be a WireGuard key.");
	}

	fl_doc_addresses(doc, value, (int)sizeof(value));
	count = fl_split_commas(value, items, 16);

	if (count == 0) {
		add_issue(out, &n, cap, "Set an interface address, for example 10.8.0.2/32.");
	} else {
		for (i = 0; i < count; i++) {
			fl_cidr_t cidr;

			if (!fl_cidr_parse(items[i], &cidr)) {
				add_issue(out, &n, cap, "Set an interface address, for example 10.8.0.2/32.");
				break;
			}
		}
	}

	fl_doc_value(doc, iface, "MTU", value, (int)sizeof(value));

	if (value[0]) {
		if (!parse_int_field(value, &mtu)) {
			add_issue(out, &n, cap, "MTU must be a number.");
		} else if (mtu < 1280 || mtu > 1500) {
			add_issue(out, &n, cap, "MTU is usually between 1280 and 1500.");
		}
	}

	peer_count = fl_doc_peers(doc, peers, 16);

	if (peer_count == 0) {
		add_issue(out, &n, cap, "Add at least one Peer.");
	}

	for (i = 0; i < peer_count; i++) {
		peer = peers[i];
		char keep[64];

		fl_doc_value(doc, peer, "PublicKey", value, (int)sizeof(value));

		if (!fl_key_is(value)) {
			add_issue(out, &n, cap, "A peer has an invalid public key.");
		}

		fl_doc_value(doc, peer, "PresharedKey", value, (int)sizeof(value));

		if (value[0] && !fl_key_is(value)) {
			add_issue(out, &n, cap, "PresharedKey is set, but it is not a WireGuard key.");
		}

		fl_doc_value(doc, peer, "Endpoint", value, (int)sizeof(value));

		if (!fl_endpoint_valid(value)) {
			add_issue(out, &n, cap, "Endpoint must look like host:port.");
		}

		fl_doc_peer_allowed(doc, peer, value, (int)sizeof(value));
		count = fl_split_commas(value, items, 16);

		if (count == 0) {
			add_issue(out, &n, cap, "AllowedIPs must list networks, for example 0.0.0.0/0.");
		} else {
			for (k = 0; k < count; k++) {
				fl_cidr_t cidr;

				if (!fl_cidr_parse(items[k], &cidr)) {
					add_issue(out, &n, cap, "AllowedIPs must list networks, for example 0.0.0.0/0.");
					break;
				}
			}
		}

		fl_doc_value(doc, peer, "PersistentKeepalive", keep, (int)sizeof(keep));

		if (keep[0]) {
			if (fl_amnezia_is(doc)) {
				if (!fl_amnezia_uint_range(keep, 65535)) {
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
		en = fl_amnezia_issues(doc, extra, 16);

		for (i = 0; i < en; i++) {
			add_issue(out, &n, cap, extra[i]);
		}
	}

	return n;
}

/*
===============
fl_tunnel_destinations
Routes the tunnel should own. A default route becomes the two /1 halves.
===============
*/
int fl_tunnel_destinations(const fl_doc_t *doc, char families[][8], char dests[][64], int cap) {
	int		peers[16];
	int		peer_count;
	int		n;
	int		i;
	int		k;
	int		count;

	peer_count = fl_doc_peers(doc, peers, 16);
	n = 0;

	for (i = 0; i < peer_count; i++) {
		char allowed[1024];
		char items[16][256];
		fl_doc_peer_allowed(doc, peers[i], allowed, (int)sizeof(allowed));
		count = fl_split_commas(allowed, items, 16);

		for (k = 0; k < count; k++) {
			fl_cidr_t cidr;

			if (!fl_cidr_parse(items[k], &cidr)) {
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
				fl_cidr_format(&cidr, dests[n], 64);
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
	return fl_b64_decode(tmp, out, cap);
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
static const char *jstr(const fl_json_t *obj, const char *key) {
	return fl_json_string(fl_json_get(obj, key));
}

/*
===============
fill_key
Set key when the section does not already have it.
===============
*/
static void fill_key(fl_doc_t *doc, int section, const char *key, const char *value) {
	char	cur[1024];

	if (!value || !value[0]) {
		return;
	}

	fl_doc_value(doc, section, key, cur, (int)sizeof(cur));

	if (cur[0]) {
		return;
	}

	fl_doc_set(doc, section, key, value);
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
	n = fl_split_commas(text, items, 8);
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
static int render_proto(const fl_json_t *proto, const char **dns, int dns_count, const char *host, const char *port, const char *embedded, char *config, int cap) {
	fl_doc_t		doc;
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
	const fl_json_t	*allowed;
	const char		*parts[8];
	int				c;
	const char		*eh;
	const char		*ep;
	char			endpoint[256];
	char			cur[256];

	fl_doc_init(&doc);

	if (embedded && contains_ci(embedded, "[interface]")) {
		fl_doc_parse(&doc, embedded);
	}

	iface = fl_doc_ensure_interface(&doc);
	priv = jstr(proto, "client_priv_key");

	if (!priv) {
		priv = jstr(proto, "PrivateKey");
	}

	fill_key(&doc, iface, "PrivateKey", priv);
	fl_doc_value(&doc, iface, "Address", addr, (int)sizeof(addr));

	if (!addr[0]) {
		ip = jstr(proto, "client_ip");

		if (!ip) {
			ip = jstr(proto, "Address");
		}

		if (ip) {
			normalize_addresses(ip, addr, (int)sizeof(addr));
			fl_doc_set(&doc, iface, "Address", addr);
		}
	}

	fl_doc_dns(&doc, dnsjoin, (int)sizeof(dnsjoin));

	if (!dnsjoin[0] && dns_count > 0) {
		dnsjoin[0] = '\0';

		for (i = 0; i < dns_count; i++) {
			if (i) {
				strncat(dnsjoin, ", ", sizeof(dnsjoin) - strlen(dnsjoin) - 1);
			}

			strncat(dnsjoin, dns[i], sizeof(dnsjoin) - strlen(dnsjoin) - 1);
		}

		fl_doc_set(&doc, iface, "DNS", dnsjoin);
	}

	fill_key(&doc, iface, "MTU", jstr(proto, "mtu") ? jstr(proto, "mtu") : jstr(proto, "MTU"));

	for (i = 0; i < s_ordered_count; i++) {
		fill_key(&doc, iface, s_ordered_keys[i], jstr(proto, s_ordered_keys[i]));
	}

	if (fl_doc_peers(&doc, peers, 4) == 0) {
		fl_doc_add_peer(&doc);
	}

	peer = fl_doc_peers(&doc, peers, 4) > 0 ? peers[0] : -1;

	if (peer < 0) {
		fl_doc_free(&doc);
		return 0;
	}

	pub = jstr(proto, "server_pub_key");

	if (!pub) {
		pub = jstr(proto, "PublicKey");
	}

	fill_key(&doc, peer, "PublicKey", pub);
	fill_key(&doc, peer, "PresharedKey", jstr(proto, "psk_key") ? jstr(proto, "psk_key") : jstr(proto, "PresharedKey"));
	fl_doc_peer_allowed(&doc, peer, allowed_join, (int)sizeof(allowed_join));

	if (!allowed_join[0]) {
		allowed = fl_json_get(proto, "allowed_ips");
		c = fl_json_strings(allowed, parts, 8);

		allowed_join[0] = '\0';

		if (allowed && allowed->type == FL_JSON_ARR) {
			for (i = 0; i < c; i++) {
				if (i) {
					strncat(allowed_join, ", ", sizeof(allowed_join) - strlen(allowed_join) - 1);
				}

				strncat(allowed_join, parts[i], sizeof(allowed_join) - strlen(allowed_join) - 1);
			}
		} else if (allowed && allowed->type == FL_JSON_STR && fl_json_string(allowed)) {
			snprintf(allowed_join, sizeof(allowed_join), "%s", fl_json_string(allowed));
		}

		if (allowed_join[0]) {
			fl_doc_set(&doc, peer, "AllowedIPs", allowed_join);
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

		fl_doc_value(&doc, peer, "Endpoint", cur, (int)sizeof(cur));

		if (!cur[0] && eh && ep) {
			make_endpoint(eh, ep, endpoint, (int)sizeof(endpoint));
			fl_doc_set(&doc, peer, "Endpoint", endpoint);
		}
	}

	fl_doc_value(&doc, iface, "PrivateKey", addr, (int)sizeof(addr));
	fl_doc_value(&doc, peer, "PublicKey", allowed_join, (int)sizeof(allowed_join));

	if (!addr[0] || !allowed_join[0]) {
		fl_doc_free(&doc);
		return 0;
	}

	rendered = fl_doc_render(&doc);
	fl_doc_free(&doc);

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
static int unwrap_last(fl_arena_t *arena, const fl_json_t *last, const fl_json_t **obj, const char **conf) {
	const char	*text;
	fl_json_t	*parsed;

	*obj = NULL;
	*conf = NULL;

	if (!last) {
		return 0;
	}

	if (last->type == FL_JSON_OBJ) {
		*obj = last;
		return 1;
	}

	text = fl_json_string(last);

	if (!text) {
		return 0;
	}

	if (text[0] == '{') {
		parsed = fl_json_parse(arena, text);

		if (!parsed || parsed->type != FL_JSON_OBJ) {
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
static int from_object(const fl_json_t *object, const char **dns_in, int dns_in_count, const char *host, const char *port, const char *name_in, char *config, int config_cap, char *name, int name_cap) {
	const char		*servers[8];
	int				server_count;
	const char		*next_host;
	const char		*next_port;
	const char		*next_name;
	const fl_json_t	*containers;
	const char		*preferred;
	int				i;
	fl_arena_t		local;
	const char		*extra[8];
	const fl_json_t	*d1;
	const fl_json_t	*d2;
	int				c;
	int				looks;
	const char		*embedded;
	const fl_json_t	*ranked[16];
	const char		*kinds[16];
	int				rc;
	const fl_json_t	*child;
	int				saw;
	int				j;
	const char		*tk;
	const fl_json_t	*tv;
	const char		*kind;
	const char		*protocol;
	const fl_json_t	*box;
	const fl_json_t	*last;
	const fl_json_t	*proto;
	const char		*conf;
	const char		*box_port;
	const char		*mh;
	const char		*mp;

	server_count = 0;

	for (i = 0; i < dns_in_count && server_count < 8; i++) {
		servers[server_count++] = dns_in[i];
	}

	{
		d1 = fl_json_get(object, "dns1");
		d2 = fl_json_get(object, "dns2");
		c = fl_json_strings(d1, extra, 8);

		if (c > 0) {
			server_count = 0;

			for (i = 0; i < c && server_count < 8; i++) {
				servers[server_count++] = extra[i];
			}

			c = fl_json_strings(d2, extra, 8);

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

	containers = fl_json_get(object, "containers");

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
			return render_proto(object, servers, server_count, next_host, next_port, embedded, config, config_cap) ? FL_IMPORT_WG : FL_IMPORT_UNREADABLE;
		}

		if (jstr(object, "hostName") || jstr(object, "defaultContainer") || jstr(object, "description")) {
			return FL_IMPORT_UNSUPPORTED;
		}

		return FL_IMPORT_UNREADABLE;
	}

	if (containers->type != FL_JSON_ARR) {
		return FL_IMPORT_UNREADABLE;
	}

	preferred = jstr(object, "defaultContainer");

	if (preferred && !supported_kind(preferred)) {
		return FL_IMPORT_UNSUPPORTED;
	}

	{
		rc = 0;
		saw = 0;

		for (child = containers->child; child && rc < 16; child = child->next) {
			if (child->type != FL_JSON_OBJ) {
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

		fl_arena_init(&local, 8192);

		for (i = 0; i < rc; i++) {
			kind = kinds[i];
			proto = NULL;
			conf = NULL;

			if (!supported_kind(kind)) {
				continue;
			}

			saw = 1;
			protocol = (strcmp(kind, "amnezia-wireguard") == 0 || strcmp(kind, "wireguard") == 0) ? "wireguard" : "awg";
			box = fl_json_get(ranked[i], protocol);

			if (!box || box->type != FL_JSON_OBJ) {
				continue;
			}

			box_port = jstr(box, "port") ? jstr(box, "port") : next_port;
			last = fl_json_get(box, "last_config");

			if (!last) {
				continue;
			}

			if (!unwrap_last(&local, last, &proto, &conf)) {
				fl_arena_free(&local);
				return FL_IMPORT_UNREADABLE;
			}

			if (proto) {
				mh = jstr(proto, "hostName") ? jstr(proto, "hostName") : next_host;
				mp = jstr(proto, "port") ? jstr(proto, "port") : box_port;
				embedded = jstr(proto, "config");

				if (!embedded) {
					embedded = jstr(proto, "nativeConfig");
				}

				if (render_proto(proto, servers, server_count, mh, mp, embedded, config, config_cap)) {
					fl_arena_free(&local);
					return FL_IMPORT_WG;
				}
			} else if (conf) {
				if (render_proto(box, servers, server_count, next_host, box_port, conf, config, config_cap)) {
					fl_arena_free(&local);
					return FL_IMPORT_WG;
				}
			}
		}

		fl_arena_free(&local);
		return saw ? FL_IMPORT_UNREADABLE : FL_IMPORT_UNSUPPORTED;
	}
}

/*
===============
decode_json_text
Import one JSON document.
===============
*/
static int decode_json_text(const char *text, char *config, int config_cap, char *name, int name_cap) {
	fl_arena_t	arena;
	fl_json_t	*obj;
	int			rc;

	fl_arena_init(&arena, 65536);
	obj = fl_json_parse(&arena, text);

	if (!obj || obj->type != FL_JSON_OBJ) {
		fl_arena_free(&arena);
		return FL_IMPORT_UNREADABLE;
	}

	name[0] = '\0';
	rc = from_object(obj, NULL, 0, NULL, NULL, NULL, config, config_cap, name, name_cap);
	fl_arena_free(&arena);
	return rc;
}

/*
===============
fl_import_normalize
Conf, JSON, or vpn:// in. A WireGuard config out, plus a name when the share has one.
===============
*/
int fl_import_normalize(const char *text, char *config, int config_cap, char *name, int name_cap) {
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
		return FL_IMPORT_UNREADABLE;
	}

	config[0] = '\0';

	if (name && name_cap > 0) {
		name[0] = '\0';
	}

	if (!text) {
		return FL_IMPORT_UNREADABLE;
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
		fl_lower(low);

		if (strcmp(low, "vpn://") == 0) {
			n = b64url_decode(trimmed + 6, raw, (int)sizeof(raw));

			if (n < 0) {
				return FL_IMPORT_UNREADABLE;
			}

			raw[n < (int)sizeof(raw) ? n : (int)sizeof(raw) - 1] = 0;
			{
				snprintf(body, sizeof(body), "%s", (char *)raw);
				fl_trim(body);

				if (body[0] == '{') {
					return decode_json_text(body, config, config_cap, name, name_cap);
				}

				if (contains_ci(body, "[interface]")) {
					snprintf(config, (size_t)config_cap, "%s", body);
					return FL_IMPORT_WG;
				}
			}

			{
				got = fairlead_quncompress(raw, (size_t)n, inflated, sizeof(inflated) - 1);

				if (got == 0) {
					return FL_IMPORT_UNREADABLE;
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
						return FL_IMPORT_WG;
					}
				}
			}

			return FL_IMPORT_UNREADABLE;
		}
	}

	if (trimmed[0] == '{') {
		return decode_json_text(trimmed, config, config_cap, name, name_cap);
	}

	snprintf(config, (size_t)config_cap, "%s", text);
	return FL_IMPORT_WG;
}
