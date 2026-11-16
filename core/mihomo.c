//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// mihomo.c -- WireGuard/AmneziaWG document + Fairlead rules → mihomo YAML.
//
//================================================================

#include "mihomo.h"
#include "tunnel.h"
#include "base.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ybuf_s {
	char	*data;
	int		len;
	int		cap;
} ybuf_t;

/*
===============
y_add
===============
*/
static void y_add(ybuf_t *b, const char *text) {
	int		n;
	int		cap;

	if (!text) {
		return;
	}

	n = (int)strlen(text);

	if (b->len + n + 1 > b->cap) {
		cap = b->cap ? b->cap * 2 : 512;

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
y_fmt
===============
*/
static void y_fmt(ybuf_t *b, const char *fmt, ...) {
	char	tmp[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	y_add(b, tmp);
}

/*
===============
y_quote
YAML double-quoted scalar.
===============
*/
static void y_quote(ybuf_t *b, const char *text) {
	const unsigned char	*p;

	y_add(b, "\"");

	for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
		if (*p == '\\' || *p == '"') {
			y_fmt(b, "\\%c", *p);
		} else if (*p == '\n') {
			y_add(b, "\\n");
		} else if (*p == '\r') {
			y_add(b, "\\r");
		} else if (*p == '\t') {
			y_add(b, "\\t");
		} else if (*p < 32) {
			y_fmt(b, "\\u%04x", *p);
		} else {
			y_fmt(b, "%c", *p);
		}
	}

	y_add(b, "\"");
}

/*
===============
y_rule
One rules: item as a quoted scalar, so paths with backslashes or spaces survive and
mihomo sees the raw "KIND,value,target" text.
===============
*/
static void y_rule(ybuf_t *b, const char *kind, const char *value, const char *tail) {
	const unsigned char	*p;

	y_add(b, "  - \"");
	y_add(b, kind);
	y_add(b, ",");

	for (p = (const unsigned char *)(value ? value : ""); *p; p++) {
		if (*p == '\\' || *p == '"') {
			y_fmt(b, "\\%c", *p);
		} else if (*p < 32) {
			y_fmt(b, "\\u%04x", *p);
		} else {
			y_fmt(b, "%c", *p);
		}
	}

	y_add(b, ",");
	y_add(b, tail);
	y_add(b, "\"\n");
}

/*
===============
y_process
A process rule. PROCESS-PATH is an exact executable path, so a macOS bundle path (/Applications/X.app)
becomes a prefix regex that also covers its helper processes. Other paths stay exact.
===============
*/
static void y_process(ybuf_t *b, const char *path, const char *tail) {
	char		re[1100];
	size_t		len;
	size_t		n;
	size_t		i;
	const char	*base;

	len = strlen(path);

	while (len > 1 && path[len - 1] == '/') {
		len--;
	}

	if (path[0] != '/' || len < 6 || len > 500 || strncmp(path + len - 4, ".app", 4) != 0) {
		y_rule(b, "PROCESS-PATH", path, tail);
		/* Windows: also match by exe name so updates/moves still hit the rule. */
		base = strrchr(path, '\\');

		if (base && base[1] && strchr(base + 1, '.') && !strchr(base + 1, '\\')) {
			y_rule(b, "PROCESS-NAME", base + 1, tail);
		}

		return;
	}

	n = 0;
	re[n++] = '^';

	for (i = 0; i < len; i++) {
		if (strchr("\\.+*?()|[]{}^$", path[i])) {
			re[n++] = '\\';
		}

		re[n++] = path[i];
	}

	re[n++] = '/';
	re[n] = '\0';
	y_rule(b, "PROCESS-PATH-REGEX", re, tail);
}

/*
===============
fail
===============
*/
static char *fail(char *err, int err_cap, const char *msg) {
	if (err && err_cap > 0) {
		snprintf(err, err_cap, "%s", msg ? msg : "Could not build the tunnel config.");
	}

	return NULL;
}

/*
===============
strip_host
Strip /prefix from Address tokens.
===============
*/
static void strip_host(char *ip) {
	char	*slash;

	slash = strchr(ip, '/');

	if (slash) {
		*slash = 0;
	}
}

/*
===============
parse_endpoint
host:port or [v6]:port → host + port. Returns 0 on failure.
===============
*/
static int parse_endpoint(const char *endpoint, char *host, int host_cap, int *port) {
	const char	*colon;
	int			n;

	if (!endpoint || !endpoint[0] || !host || host_cap < 2 || !port) {
		return 0;
	}

	if (endpoint[0] == '[') {
		const char	*end;

		end = strchr(endpoint, ']');

		if (!end || end[1] != ':') {
			return 0;
		}

		n = (int)(end - (endpoint + 1));

		if (n <= 0 || n >= host_cap) {
			return 0;
		}

		memcpy(host, endpoint + 1, (size_t)n);
		host[n] = 0;
		*port = atoi(end + 2);
		return *port > 0 && *port <= 65535;
	}

	colon = strrchr(endpoint, ':');

	if (!colon || colon == endpoint) {
		return 0;
	}

	n = (int)(colon - endpoint);

	if (n <= 0 || n >= host_cap) {
		return 0;
	}

	memcpy(host, endpoint, (size_t)n);
	host[n] = 0;
	*port = atoi(colon + 1);
	return *port > 0 && *port <= 65535;
}

/*
===============
awg_version
3 when v3 fields are set, else 2 when any Amnezia field is set, else 0.
===============
*/
static int awg_version(const doc_t *doc) {
	int		iface;
	char	raw[4096];
	int		i;
	static const char *v3_keys[] = {
		"HeaderProtectionKey", "ContentPaddingAddition", "RekeyAfterTime", "RekeyTimeout",
		"RejectAfterTime", "KeepaliveTimeout", "MaxHandshakeAttempts", "RandomTrailers", "DisableCookies"
	};

	iface = doc_interface(doc);

	if (iface < 0 || !amnezia_is(doc)) {
		return 0;
	}

	for (i = 0; i < (int)(sizeof(v3_keys) / sizeof(v3_keys[0])); i++) {
		doc_value(doc, iface, v3_keys[i], raw, (int)sizeof(raw));

		if (raw[0]) {
			return 3;
		}
	}

	return 2;
}

/*
===============
yaml_key
Map Fairlead Amnezia key → mihomo amnezia-wg-option key into out.
===============
*/
static int yaml_key(const char *key, char *out, int cap) {
	const char	*mapped;

	if (!key || !out || cap < 4) {
		return 0;
	}

	mapped = NULL;

	if (strcmp(key, "HeaderProtectionKey") == 0) {
		mapped = "header-protection-key";
	} else if (strcmp(key, "ContentPaddingAddition") == 0) {
		mapped = "content-padding-addition";
	} else if (strcmp(key, "RekeyAfterTime") == 0) {
		mapped = "rekey-after-time";
	} else if (strcmp(key, "RekeyTimeout") == 0) {
		mapped = "rekey-timeout";
	} else if (strcmp(key, "RejectAfterTime") == 0) {
		mapped = "reject-after-time";
	} else if (strcmp(key, "KeepaliveTimeout") == 0) {
		mapped = "keepalive-timeout";
	} else if (strcmp(key, "MaxHandshakeAttempts") == 0) {
		mapped = "max-handshake-attempts";
	} else if (strcmp(key, "RandomTrailers") == 0) {
		mapped = "random-trailers";
	} else if (strcmp(key, "DisableCookies") == 0) {
		mapped = "disable-cookies";
	} else if (strcmp(key, "Jmin") == 0) {
		mapped = "jmin";
	} else if (strcmp(key, "Jmax") == 0) {
		mapped = "jmax";
	} else if (key[0] && key[1] && !key[2]) {
		out[0] = (char)(key[0] >= 'A' && key[0] <= 'Z' ? key[0] - 'A' + 'a' : key[0]);
		out[1] = (char)(key[1] >= 'A' && key[1] <= 'Z' ? key[1] - 'A' + 'a' : key[1]);
		out[2] = 0;
		return 1;
	} else if (key[0] == 'I' && key[1] && !key[2]) {
		out[0] = 'i';
		out[1] = key[1];
		out[2] = 0;
		return 1;
	} else {
		return 0;
	}

	snprintf(out, cap, "%s", mapped);
	return 1;
}

/*
===============
is_bool_key
===============
*/
static int is_bool_key(const char *key) {
	return strcmp(key, "RandomTrailers") == 0 || strcmp(key, "DisableCookies") == 0;
}

/*
===============
is_full_tunnel
AllowedIPs contains 0.0.0.0/0 or ::/0.
===============
*/
static int is_full_tunnel(const doc_t *doc, int peer) {
	char	allowed[1024];
	char	items[16][256];
	int		n;
	int		i;

	doc_peer_allowed(doc, peer, allowed, (int)sizeof(allowed));
	n = split_commas(allowed, items, 16);

	for (i = 0; i < n; i++) {
		if (strcmp(items[i], "0.0.0.0/0") == 0 || strcmp(items[i], "::/0") == 0) {
			return 1;
		}
	}

	return n == 0;
}

/*
===============
emit_rule_lines
policy_selected: Through → list uses PROXY, else list uses DIRECT.
match_proxy: MATCH goes to PROXY when 1.
===============
*/
/*
===============
emit_quic_reject
Chrome/YouTube HTTP/3 (QUIC, UDP/443) keeps a session across path changes. When a
process flips between PROXY and DIRECT the QUIC flow is poisoned and the tab shows
"no internet" until Chrome restarts. Force TCP HTTPS for remaining traffic.
DIRECT process rules are emitted before this so excluded apps can still use QUIC.
Only UDP/443 is blocked — game/voice UDP on other ports is untouched.
===============
*/
static void emit_quic_reject(ybuf_t *b) {
	y_add(b, "  - AND,((NETWORK,UDP),(DST-PORT,443)),REJECT\n");
}

/*
===============
emit_app_rules
===============
*/
static void emit_app_rules(ybuf_t *b, const mihomo_rules_t *rules, const char *target) {
	int	i;

	if (!rules) {
		return;
	}

	for (i = 0; i < rules->app_count; i++) {
		if (!rules->apps[i] || !rules->apps[i][0]) {
			continue;
		}

		y_process(b, rules->apps[i], target);
	}
}

/*
===============
emit_host_rules
Domains and CIDRs for the list hit.
===============
*/
static void emit_host_rules(ybuf_t *b, const mihomo_rules_t *rules, const char *hit) {
	int	i;

	if (!rules) {
		return;
	}

	for (i = 0; i < rules->domain_count; i++) {
		if (!rules->domains[i] || !rules->domains[i][0]) {
			continue;
		}

		y_rule(b, "DOMAIN-SUFFIX", rules->domains[i], hit);
	}

	for (i = 0; i < rules->cidr_count; i++) {
		if (!rules->cidrs[i] || !rules->cidrs[i][0]) {
			continue;
		}

		y_add(b, "  - IP-CIDR,");
		y_add(b, rules->cidrs[i]);
		y_fmt(b, ",%s,no-resolve\n", hit);
	}
}

static void emit_rule_lines(ybuf_t *b, const mihomo_rules_t *rules, int policy_selected, int exclude_lan,
	int match_proxy, int block_quic) {
	const char	*hit;
	const char	*miss;

	hit = policy_selected ? "PROXY" : "DIRECT";
	miss = match_proxy ? "PROXY" : "DIRECT";

	y_add(b, "rules:\n");

	if (exclude_lan) {
		y_add(b, "  - GEOIP,LAN,DIRECT\n");
	}

	/*
	 * Exceptions mode: listed apps go DIRECT before the QUIC reject so Chrome
	 * outside the tunnel can keep HTTP/3. Through mode lists apps after reject.
	 */
	if (!policy_selected) {
		emit_app_rules(b, rules, hit);
	}

	if (block_quic) {
		emit_quic_reject(b);
	}

	if (policy_selected) {
		emit_app_rules(b, rules, hit);
	}

	emit_host_rules(b, rules, hit);
	y_fmt(b, "  - MATCH,%s\n", miss);
}

/*
===============
mihomo_config
===============
*/
char *mihomo_config(const char *wg_text, int policy_selected, int exclude_lan, int block_quic,
	const mihomo_rules_t *rules, const char *controller, const char *secret,
	char *err, int err_cap) {
	doc_t		doc;
	ybuf_t		b;
	int			iface;
	int			peers[8];
	int			peer_count;
	int			peer;
	char		private_key[128];
	char		address[512];
	char		dns[512];
	char		mtu[64];
	char		items[16][256];
	char		host[256];
	char		pub[128];
	char		psk[128];
	char		keep[64];
	char		endpoint[256];
	char		ip4[64];
	char		ip6[64];
	char		dns_items[8][256];
	int			addr_n;
	int			dns_n;
	int			port;
	int			i;
	int			ver;
	int			full;
	const char	*keys[32];
	int			key_n;
	char		raw[4096];

	memset(&b, 0, sizeof(b));
	ip4[0] = 0;
	ip6[0] = 0;

	if (err && err_cap > 0) {
		err[0] = 0;
	}

	if (!wg_text || !wg_text[0]) {
		return fail(err, err_cap, "The config is empty.");
	}

	if (!controller || !controller[0]) {
		return fail(err, err_cap, "The controller address is missing.");
	}

	doc_init(&doc);
	doc_parse(&doc, wg_text);
	iface = doc_interface(&doc);

	if (iface < 0) {
		doc_free(&doc);
		return fail(err, err_cap, "The config has no Interface section.");
	}

	peer_count = doc_peers(&doc, peers, 8);

	if (peer_count < 1) {
		doc_free(&doc);
		return fail(err, err_cap, "The config has no peer.");
	}

	peer = peers[0];
	doc_value(&doc, iface, "PrivateKey", private_key, (int)sizeof(private_key));
	doc_addresses(&doc, address, (int)sizeof(address));
	doc_dns(&doc, dns, (int)sizeof(dns));
	doc_value(&doc, iface, "MTU", mtu, (int)sizeof(mtu));
	doc_value(&doc, peer, "PublicKey", pub, (int)sizeof(pub));
	doc_value(&doc, peer, "PresharedKey", psk, (int)sizeof(psk));
	doc_value(&doc, peer, "Endpoint", endpoint, (int)sizeof(endpoint));
	doc_value(&doc, peer, "PersistentKeepalive", keep, (int)sizeof(keep));

	if (!private_key[0] || !pub[0]) {
		doc_free(&doc);
		return fail(err, err_cap, "PrivateKey and PublicKey are required.");
	}

	if (!parse_endpoint(endpoint, host, (int)sizeof(host), &port)) {
		doc_free(&doc);
		return fail(err, err_cap, "Endpoint must look like host:port.");
	}

	addr_n = split_commas(address, items, 16);

	for (i = 0; i < addr_n; i++) {
		char	one[256];

		snprintf(one, sizeof(one), "%s", items[i]);
		strip_host(one);

		if (strchr(one, ':')) {
			if (!ip6[0]) {
				snprintf(ip6, sizeof(ip6), "%s", one);
			}
		} else if (one[0] && !ip4[0]) {
			snprintf(ip4, sizeof(ip4), "%s", one);
		}
	}

	if (!ip4[0] && !ip6[0]) {
		doc_free(&doc);
		return fail(err, err_cap, "Interface Address is required.");
	}

	full = is_full_tunnel(&doc, peer);
	ver = awg_version(&doc);
	dns_n = split_commas(dns, dns_items, 8);

	y_add(&b, "# Generated by Fairlead. Do not edit while connected.\n");
	y_add(&b, "mixed-port: 0\n");
	y_add(&b, "allow-lan: false\n");
	y_add(&b, "bind-address: 127.0.0.1\n");
	y_add(&b, "mode: rule\n");
	y_add(&b, "log-level: warning\n");
	y_add(&b, "ipv6: true\n");
	y_add(&b, "find-process-mode: always\n");
	y_add(&b, "external-controller: ");
	y_add(&b, controller);
	y_add(&b, "\n");

	if (secret && secret[0]) {
		y_add(&b, "secret: ");
		y_quote(&b, secret);
		y_add(&b, "\n");
	}

	y_add(&b, "tun:\n");
	y_add(&b, "  enable: true\n");
	y_add(&b, "  stack: system\n");
	y_add(&b, "  auto-route: true\n");
	y_add(&b, "  auto-detect-interface: true\n");
	y_add(&b, "  strict-route: true\n");
	y_add(&b, "  dns-hijack:\n");
	y_add(&b, "    - any:53\n");

	if (exclude_lan) {
		y_add(&b, "  route-exclude-address:\n");
		y_add(&b, "    - 10.0.0.0/8\n");
		y_add(&b, "    - 172.16.0.0/12\n");
		y_add(&b, "    - 192.168.0.0/16\n");
		y_add(&b, "    - 169.254.0.0/16\n");
		y_add(&b, "    - fc00::/7\n");
		y_add(&b, "    - fe80::/10\n");
	}

	/* Keep the WireGuard endpoint off the TUN when it is a literal IP. */
	if (host[0] && host[0] != '[' && (host[0] < 'A' || (strchr(host, '.') && host[0] >= '0' && host[0] <= '9') || strchr(host, ':'))) {
		int	literal;

		literal = 0;

		if (strchr(host, ':')) {
			literal = 1;
		} else {
			const char	*p;

			literal = 1;

			for (p = host; *p; p++) {
				if ((*p < '0' || *p > '9') && *p != '.') {
					literal = 0;
					break;
				}
			}
		}

		if (literal) {
			if (!exclude_lan) {
				y_add(&b, "  route-exclude-address:\n");
			}

			y_add(&b, "    - ");
			y_add(&b, host);
			y_add(&b, strchr(host, ':') ? "/128\n" : "/32\n");
		}
	}

	/*
	 * redir-host (not fake-ip): live rule reloads must not poison browser DNS caches.
	 * fake-ip + PUT /configs resets the pool cursor; flushing it leaves Chrome holding
	 * dead 198.18.x.x addresses until the tunnel is bounced. Public resolvers keep
	 * DIRECT apps alive; WG DNS stays on the proxy via remote-dns-resolve.
	 */
	y_add(&b, "dns:\n");
	y_add(&b, "  enable: true\n");
	y_add(&b, "  enhanced-mode: redir-host\n");
	y_add(&b, "  respect-rules: true\n");
	y_add(&b, "  default-nameserver:\n");
	y_add(&b, "    - 1.1.1.1\n");
	y_add(&b, "    - 8.8.8.8\n");
	y_add(&b, "  proxy-server-nameserver:\n");
	y_add(&b, "    - 1.1.1.1\n");
	y_add(&b, "    - 8.8.8.8\n");
	y_add(&b, "  nameserver:\n");
	y_add(&b, "    - 1.1.1.1\n");
	y_add(&b, "    - 8.8.8.8\n");

	y_add(&b, "proxies:\n");
	y_add(&b, "  - name: fairlead\n");
	y_add(&b, "    type: wireguard\n");
	y_add(&b, "    private-key: ");
	y_quote(&b, private_key);
	y_add(&b, "\n");

	if (ip4[0]) {
		y_add(&b, "    ip: ");
		y_add(&b, ip4);
		y_add(&b, "\n");
	}

	if (ip6[0]) {
		y_add(&b, "    ipv6: ");
		y_add(&b, ip6);
		y_add(&b, "\n");
	}

	y_add(&b, "    server: ");
	y_quote(&b, host);
	y_fmt(&b, "\n    port: %d\n", port);
	y_add(&b, "    public-key: ");
	y_quote(&b, pub);
	y_add(&b, "\n");

	if (psk[0]) {
		y_add(&b, "    pre-shared-key: ");
		y_quote(&b, psk);
		y_add(&b, "\n");
	}

	if (keep[0]) {
		/* Ranges are Amnezia-only; mihomo wants a number — take the low end. */
		int	ka;

		ka = atoi(keep);

		if (ka > 0) {
			y_fmt(&b, "    persistent-keepalive: %d\n", ka);
		}
	}

	if (mtu[0] && atoi(mtu) > 0) {
		y_fmt(&b, "    mtu: %d\n", atoi(mtu));
	}

	y_add(&b, "    udp: true\n");
	y_add(&b, "    allowed-ips:\n");
	y_add(&b, "      - 0.0.0.0/0\n");
	y_add(&b, "      - ::/0\n");

	if (dns_n > 0) {
		y_add(&b, "    remote-dns-resolve: true\n");
		y_add(&b, "    dns:\n");

		for (i = 0; i < dns_n; i++) {
			y_add(&b, "      - ");
			y_add(&b, dns_items[i]);
			y_add(&b, "\n");
		}
	}

	if (ver > 0) {
		y_add(&b, "    amnezia-wg-option:\n");
		y_fmt(&b, "      version: %d\n", ver);
		key_n = amnezia_present(&doc, keys, 32);

		for (i = 0; i < key_n; i++) {
			char	yk[64];

			doc_value(&doc, iface, keys[i], raw, (int)sizeof(raw));

			if (!raw[0] || !yaml_key(keys[i], yk, (int)sizeof(yk))) {
				continue;
			}

			y_add(&b, "      ");
			y_add(&b, yk);
			y_add(&b, ": ");

			if (is_bool_key(keys[i])) {
				if (str_icmp(raw, "true") == 0 || strcmp(raw, "1") == 0) {
					y_add(&b, "true\n");
				} else {
					y_add(&b, "false\n");
				}
			} else if (keys[i][0] == 'I' || strcmp(keys[i], "HeaderProtectionKey") == 0 ||
				strchr(raw, ':') || strchr(raw, ' ') || strchr(raw, '<')) {
				y_quote(&b, raw);
				y_add(&b, "\n");
			} else {
				y_add(&b, raw);
				y_add(&b, "\n");
			}
		}
	}

	y_add(&b, "proxy-groups:\n");
	y_add(&b, "  - name: PROXY\n");
	y_add(&b, "    type: select\n");
	y_add(&b, "    proxies:\n");
	y_add(&b, "      - fairlead\n");

	/*
	 * Direct  (exceptions): list → DIRECT, everything else → PROXY when AllowedIPs is full.
	 * Through (selected):   list → PROXY, everything else → DIRECT.
	 * Partial AllowedIPs without full tunnel: only listed destinations (and the filter list
	 * in Through) use PROXY; MATCH stays DIRECT.
	 */
	if (policy_selected) {
		emit_rule_lines(&b, rules, 1, exclude_lan, 0, block_quic);
	} else if (full) {
		emit_rule_lines(&b, rules, 0, exclude_lan, 1, block_quic);
	} else {
		char	allowed[1024];
		char	nets[16][256];
		int		n;

		doc_peer_allowed(&doc, peer, allowed, (int)sizeof(allowed));
		n = split_commas(allowed, nets, 16);
		y_add(&b, "rules:\n");

		if (exclude_lan) {
			y_add(&b, "  - GEOIP,LAN,DIRECT\n");
		}

		emit_app_rules(&b, rules, "DIRECT");

		if (block_quic) {
			emit_quic_reject(&b);
		}

		emit_host_rules(&b, rules, "DIRECT");

		for (i = 0; i < n; i++) {
			y_add(&b, "  - IP-CIDR,");
			y_add(&b, nets[i]);
			y_add(&b, ",PROXY,no-resolve\n");
		}

		y_add(&b, "  - MATCH,DIRECT\n");
	}

	doc_free(&doc);

	if (!b.data) {
		return fail(err, err_cap, "Out of memory.");
	}

	return b.data;
}
