//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_keys.h -- Base64 and Curve25519 keys in the form WireGuard configs use.
//
//===============================================================

#ifndef FL_KEYS_H
#define FL_KEYS_H

int		fl_b64_decode(const char *text, unsigned char *out, int cap);
int		fl_b64_encode(const unsigned char *in, int len, char *out, int cap);
int		fl_key_is(const char *text);
/* 64 hex chars, no NUL counted in the 64. Returns 0 on failure. */
int		fl_key_hex(const char *b64, char *hex, int cap);
int		fl_hex_b64(const char *hex, char *b64, int cap);
int		fl_key_public(const char *private_b64, char *public_b64, int cap);
int		fl_key_generate(char *private_b64, int private_cap, char *public_b64, int public_cap);

#endif
