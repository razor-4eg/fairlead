//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// net.c -- IP/CIDR, DNS, bypass planner.
//
//================================================================

#include "net.h"
#include "base.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

/*
===============
max_prefix
32 or 128.
===============
*/
static int max_prefix(int v6) {
	return v6 ? 128 : 32;
}

/*
===============
ip_parse
Parse a numeric address. Returns 0 when text is not one.
===============
*/
int ip_parse(const char *text, ip_t *out) {
	char			buf[128];
	unsigned char	v4[4];
	unsigned char	v6[16];
	int				i;

	if (!text || !out) {
		return 0;
	}

	i = 0;

	while (*text && i < (int)sizeof(buf) - 1) {
		buf[i++] = *text++;
	}

	buf[i] = '\0';
	str_trim(buf);

	if (buf[0] == '\0') {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	if (inet_pton(AF_INET, buf, v4) == 1) {
		memcpy(out->bytes, v4, 4);
		return 1;
	}

	if (inet_pton(AF_INET6, buf, v6) == 1) {
		out->v6 = 1;
		memcpy(out->bytes, v6, 16);
		return 1;
	}

	return 0;
}

/*
===============
ip_format
Write the numeric form. Empty when ip is missing.
===============
*/
void ip_format(const ip_t *ip, char *buf, int cap) {
	if (!buf || cap <= 0) {
		return;
	}

	buf[0] = '\0';

	if (!ip) {
		return;
	}

	if (!ip->v6) {
		inet_ntop(AF_INET, ip->bytes, buf, (socklen_t)cap);
	} else {
		inet_ntop(AF_INET6, ip->bytes, buf, (socklen_t)cap);
	}
}

/*
===============
ip_equal
Same family and the same bytes.
===============
*/
int ip_equal(const ip_t *a, const ip_t *b) {
	int		n;

	if (!a || !b || a->v6 != b->v6) {
		return 0;
	}

	n = a->v6 ? 16 : 4;
	return memcmp(a->bytes, b->bytes, (size_t)n) == 0;
}

/*
===============
ip_unspecified
0.0.0.0 or ::.
===============
*/
int ip_unspecified(const ip_t *ip) {
	int		n;
	int		i;

	if (!ip) {
		return 1;
	}

	n = ip->v6 ? 16 : 4;

	for (i = 0; i < n; i++) {
		if (ip->bytes[i] != 0) {
			return 0;
		}
	}

	return 1;
}

/*
===============
ip_loopback
127.0.0.0/8 or ::1.
===============
*/
int ip_loopback(const ip_t *ip) {
	int		i;

	if (!ip) {
		return 0;
	}

	if (!ip->v6) {
		return ip->bytes[0] == 127;
	}

	for (i = 0; i < 15; i++) {
		if (ip->bytes[i] != 0) {
			return 0;
		}
	}

	return ip->bytes[15] == 1;
}

/*
===============
ip_multicast
224.0.0.0/4 or ff00::/8.
===============
*/
int ip_multicast(const ip_t *ip) {
	if (!ip) {
		return 0;
	}

	if (!ip->v6) {
		return ip->bytes[0] >= 224 && ip->bytes[0] <= 239;
	}

	return ip->bytes[0] == 0xff;
}

/*
===============
ip_link_local
169.254.0.0/16 or fe80::/10.
===============
*/
int ip_link_local(const ip_t *ip) {
	if (!ip) {
		return 0;
	}

	if (!ip->v6) {
		return ip->bytes[0] == 169 && ip->bytes[1] == 254;
	}

	return ip->bytes[0] == 0xfe && (ip->bytes[1] & 0xc0) == 0x80;
}

/*
===============
ip_broadcast
255.255.255.255.
===============
*/
int ip_broadcast(const ip_t *ip) {
	if (!ip || ip->v6) {
		return 0;
	}

	return ip->bytes[0] == 255 && ip->bytes[1] == 255 && ip->bytes[2] == 255 && ip->bytes[3] == 255;
}

/*
===============
ip_routable
An address the tunnel is allowed to own.
===============
*/
int ip_routable(const ip_t *ip) {
	return ip && !ip_unspecified(ip) && !ip_loopback(ip) && !ip_multicast(ip)
		&& !ip_link_local(ip) && !ip_broadcast(ip);
}

/*
===============
ip_mask
Zero the host bits past prefix.
===============
*/
void ip_mask(ip_t *ip, int prefix) {
	int		limit;
	int		bits;
	int		full;
	int		rem;
	int		i;

	if (!ip) {
		return;
	}

	limit = max_prefix(ip->v6);
	bits = prefix;

	if (bits < 0) {
		bits = 0;
	}

	if (bits > limit) {
		bits = limit;
	}

	full = bits / 8;
	rem = bits % 8;

	if (full < (ip->v6 ? 16 : 4)) {
		if (rem == 0) {
			for (i = full; i < (ip->v6 ? 16 : 4); i++) {
				ip->bytes[i] = 0;
			}
		} else {
			ip->bytes[full] = (unsigned char)(ip->bytes[full] & (unsigned char)(0xff << (8 - rem)));

			for (i = full + 1; i < (ip->v6 ? 16 : 4); i++) {
				ip->bytes[i] = 0;
			}
		}
	}
}

/*
===============
cidr_parse
Parse addr/prefix. The stored address is masked.
===============
*/
int cidr_parse(const char *text, cidr_t *out) {
	char	buf[160];
	char	*slash;
	ip_t	ip;
	int		prefix;
	int		i;

	if (!text || !out) {
		return 0;
	}

	i = 0;

	while (*text && i < (int)sizeof(buf) - 1) {
		buf[i++] = *text++;
	}

	buf[i] = '\0';
	str_trim(buf);

	if (buf[0] == '\0') {
		return 0;
	}

	slash = strchr(buf, '/');

	if (slash && strchr(slash + 1, '/')) {
		return 0;
	}

	if (slash) {
		*slash = '\0';
	}

	if (!ip_parse(buf, &ip)) {
		return 0;
	}

	if (!slash) {
		prefix = max_prefix(ip.v6);
	} else {
		prefix = 0;

		if (slash[1] == '\0') {
			return 0;
		}

		for (i = 1; slash[i]; i++) {
			if (slash[i] < '0' || slash[i] > '9') {
				return 0;
			}

			prefix = prefix * 10 + (slash[i] - '0');

			if (prefix > 128) {
				return 0;
			}
		}
	}

	if (prefix > max_prefix(ip.v6)) {
		return 0;
	}

	ip_mask(&ip, prefix);
	out->ip = ip;
	out->prefix = prefix;
	return 1;
}

/*
===============
cidr_format
Write addr/prefix.
===============
*/
void cidr_format(const cidr_t *cidr, char *buf, int cap) {
	char	ip[64];

	if (!buf || cap <= 0) {
		return;
	}

	buf[0] = '\0';

	if (!cidr) {
		return;
	}

	ip_format(&cidr->ip, ip, (int)sizeof(ip));
	snprintf(buf, (size_t)cap, "%s/%d", ip, cidr->prefix);
}

/*
===============
cidr_contains
True when ip is inside cidr.
===============
*/
int cidr_contains(const cidr_t *cidr, const ip_t *ip) {
	int				full;
	int				rem;
	int				i;
	unsigned char	mask;

	if (!cidr || !ip || cidr->ip.v6 != ip->v6) {
		return 0;
	}

	if (cidr->prefix == 0) {
		return 1;
	}

	full = cidr->prefix / 8;
	rem = cidr->prefix % 8;

	for (i = 0; i < full; i++) {
		if (ip->bytes[i] != cidr->ip.bytes[i]) {
			return 0;
		}
	}

	if (rem == 0) {
		return 1;
	}

	mask = (unsigned char)(0xff << (8 - rem));
	return (ip->bytes[full] & mask) == (cidr->ip.bytes[full] & mask);
}

/*
===============
cidr_max_prefix
32 or 128 for this network.
===============
*/
int cidr_max_prefix(const cidr_t *cidr) {
	return cidr && cidr->ip.v6 ? 128 : 32;
}

/*
===============
endpoint_host
Host from host:port or [ipv6]:port. (none) and empty fail.
===============
*/
int endpoint_host(const char *endpoint, char *buf, int cap) {
	char	tmp[256];
	char	*end;
	char	*colon;
	int		i;

	if (!buf || cap <= 0) {
		return 0;
	}

	buf[0] = '\0';

	if (!endpoint) {
		return 0;
	}

	i = 0;

	while (endpoint[i] && i < (int)sizeof(tmp) - 1) {
		tmp[i] = endpoint[i];
		i++;
	}

	tmp[i] = '\0';
	str_trim(tmp);

	if (tmp[0] == '\0' || strcmp(tmp, "(none)") == 0) {
		return 0;
	}

	if (tmp[0] == '[') {
		end = strchr(tmp, ']');

		if (!end || end == tmp + 1) {
			return 0;
		}

		*end = '\0';
		snprintf(buf, (size_t)cap, "%s", tmp + 1);
		return buf[0] != '\0';
	}

	colon = strrchr(tmp, ':');

	if (!colon) {
		snprintf(buf, (size_t)cap, "%s", tmp);
		return 1;
	}

	if (colon[1] == '\0') {
		snprintf(buf, (size_t)cap, "%s", tmp);
		return 1;
	}

	for (i = 1; colon[i]; i++) {
		if (colon[i] < '0' || colon[i] > '9') {
			snprintf(buf, (size_t)cap, "%s", tmp);
			return 1;
		}
	}

	*colon = '\0';

	if (tmp[0] == '\0') {
		return 0;
	}

	snprintf(buf, (size_t)cap, "%s", tmp);
	return 1;
}

/*
===============
endpoint_port
Port from an endpoint, or 0.
===============
*/
int endpoint_port(const char *endpoint, int *port) {
	const char	*p;
	int			value;
	int			i;

	if (!endpoint || !port) {
		return 0;
	}

	if (endpoint[0] == '[') {
		p = strrchr(endpoint, ']');

		if (!p || p[1] != ':') {
			return 0;
		}

		p += 2;
	} else {
		p = strrchr(endpoint, ':');

		if (!p) {
			return 0;
		}

		p++;
	}

	if (*p == '\0') {
		return 0;
	}

	value = 0;

	for (i = 0; p[i]; i++) {
		if (p[i] < '0' || p[i] > '9') {
			return 0;
		}

		value = value * 10 + (p[i] - '0');

		if (value > 65535) {
			return 0;
		}
	}

	*port = value;
	return 1;
}

/*
===============
endpoint_valid
Host and a port in 1..65535.
===============
*/
int endpoint_valid(const char *endpoint) {
	char	host[256];
	int		port;

	if (!endpoint_host(endpoint, host, (int)sizeof(host))) {
		return 0;
	}

	if (!endpoint_port(endpoint, &port)) {
		return 0;
	}

	return port >= 1 && port <= 65535;
}

/*
===============
socket_endpoint
ip:port, using the port from the original endpoint.
===============
*/
int socket_endpoint(const char *ip, const char *original, char *buf, int cap) {
	int		port;

	if (!ip || !buf || cap <= 0) {
		return 0;
	}

	if (!endpoint_port(original, &port) || port < 1) {
		return 0;
	}

	if (strchr(ip, ':')) {
		snprintf(buf, (size_t)cap, "[%s]:%d", ip, port);
	} else {
		snprintf(buf, (size_t)cap, "%s:%d", ip, port);
	}

	return 1;
}

/*
===============
snap_value
Copy the text after a route-table label.
===============
*/
static int snap_value(const char *line, const char *label, char *buf, int cap) {
	size_t		n;
	const char	*p;

	n = strlen(label);

	if (strncmp(line, label, n) != 0) {
		return 0;
	}

	p = line + n;

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	if (*p == '\0') {
		return 0;
	}

	snprintf(buf, (size_t)cap, "%s", p);
	return 1;
}

/*
===============
route_snapshot
Read one route line from netstat-style text.
===============
*/
void route_snapshot(const char *text, route_snap_t *out) {
	const char	*p;
	const char	*eol;
	char		line[256];
	int			n;

	memset(out, 0, sizeof(*out));

	if (!text) {
		return;
	}

	p = text;

	while (*p) {
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

		str_trim(line);

		if (!out->has_destination && snap_value(line, "destination:", out->destination, (int)sizeof(out->destination))) {
			out->has_destination = 1;
		} else if (!out->has_mask && snap_value(line, "mask:", out->mask, (int)sizeof(out->mask))) {
			out->has_mask = 1;
		} else if (!out->has_gateway && snap_value(line, "gateway:", out->gateway, (int)sizeof(out->gateway))) {
			out->has_gateway = 1;
		} else if (!out->has_interface && snap_value(line, "interface:", out->interface_name, (int)sizeof(out->interface_name))) {
			out->has_interface = 1;
		}

		if (!eol) {
			break;
		}

		p = eol + 1;
	}
}

/*
===============
prefix_from_u32
Prefix length of a left-aligned mask, or -1.
===============
*/
static int prefix_from_u32(unsigned int value) {
	int		bits;
	int		seen_zero;
	int		shift;
	int		on;

	bits = 0;
	seen_zero = 0;

	for (shift = 31; shift >= 0; shift--) {
		on = (value & (1u << shift)) != 0;

		if (on) {
			if (seen_zero) {
				return -1;
			}

			bits++;
		} else {
			seen_zero = 1;
		}
	}

	return bits;
}

/*
===============
prefix_dotted
Prefix length of a dotted IPv4 mask.
===============
*/
static int prefix_dotted(const char *mask) {
	unsigned int	parts[4];
	unsigned int	value;
	int				i;
	const char		*p;

	p = mask;

	for (i = 0; i < 4; i++) {
		if (*p < '0' || *p > '9') {
			return -1;
		}

		parts[i] = 0;

		while (*p >= '0' && *p <= '9') {
			parts[i] = parts[i] * 10 + (unsigned)(*p - '0');

			if (parts[i] > 255) {
				return -1;
			}

			p++;
		}

		if (i < 3) {
			if (*p != '.') {
				return -1;
			}

			p++;
		}
	}

	if (*p != '\0') {
		return -1;
	}

	value = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
	return prefix_from_u32(value);
}

/*
===============
prefix_hex
Prefix length of a 0x mask from ifconfig.
===============
*/
static int prefix_hex(const char *mask) {
	unsigned int	value;
	const char		*p;
	char			c;

	value = 0;
	p = mask;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
	}

	if (*p == '\0') {
		return -1;
	}

	while (*p) {
		c = *p++;

		value <<= 4;

		if (c >= '0' && c <= '9') {
			value |= (unsigned)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			value |= (unsigned)(c - 'a' + 10);
		} else if (c >= 'A' && c <= 'F') {
			value |= (unsigned)(c - 'A' + 10);
		} else {
			return -1;
		}
	}

	return prefix_from_u32(value);
}

