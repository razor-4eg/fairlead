//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_dns.c -- Host names, and the DNS query and reply bytes used to resolve a bypass domain.
//
//===============================================================

#include "fl_dns.h"
#include "fl_ip.h"

#include <stdio.h>
#include <string.h>

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
fl_domain_normalize
Lowercase host, punycode where needed. Returns 0 when the name is rejected.
===============
*/
int fl_domain_normalize(const char *raw, char *buf, int cap) {
	char			text[512];
	int				i;
	int				len;
	int				high;
	const char		*p;
	int				labels;
	fl_ip_t			ip;
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

	if (fl_ip_parse(text, &ip)) {
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
fl_dns_query
A query packet. Returns the byte count, or 0.
===============
*/
int fl_dns_query(uint16_t id, const char *name, uint16_t type, int edns, unsigned char *out, int cap) {
	char	norm[512];
	int		used;

	if (!fl_domain_normalize(name, norm, (int)sizeof(norm))) {
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
static int read_section(const unsigned char *d, int len, int *offset, int count, fl_dns_rec_t *recs, int rec_cap, int *rec_count, int *cname) {
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

		if ((type == FL_DNS_A && rdlen == 4) || (type == FL_DNS_AAAA && rdlen == 16)) {
			fl_ip_t ip;

			memset(&ip, 0, sizeof(ip));
			ip.v6 = type == FL_DNS_AAAA;
			memcpy(ip.bytes, d + *offset, rdlen);

			if (*rec_count < rec_cap) {
				fl_ip_format(&ip, recs[*rec_count].address, 64);
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
fl_dns_parse
Parse a reply when the id matches and the QR bit is set.
===============
*/
int fl_dns_parse(const unsigned char *data, int len, uint16_t id, fl_dns_reply_t *out) {
	unsigned int	got;
	unsigned int	flags;
	unsigned int	questions;
	unsigned int	answers;
	unsigned int	authority;
	unsigned int	additional;
	int				offset;
	int				i;
	int				dummy;
	fl_dns_rec_t	junk[8];
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
