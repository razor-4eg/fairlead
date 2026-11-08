//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_keys.c -- Base64 and WireGuard keys. Generate, check, and convert to the hex form UAPI wants.
//
//===============================================================

#include "fl_keys.h"
#include "fl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
fl_b64_decode
Decode standard or url base64. Returns the byte count, or -1.
===============
*/
int fl_b64_decode(const char *text, unsigned char *out, int cap) {
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
fl_b64_encode
Standard base64 with padding. Returns the character count, or -1.
===============
*/
int fl_b64_encode(const unsigned char *in, int len, char *out, int cap) {
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
fl_key_is
True when text is a 32-byte WireGuard key.
===============
*/
int fl_key_is(const char *text) {
	unsigned char	raw[33];
	int				n;
	char			buf[128];

	if (!text) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", text);
	fl_trim(buf);
	n = fl_b64_decode(buf, raw, 33);
	return n == 32;
}

/*
===============
fl_key_hex
64 lowercase hex digits, or 0.
===============
*/
int fl_key_hex(const char *b64, char *hex, int cap) {
	unsigned char	raw[32];
	char			buf[128];
	int				n;
	int				i;

	if (!hex || cap < 65 || !b64) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", b64);
	fl_trim(buf);
	n = fl_b64_decode(buf, raw, 32);

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
fl_hex_b64
Hex back to base64. An empty hex string writes an empty key.
===============
*/
int fl_hex_b64(const char *hex, char *b64, int cap) {
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

	return fl_b64_encode(raw, n, b64, cap) > 0;
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
	return fl_b64_encode(pub, 32, public_b64, cap) > 0;
}

/*
===============
fl_key_public
Public key for a base64 private key.
===============
*/
int fl_key_public(const char *private_b64, char *public_b64, int cap) {
	unsigned char	raw[32];
	char			buf[128];
	int				n;

	if (!private_b64 || !public_b64) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s", private_b64);
	fl_trim(buf);
	n = fl_b64_decode(buf, raw, 32);

	if (n != 32) {
		return 0;
	}

	return public_from_raw(raw, public_b64, cap);
}

/*
===============
fl_key_generate
A new key pair. Windows uses BCrypt, anything else /dev/urandom.
===============
*/
int fl_key_generate(char *private_b64, int private_cap, char *public_b64, int public_cap) {
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
	if (fl_b64_encode(raw, 32, private_b64, private_cap) <= 0) {
		return 0;
	}

	return public_from_raw(raw, public_b64, public_cap);
}