/*
===============
route_exact
True when the snapshot is this network and not a wider route.
===============
*/
int route_exact(const cidr_t *cidr, const route_snap_t *snap) {
	ip_t		dest;
	int			prefix;
	ip_t		masked;
	const char	*p;

	if (!cidr || !snap || !snap->has_destination) {
		return 0;
	}

	if (strcmp(snap->destination, "default") == 0) {
		return 0;
	}

	if (!ip_parse(snap->destination, &dest) || dest.v6 != cidr->ip.v6) {
		return 0;
	}

	if (snap->has_mask && strcmp(snap->mask, "default") != 0) {
		prefix = -1;

		if (snap->mask[0] >= '0' && snap->mask[0] <= '9' && strchr(snap->mask, '.') == NULL) {
			prefix = 0;
			{
				p = snap->mask;

				while (*p >= '0' && *p <= '9') {
					prefix = prefix * 10 + (*p - '0');
					p++;
				}

				if (*p != '\0' || prefix < 0 || prefix > cidr_max_prefix(cidr)) {
					prefix = -1;
				}
			}
		}

		if (prefix < 0) {
			prefix = prefix_dotted(snap->mask);
		}

		if (prefix < 0) {
			return 0;
		}
	} else {
		prefix = cidr_max_prefix(cidr);
	}

	masked = dest;
	ip_mask(&masked, prefix);
	return prefix == cidr->prefix && ip_equal(&masked, &cidr->ip);
}

