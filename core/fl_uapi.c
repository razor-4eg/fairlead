//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_uapi.c -- WireGuard userspace API. Build a set message, read a get reply, and parse a wg dump.
//
//===============================================================

#include "fl_uapi.h"
#include "fl_ip.h"
#include "fl_keys.h"
#include "fl_amnezia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
fl_uapi_set
The set=1 message for this config. endpoints_by_section[section] overrides Endpoint.
===============
*/
int fl_uapi_set(const fl_doc_t *doc, const char **endpoints_by_section, char *out, int cap) {
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
	iface = fl_doc_interface(doc);

	if (iface < 0) {
		return 0;
	}

	fl_doc_value(doc, iface, "PrivateKey", private_key, (int)sizeof(private_key));

	if (!fl_key_hex(private_key, hex, (int)sizeof(hex))) {
		return 0;
	}

	snprintf(line, sizeof(line), "set=1\nprivate_key=%s\n", hex);
	used = append(out, cap, used, line);

	if (used < 0) {
		return 0;
	}

	fl_doc_value(doc, iface, "ListenPort", portbuf, (int)sizeof(portbuf));
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

	if (!fl_amnezia_uapi(doc, awg, (int)sizeof(awg))) {
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

	peer_count = fl_doc_peers(doc, peers, 16);

	for (i = 0; i < peer_count; i++) {
		peer = peers[i];
		char pub[256];
		char psk[256];
		char keep[64];
		char allowed[1024];

		char items[16][256];
		fl_doc_value(doc, peer, "PublicKey", pub, (int)sizeof(pub));

		if (!fl_key_hex(pub, hex, (int)sizeof(hex))) {
			return 0;
		}

		snprintf(line, sizeof(line), "public_key=%s\n", hex);
		used = append(out, cap, used, line);

		if (used < 0) {
			return 0;
		}

		fl_doc_value(doc, peer, "PresharedKey", psk, (int)sizeof(psk));

		if (psk[0]) {
			if (!fl_key_hex(psk, hex, (int)sizeof(hex))) {
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

		fl_doc_value(doc, peer, "PersistentKeepalive", keep, (int)sizeof(keep));
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
			} else if (fl_amnezia_is(doc) && fl_amnezia_uint_range(keep, 65535)) {
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

		fl_doc_peer_allowed(doc, peer, allowed, (int)sizeof(allowed));
		nitems = fl_split_commas(allowed, items, 16);

		for (k = 0; k < nitems; k++) {
			fl_cidr_t cidr;
			char formatted[64];

			if (!fl_cidr_parse(items[k], &cidr)) {
				return 0;
			}

			fl_cidr_format(&cidr, formatted, (int)sizeof(formatted));
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
fl_uapi_parse_get
Peer status from a get=1 reply.
===============
*/
void fl_uapi_parse_get(const char *text, const char *iface, fl_device_status_t *out) {
	const char			*p;
	char				device_key[128];
	char				pending[128];
	int					have_pending;
	int					in_peer;
	fl_peer_status_t	peer;
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
				if (in_peer && out->peer_count < FL_PEER_MAX) {
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
					fl_hex_b64(pending, device_key, (int)sizeof(device_key));
					snprintf(pending, sizeof(pending), "%s", value);
					have_pending = 1;
				} else {
					snprintf(pending, sizeof(pending), "%s", value);
					have_pending = 1;
				}
			} else if (strcmp(key, "preshared_key") == 0) {
				memset(&peer, 0, sizeof(peer));
				fl_hex_b64(have_pending ? pending : "", peer.public_key, (int)sizeof(peer.public_key));
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

	if (in_peer && out->peer_count < FL_PEER_MAX) {
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
		fl_hex_b64(pending, device_key, (int)sizeof(device_key));
	}

	snprintf(out->public_key, sizeof(out->public_key), "%s", device_key);
}

/*
===============
fl_dump_parse
Devices from tab-separated wg dump lines.
===============
*/
int fl_dump_parse(const char *text, fl_device_status_t *out, int cap) {
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
				fl_peer_status_t peer;

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

				if (dev >= 0 && out[dev].peer_count < FL_PEER_MAX) {
					out[dev].peers[out[dev].peer_count++] = peer;
				}
			} else if (fc >= 5) {
				fl_peer_status_t saved[FL_PEER_MAX];
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
