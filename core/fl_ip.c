//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_ip.c -- IP addresses, CIDR blocks, endpoints, and the route and ifconfig text the kernel matches.
//
//===============================================================

#include "fl_ip.h"
#include "fl.h"

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
fl_ip_parse
Parse a numeric address. Returns 0 when text is not one.
===============
*/
int fl_ip_parse(const char *text, fl_ip_t *out) {
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
	fl_trim(buf);

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
fl_ip_format
Write the numeric form. Empty when ip is missing.
===============
*/
void fl_ip_format(const fl_ip_t *ip, char *buf, int cap) {
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
fl_ip_equal
Same family and the same bytes.
===============
*/
int fl_ip_equal(const fl_ip_t *a, const fl_ip_t *b) {
	int		n;

	if (!a || !b || a->v6 != b->v6) {
		return 0;
	}

	n = a->v6 ? 16 : 4;
	return memcmp(a->bytes, b->bytes, (size_t)n) == 0;
}

/*
===============
fl_ip_unspecified
0.0.0.0 or ::.
===============
*/
int fl_ip_unspecified(const fl_ip_t *ip) {
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
fl_ip_loopback
127.0.0.0/8 or ::1.
===============
*/
int fl_ip_loopback(const fl_ip_t *ip) {
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
fl_ip_multicast
224.0.0.0/4 or ff00::/8.
===============
*/
int fl_ip_multicast(const fl_ip_t *ip) {
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
fl_ip_link_local
169.254.0.0/16 or fe80::/10.
===============
*/
int fl_ip_link_local(const fl_ip_t *ip) {
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
fl_ip_broadcast
255.255.255.255.
===============
*/
int fl_ip_broadcast(const fl_ip_t *ip) {
	if (!ip || ip->v6) {
		return 0;
	}

	return ip->bytes[0] == 255 && ip->bytes[1] == 255 && ip->bytes[2] == 255 && ip->bytes[3] == 255;
}

/*
===============
fl_ip_routable
An address the tunnel is allowed to own.
===============
*/
int fl_ip_routable(const fl_ip_t *ip) {
	return ip && !fl_ip_unspecified(ip) && !fl_ip_loopback(ip) && !fl_ip_multicast(ip)
		&& !fl_ip_link_local(ip) && !fl_ip_broadcast(ip);
}

/*
===============
fl_ip_mask
Zero the host bits past prefix.
===============
*/
void fl_ip_mask(fl_ip_t *ip, int prefix) {
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
fl_cidr_parse
Parse addr/prefix. The stored address is masked.
===============
*/
int fl_cidr_parse(const char *text, fl_cidr_t *out) {
	char	buf[160];
	char	*slash;
	fl_ip_t	ip;
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
	fl_trim(buf);

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

	if (!fl_ip_parse(buf, &ip)) {
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

	fl_ip_mask(&ip, prefix);
	out->ip = ip;
	out->prefix = prefix;
	return 1;
}

/*
===============
fl_cidr_format
Write addr/prefix.
===============
*/
void fl_cidr_format(const fl_cidr_t *cidr, char *buf, int cap) {
	char	ip[64];

	if (!buf || cap <= 0) {
		return;
	}

	buf[0] = '\0';

	if (!cidr) {
		return;
	}

	fl_ip_format(&cidr->ip, ip, (int)sizeof(ip));
	snprintf(buf, (size_t)cap, "%s/%d", ip, cidr->prefix);
}

/*
===============
fl_cidr_contains
True when ip is inside cidr.
===============
*/
int fl_cidr_contains(const fl_cidr_t *cidr, const fl_ip_t *ip) {
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
fl_cidr_max_prefix
32 or 128 for this network.
===============
*/
int fl_cidr_max_prefix(const fl_cidr_t *cidr) {
	return cidr && cidr->ip.v6 ? 128 : 32;
}

/*
===============
fl_endpoint_host
Host from host:port or [ipv6]:port. (none) and empty fail.
===============
*/
int fl_endpoint_host(const char *endpoint, char *buf, int cap) {
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
	fl_trim(tmp);

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
fl_endpoint_port
Port from an endpoint, or 0.
===============
*/
int fl_endpoint_port(const char *endpoint, int *port) {
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
fl_endpoint_valid
Host and a port in 1..65535.
===============
*/
int fl_endpoint_valid(const char *endpoint) {
	char	host[256];
	int		port;

	if (!fl_endpoint_host(endpoint, host, (int)sizeof(host))) {
		return 0;
	}

	if (!fl_endpoint_port(endpoint, &port)) {
		return 0;
	}

	return port >= 1 && port <= 65535;
}

/*
===============
fl_socket_endpoint
ip:port, using the port from the original endpoint.
===============
*/
int fl_socket_endpoint(const char *ip, const char *original, char *buf, int cap) {
	int		port;

	if (!ip || !buf || cap <= 0) {
		return 0;
	}

	if (!fl_endpoint_port(original, &port) || port < 1) {
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
fl_route_snapshot
Read one route line from netstat-style text.
===============
*/
void fl_route_snapshot(const char *text, fl_route_snap_t *out) {
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

		fl_trim(line);

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
fl_route_exact
True when the snapshot is this network and not a wider route.
===============
*/
int fl_route_exact(const fl_cidr_t *cidr, const fl_route_snap_t *snap) {
	fl_ip_t		dest;
	int			prefix;
	fl_ip_t		masked;
	const char	*p;

	if (!cidr || !snap || !snap->has_destination) {
		return 0;
	}

	if (strcmp(snap->destination, "default") == 0) {
		return 0;
	}

	if (!fl_ip_parse(snap->destination, &dest) || dest.v6 != cidr->ip.v6) {
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

				if (*p != '\0' || prefix < 0 || prefix > fl_cidr_max_prefix(cidr)) {
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
		prefix = fl_cidr_max_prefix(cidr);
	}

	masked = dest;
	fl_ip_mask(&masked, prefix);
	return prefix == cidr->prefix && fl_ip_equal(&masked, &cidr->ip);
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
fl_ifconfig_networks
On-link networks from one ifconfig listing.
===============
*/
int fl_ifconfig_networks(const char *text, char out[][64], int cap) {
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
		fl_trim(line);
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
			fl_ip_t ip;
			mi = -1;
			fl_cidr_t cidr;
			char formatted[64];

			if (fl_ip_parse(parts[1], &ip)) {
				for (k = 0; k < pc - 1; k++) {
					if (strcmp(parts[k], "netmask") == 0) {
						mi = k + 1;
					}
				}

				prefix = mi >= 0 ? prefix_hex(parts[mi]) : -1;

				if (prefix >= 0) {
					fl_ip_mask(&ip, prefix);
					cidr.ip = ip;
					cidr.prefix = prefix;

					if (prefix > 0 && prefix < 32) {
						fl_cidr_format(&cidr, formatted, (int)sizeof(formatted));
						count = push_unique(out, count, cap, formatted);
					}
				}
			}
		} else if (pc >= 4 && strcmp(parts[0], "inet6") == 0) {
			char addr[128];
			fl_ip_t ip;
			pi = -1;
			prefix = -1;

			snprintf(addr, sizeof(addr), "%s", parts[1]);
			pct = strchr(addr, '%');

			if (pct) {
				*pct = '\0';
			}

			if (fl_ip_parse(addr, &ip) && !fl_ip_link_local(&ip) && !fl_ip_loopback(&ip)) {
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
					fl_cidr_t cidr;
					char formatted[64];

					fl_ip_mask(&ip, prefix);
					cidr.ip = ip;
					cidr.prefix = prefix;
					fl_cidr_format(&cidr, formatted, (int)sizeof(formatted));
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