/*
===============
push_unique
Append cidr when it is not already in the list.
===============
*/
static int push_unique(char out[][64], int count, int cap, const char *cidr) {
	int		i;

	if (count >= cap) {
		return count;
	}

	for (i = 0; i < count; i++) {
		if (strcmp(out[i], cidr) == 0) {
			return count;
		}
	}

	snprintf(out[count], 64, "%s", cidr);
	return count + 1;
}

/*
===============
ifconfig_networks
On-link networks from one ifconfig listing.
===============
*/
int ifconfig_networks(const char *text, char out[][64], int cap) {
	const char	*p;
	const char	*eol;
	char		line[512];
	int			count;
	int			n;
	int			pc;
	char		*tok;
	int			mi;
	int			k;
	int			prefix;
	char		*pct;
	int			pi;

	count = 0;

	if (!text) {
		return 0;
	}

	p = text;

	while (*p) {
		char *parts[16];
		pc = 0;

		eol = strchr(p, '\n');
		n = eol ? (int)(eol - p) : (int)strlen(p);

		if (n > (int)sizeof(line) - 1) {
			n = (int)sizeof(line) - 1;
		}

		memcpy(line, p, (size_t)n);
		line[n] = '\0';
		str_trim(line);
		tok = line;

		while (tok && *tok && pc < 16) {
			while (*tok == ' ' || *tok == '\t') {
				tok++;
			}

			if (*tok == '\0') {
				break;
			}

			parts[pc++] = tok;

			while (*tok && *tok != ' ' && *tok != '\t') {
				tok++;
			}

			if (*tok) {
				*tok++ = '\0';
			}
		}

		if (pc >= 4 && strcmp(parts[0], "inet") == 0) {
			ip_t ip;
			mi = -1;
			cidr_t cidr;
			char formatted[64];

			if (ip_parse(parts[1], &ip)) {
				for (k = 0; k < pc - 1; k++) {
					if (strcmp(parts[k], "netmask") == 0) {
						mi = k + 1;
					}
				}

				prefix = mi >= 0 ? prefix_hex(parts[mi]) : -1;

				if (prefix >= 0) {
					ip_mask(&ip, prefix);
					cidr.ip = ip;
					cidr.prefix = prefix;

					if (prefix > 0 && prefix < 32) {
						cidr_format(&cidr, formatted, (int)sizeof(formatted));
						count = push_unique(out, count, cap, formatted);
					}
				}
			}
		} else if (pc >= 4 && strcmp(parts[0], "inet6") == 0) {
			char addr[128];
			ip_t ip;
			pi = -1;
			prefix = -1;

			snprintf(addr, sizeof(addr), "%s", parts[1]);
			pct = strchr(addr, '%');

			if (pct) {
				*pct = '\0';
			}

			if (ip_parse(addr, &ip) && !ip_link_local(&ip) && !ip_loopback(&ip)) {
				for (k = 0; k < pc - 1; k++) {
					if (strcmp(parts[k], "prefixlen") == 0) {
						pi = k + 1;
					}
				}

				if (pi >= 0) {
					prefix = 0;

					for (k = 0; parts[pi][k]; k++) {
						if (parts[pi][k] < '0' || parts[pi][k] > '9') {
							prefix = -1;
							break;
						}

						prefix = prefix * 10 + (parts[pi][k] - '0');
					}
				}

				if (prefix > 0 && prefix < 128) {
					cidr_t cidr;
					char formatted[64];

					ip_mask(&ip, prefix);
					cidr.ip = ip;
					cidr.prefix = prefix;
					cidr_format(&cidr, formatted, (int)sizeof(formatted));
					count = push_unique(out, count, cap, formatted);
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



/*
===============
utf8_decode
One UTF-8 sequence. Rejects overlong forms.
===============
*/
static int utf8_decode(const unsigned char *s, int len, unsigned int *cp, int *used) {
	if (len <= 0) {
		return 0;
	}

	if (s[0] < 0x80) {
		*cp = s[0];
		*used = 1;
		return 1;
	}

	if ((s[0] & 0xe0) == 0xc0 && len >= 2) {
		*cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
		*used = 2;
		return *cp >= 0x80;
	}

	if ((s[0] & 0xf0) == 0xe0 && len >= 3) {
		*cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
		*used = 3;
		return *cp >= 0x800;
	}

	if ((s[0] & 0xf8) == 0xf0 && len >= 4) {
		*cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
		*used = 4;
		return *cp >= 0x10000 && *cp <= 0x10ffff;
	}

	return 0;
}

/*
===============
adapt_bias
Punycode bias adapt.
===============
*/
static int adapt_bias(int delta, int numpoints, int first) {
	int		k;

	delta = first ? delta / 700 : delta / 2;
	delta += delta / numpoints;
	k = 0;

	while (delta > ((36 - 1) * 26) / 2) {
		delta /= 36 - 1;
		k += 36;
	}

	return k + ((36 - 1 + 1) * delta) / (delta + 38);
}

/*
===============
encode_digit
One punycode digit.
===============
*/
static char encode_digit(int d) {
	return (char)(d < 26 ? 'a' + d : '0' + (d - 26));
}

/*
===============
puny_encode
Punycode a label that is not already ASCII.
===============
*/
static int puny_encode(const unsigned int *cps, int n, char *out, int cap) {
	int				b;
	int				h;
	int				i;
	int				bias;
	int				delta;
	unsigned int	m;
	unsigned int	nn;
	int				used;
	int				q;
	int				k;
	int				t;

	b = 0;
	bias = 72;
	delta = 0;
	nn = 128;
	used = 0;

	for (i = 0; i < n; i++) {
		if (cps[i] < 128) {
			if (used + 1 >= cap) {
				return 0;
			}

			out[used++] = (char)cps[i];
			b++;
		}
	}

	h = b;

	if (b > 0) {
		if (used + 1 >= cap) {
			return 0;
		}

		out[used++] = '-';
	}

	while (h < n) {
		m = 0xffffffffu;

		for (i = 0; i < n; i++) {
			if (cps[i] >= nn && cps[i] < m) {
				m = cps[i];
			}
		}

		delta += (int)(m - nn) * (h + 1);
		nn = m;

		for (i = 0; i < n; i++) {
			if (cps[i] < nn) {
				delta++;
			}

			if (cps[i] == nn) {
				q = delta;

				for (k = 36;; k += 36) {
					t = k <= bias ? 1 : (k >= bias + 26 ? 26 : k - bias);

					if (q < t) {
						break;
					}

					if (used + 1 >= cap) {
						return 0;
					}

					out[used++] = encode_digit(t + (q - t) % (36 - t));
					q = (q - t) / (36 - t);
				}

				if (used + 1 >= cap) {
					return 0;
				}

				out[used++] = encode_digit(q);
				bias = adapt_bias(delta, h + 1, h == b);
				delta = 0;
				h++;
			}
		}

		delta++;
		nn++;
	}

	out[used] = '\0';
	return 1;
}

/*
===============
label_ok
An LDH label: length, hyphens, and the allowed characters.
===============
*/
static int label_ok(const char *label) {
	int		n;
	int		i;
	char	c;
	int		ok;

	n = (int)strlen(label);

	if (n <= 0 || n > 63) {
		return 0;
	}

	if (label[0] == '-' || label[n - 1] == '-') {
		return 0;
	}

	for (i = 0; i < n; i++) {
		c = label[i];

		ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';

		if (!ok) {
			return 0;
		}
	}

	return 1;
}

/*
===============
domain_normalize
Lowercase host, punycode where needed. Returns 0 when the name is rejected.
===============
*/
int domain_normalize(const char *raw, char *buf, int cap) {
	char			text[512];
	int				i;
	int				len;
	int				high;
	const char		*p;
	int				labels;
	ip_t			ip;
	char			*s;
	unsigned char	c;
	char			result[512];
	int				rlen;
	const char		*dot;
	int				lablen;
	int				nc;
	int				pos;
	int				basic;
	const char		*use;
	unsigned int	cp;
	int				used;

	high = 0;
	labels = 0;

	if (!raw || !buf || cap <= 0) {
		return 0;
	}

	buf[0] = '\0';
	snprintf(text, sizeof(text), "%s", raw);
	{
		s = text;

		while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
			s++;
		}

		memmove(text, s, strlen(s) + 1);
		len = (int)strlen(text);

		while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == '.')) {
			if (text[len - 1] != '.' && text[len - 1] != ' ' && text[len - 1] != '\t') {
				break;
			}

			/* strip one trailing dot only after trim of space; do spaces first */
			text[len - 1] = '\0';
			len--;
		}
	}

	for (i = 0; text[i]; i++) {
		if (text[i] >= 'A' && text[i] <= 'Z') {
			text[i] = (char)(text[i] - 'A' + 'a');
		}
	}

	/* trim a single trailing dot that survived lowercase */
	len = (int)strlen(text);

	if (len > 0 && text[len - 1] == '.') {
		text[len - 1] = '\0';
	}

	if (text[0] == '\0') {
		return 0;
	}

	for (i = 0; text[i]; i++) {
		c = (unsigned char)text[i];

		if (c == '*' || c == '/' || c == '\\' || c == ':' || c == ' ' || c == '@') {
			return 0;
		}

		if (c > 127) {
			high = 1;
		}
	}

	if (high) {
		rlen = 0;

		p = text;
		result[0] = '\0';

		while (*p) {
			dot = strchr(p, '.');
			lablen = dot ? (int)(dot - p) : (int)strlen(p);
			unsigned int cps[256];
			nc = 0;
			pos = 0;
			basic = 1;
			char encoded[256];

			while (pos < lablen && nc < 256) {
				if (!utf8_decode((const unsigned char *)p + pos, lablen - pos, &cp, &used)) {
					return 0;
				}

				if (cp > 127) {
					basic = 0;
				}

				if (cp >= 'A' && cp <= 'Z') {
					cp = cp - 'A' + 'a';
				}

				cps[nc++] = cp;
				pos += used;
			}

			if (pos != lablen) {
				return 0;
			}

			if (basic) {
				if (lablen >= (int)sizeof(encoded)) {
					return 0;
				}

				memcpy(encoded, p, (size_t)lablen);
				encoded[lablen] = '\0';
				use = encoded;
			} else {
				char body[256];

				if (!puny_encode(cps, nc, body, (int)sizeof(body))) {
					return 0;
				}

				snprintf(encoded, sizeof(encoded), "xn--%s", body);
				use = encoded;
			}

			if (rlen && rlen + 1 < (int)sizeof(result)) {
				result[rlen++] = '.';
			}

			snprintf(result + rlen, sizeof(result) - (size_t)rlen, "%s", use);
			rlen = (int)strlen(result);

			if (!dot) {
				break;
			}

			p = dot + 1;
		}

		snprintf(text, sizeof(text), "%s", result);
	}

	if (ip_parse(text, &ip)) {
		return 0;
	}

	if ((int)strlen(text) > 253) {
		return 0;
	}

	p = text;

	while (*p) {
		dot = strchr(p, '.');
		lablen = dot ? (int)(dot - p) : (int)strlen(p);
		char label[64];

		if (lablen <= 0 || lablen > 63) {
			return 0;
		}

		memcpy(label, p, (size_t)lablen);
		label[lablen] = '\0';

		if (!label_ok(label)) {
			return 0;
		}

		labels++;

		if (!dot) {
			break;
		}

		p = dot + 1;

		if (*p == '\0') {
			return 0;
		}
	}

	if (labels < 2) {
		return 0;
	}

	snprintf(buf, (size_t)cap, "%s", text);
	return 1;
}

/*
===============
append_name
Write a DNS name in wire form.
===============
*/
static int append_name(const char *name, unsigned char *out, int cap, int used) {
	const char	*p;
	const char	*dot;
	int			n;

	p = name;

	while (*p) {
		dot = strchr(p, '.');
		n = dot ? (int)(dot - p) : (int)strlen(p);

		if (n <= 0 || n > 63 || used + 1 + n > cap) {
			return -1;
		}

		out[used++] = (unsigned char)n;
		memcpy(out + used, p, (size_t)n);
		used += n;

		if (!dot) {
			break;
		}

		p = dot + 1;
	}

	if (used + 1 > cap) {
		return -1;
	}

	out[used++] = 0;
	return used;
}

/*
===============
put16
Big-endian 16 bits.
===============
*/
static void put16(unsigned char *p, unsigned int v) {
	p[0] = (unsigned char)((v >> 8) & 0xff);
	p[1] = (unsigned char)(v & 0xff);
}

/*
===============
put32
Big-endian 32 bits.
===============
*/
static void put32(unsigned char *p, unsigned int v) {
	p[0] = (unsigned char)((v >> 24) & 0xff);
	p[1] = (unsigned char)((v >> 16) & 0xff);
	p[2] = (unsigned char)((v >> 8) & 0xff);
	p[3] = (unsigned char)(v & 0xff);
}

/*
===============
dns_query
A query packet. Returns the byte count, or 0.
===============
*/
int dns_query(uint16_t id, const char *name, uint16_t type, int edns, unsigned char *out, int cap) {
	char	norm[512];
	int		used;

	if (!domain_normalize(name, norm, (int)sizeof(norm))) {
		return 0;
	}

	if (cap < 32) {
		return 0;
	}

	put16(out, id);
	put16(out + 2, 0x0100);
	put16(out + 4, 1);
	put16(out + 6, 0);
	put16(out + 8, 0);
	put16(out + 10, edns ? 1 : 0);
	used = append_name(norm, out, cap, 12);

	if (used < 0 || used + 4 > cap) {
		return 0;
	}

	put16(out + used, type);
	put16(out + used + 2, 1);
	used += 4;

	if (edns) {
		if (used + 11 > cap) {
			return 0;
		}

		out[used++] = 0;
		put16(out + used, 41);
		put16(out + used + 2, 1232);
		put32(out + used + 4, 0);
		put16(out + used + 8, 0);
		used += 10;
	}

	return used;
}

/*
===============
read16
Read a big-endian 16-bit field, or 0.
===============
*/
static int read16(const unsigned char *d, int len, int off, unsigned int *v) {
	if (off + 2 > len) {
		return 0;
	}

	*v = ((unsigned int)d[off] << 8) | d[off + 1];
	return 1;
}

/*
===============
read32
Read a big-endian 32-bit field, or 0.
===============
*/
static int read32(const unsigned char *d, int len, int off, unsigned int *v) {
	if (off + 4 > len) {
		return 0;
	}

	*v = ((unsigned int)d[off] << 24) | ((unsigned int)d[off + 1] << 16) | ((unsigned int)d[off + 2] << 8) | d[off + 3];
	return 1;
}

/*
===============
skip_name
Step past a compressed DNS name.
===============
*/
static int skip_name(const unsigned char *d, int len, int *offset) {
	int		cursor;
	int		jumped;
	int		hops;
	int		length;
	int		pointer;

	cursor = *offset;
	jumped = 0;
	hops = 0;

	while (cursor < len) {
		length = d[cursor];

		if (length == 0) {
			if (!jumped) {
				*offset = cursor + 1;
			}

			return 1;
		}

		if ((length & 0xc0) == 0xc0) {
			if (cursor + 1 >= len) {
				return 0;
			}

			pointer = ((length & 0x3f) << 8) | d[cursor + 1];

			if (pointer >= len || hops >= 8) {
				return 0;
			}

			if (!jumped) {
				*offset = cursor + 2;
			}

			cursor = pointer;
			jumped = 1;
			hops++;
			continue;
		}

		if (length > 63 || cursor + 1 + length > len) {
			return 0;
		}

		cursor += 1 + length;

		if (!jumped) {
			*offset = cursor;
		}
	}

	return 0;
}

/*
===============
read_section
Parse one answer section into address records.
===============
*/
static int read_section(const unsigned char *d, int len, int *offset, int count, dns_rec_t *recs, int rec_cap, int *rec_count, int *cname) {
	int				i;
	unsigned int	type;
	unsigned int	class_;
	unsigned int	ttl;
	unsigned int	rdlen;

	*rec_count = 0;

	for (i = 0; i < count; i++) {
		if (!skip_name(d, len, offset)) {
			return 0;
		}

		if (!read16(d, len, *offset, &type)) {
			return 0;
		}

		*offset += 2;

		if (!read16(d, len, *offset, &class_)) {
			return 0;
		}

		*offset += 2;
		(void)class_;

		if (!read32(d, len, *offset, &ttl)) {
			return 0;
		}

		*offset += 4;

		if (!read16(d, len, *offset, &rdlen)) {
			return 0;
		}

		*offset += 2;

		if (*offset + (int)rdlen > len) {
			return 0;
		}

		if (type == 5 && cname) {
			*cname = 1;
		}

		if ((type == DNS_A && rdlen == 4) || (type == DNS_AAAA && rdlen == 16)) {
			ip_t ip;

			memset(&ip, 0, sizeof(ip));
			ip.v6 = type == DNS_AAAA;
			memcpy(ip.bytes, d + *offset, rdlen);

			if (*rec_count < rec_cap) {
				ip_format(&ip, recs[*rec_count].address, 64);
				recs[*rec_count].ttl = ttl;
				(*rec_count)++;
			}
		}

		*offset += (int)rdlen;
	}

	return 1;
}

/*
===============
dns_parse
Parse a reply when the id matches and the QR bit is set.
===============
*/
int dns_parse(const unsigned char *data, int len, uint16_t id, dns_reply_t *out) {
	unsigned int	got;
	unsigned int	flags;
	unsigned int	questions;
	unsigned int	answers;
	unsigned int	authority;
	unsigned int	additional;
	int				offset;
	int				i;
	int				dummy;
	dns_rec_t	junk[8];
	int				junk_count;

	offset = 12;
	dummy = 0;
	junk_count = 0;

	if (!data || !out || len < 12) {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	if (!read16(data, len, 0, &got) || got != id) {
		return 0;
	}

	if (!read16(data, len, 2, &flags) || (flags & 0x8000) == 0) {
		return 0;
	}

	if (!read16(data, len, 4, &questions)) {
		return 0;
	}

	if (!read16(data, len, 6, &answers)) {
		return 0;
	}

	if (!read16(data, len, 8, &authority)) {
		return 0;
	}

	if (!read16(data, len, 10, &additional)) {
		return 0;
	}

	if (questions > 8 || answers > 64 || authority > 64 || additional > 64) {
		return 0;
	}

	for (i = 0; i < (int)questions; i++) {
		if (!skip_name(data, len, &offset)) {
			return 0;
		}

		if (offset + 4 > len) {
			return 0;
		}

		offset += 4;
	}

	if (!read_section(data, len, &offset, (int)answers, out->answers, 32, &out->answer_count, &out->saw_cname)) {
		return 0;
	}

	if (!read_section(data, len, &offset, (int)authority, junk, 8, &junk_count, &dummy)) {
		return 0;
	}

	if (!read_section(data, len, &offset, (int)additional, out->additional, 32, &out->additional_count, &dummy)) {
		return 0;
	}

	out->rcode = (int)(flags & 0x000f);
	out->truncated = (flags & 0x0200) != 0;
	return 1;
}


#include <stdlib.h>
/*
===============
warn
Keep a plan warning when there is room.
===============
*/
static void warn(bypass_t *out, const char *text) {
	if (out->warning_count >= 16) {
		return;
	}

	snprintf(out->warnings[out->warning_count], 256, "%s", text);
	out->warning_count++;
}

/*
===============
push_cidr
Append one network.
===============
*/
static int push_cidr(cidr_t *list, int *count, int cap, const cidr_t *cidr) {
	if (!list || *count >= cap) {
		return 0;
	}

	list[(*count)++] = *cidr;
	return 1;
}

/*
===============
cmp_str
qsort helper for the formatted CIDR list.
===============
*/
static int cmp_str(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

/*
===============
unique_sort
Format, drop duplicates, and sort.
===============
*/
static int unique_sort(const cidr_t *list, int count, char out[][64], int cap) {
	int		i;
	int		n;
	int		k;
	int		seen;

	n = 0;

	for (i = 0; i < count && n < cap; i++) {
		char text[64];
		seen = 0;

		cidr_format(&list[i], text, (int)sizeof(text));

		for (k = 0; k < n; k++) {
			if (strcmp(out[k], text) == 0) {
				seen = 1;
			}
		}

		if (!seen) {
			snprintf(out[n], 64, "%s", text);
			n++;
		}
	}

	if (n > 1) {
		qsort(out, (size_t)n, 64, cmp_str);
	}

	return n;
}

/*
===============
take
Accept one rule, or record why it was skipped.
===============
*/
static void take(bypass_t *out, int selected, const char *raw, const char *origin,
	const ip_t *endpoints, int endpoint_count, cidr_t *list, int *count) {
	cidr_t cidr;
	char msg[256];
	int i;

	if (!cidr_parse(raw, &cidr)) {
		snprintf(msg, sizeof(msg), "Skipped invalid address %s.", raw);
		warn(out, msg);
		return;
	}

	if (cidr.prefix == 0) {
		if (selected) {
			snprintf(msg, sizeof(msg), "Address %s covers the whole internet. Use the other filter for that.", raw);
		} else {
			snprintf(msg, sizeof(msg), "Address %s is too broad and would disable the whole tunnel.", raw);
		}

		warn(out, msg);
		return;
	}

	for (i = 0; i < endpoint_count; i++) {
		if (cidr_contains(&cidr, &endpoints[i])) {
			if (selected) {
				snprintf(msg, sizeof(msg), "%s overlaps the WireGuard server and stays on the regular network.", origin);
			} else {
				snprintf(msg, sizeof(msg), "%s overlaps the WireGuard server and stays in the tunnel.", origin);
			}

			warn(out, msg);
			return;
		}
	}

	push_cidr(list, count, BYPASS_MAX, &cidr);
}

/*
===============
bypass_plan
Build the tunnel routes and the direct routes for one policy.
===============
*/
void bypass_plan(
	int policy_selected,
	const char **rules, int rule_count,
	const char **learned, int learned_count,
	const char **lan, int lan_count,
	int exclude_lan,
	const char **endpoints, int endpoint_count,
	bypass_t *out)
{
	cidr_t *cidrs;
	cidr_t *direct;
	int cidr_count = 0;
	int direct_count = 0;
	ip_t eps[16];
	int ep_count = 0;
	int i;
	int skipped = 0;

	memset(out, 0, sizeof(*out));
	cidrs = (cidr_t *)calloc((size_t)BYPASS_MAX, sizeof(cidr_t));
	direct = (cidr_t *)calloc((size_t)BYPASS_MAX, sizeof(cidr_t));

	if (!cidrs || !direct) {
		free(cidrs);
		free(direct);
		warn(out, "Not enough memory for the route plan.");
		return;
	}

	for (i = 0; i < endpoint_count && ep_count < 16; i++) {
		ip_t ip;

		if (endpoints[i] && ip_parse(endpoints[i], &ip)) {
			eps[ep_count++] = ip;
		}
	}

	for (i = 0; i < rule_count; i++) {
		int before = cidr_count;

		take(out, policy_selected, rules[i], rules[i], eps, ep_count, cidrs, &cidr_count);

		if (before == cidr_count && cidr_count >= BYPASS_MAX) {
			skipped++;
		}
	}

	for (i = 0; i < learned_count; i++) {
		int before = cidr_count;

		take(out, policy_selected, learned[i], learned[i], eps, ep_count, cidrs, &cidr_count);

		if (before == cidr_count && cidr_count >= BYPASS_MAX) {
			skipped++;
		}
	}

	if (exclude_lan) {
		for (i = 0; i < lan_count; i++) {
			char origin[128];

			snprintf(origin, sizeof(origin), "Local network %s", lan[i]);

			if (policy_selected) {
				take(out, policy_selected, lan[i], origin, eps, ep_count, direct, &direct_count);
			} else {
				take(out, policy_selected, lan[i], origin, eps, ep_count, cidrs, &cidr_count);
			}
		}
	}

	if (skipped > 0) {
		char msg[256];

		snprintf(msg, sizeof(msg), "Skipped %d addresses; the route plan is full.", skipped);
		warn(out, msg);
	}

	out->route_count = unique_sort(cidrs, cidr_count, out->routes, BYPASS_MAX);
	out->direct_count = unique_sort(direct, direct_count, out->direct, BYPASS_MAX);
	free(cidrs);
	free(direct);

	if (policy_selected) {
		int w = 0;

		for (i = 0; i < out->route_count; i++) {
			int k;
			int drop = 0;

			for (k = 0; k < out->direct_count; k++) {
				if (strcmp(out->routes[i], out->direct[k]) == 0) {
					drop = 1;
				}
			}

			if (!drop) {
				if (w != i) {
					snprintf(out->routes[w], 64, "%s", out->routes[i]);
				}

				w++;
			}
		}

		out->route_count = w;
	}
}

