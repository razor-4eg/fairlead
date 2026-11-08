//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// qcompress.c -- Qt qCompress and qUncompress. Four byte big-endian size, then zlib.
//
//===============================================================

#include "qcompress.h"

#include <zlib.h>

/*
===============
fairlead_qcompress
Write the framed stream into dst. Returns the byte count, or 0.
===============
*/
size_t fairlead_qcompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap) {
	uLongf	bound;
	uLongf	produced;
	int		rc;

	if (src == NULL || dst == NULL || src_len == 0 || src_len > 8000000) {
		return 0;
	}

	bound = compressBound((uLong)src_len);

	if (dst_cap < (size_t)bound + 4) {
		return 0;
	}

	dst[0] = (uint8_t)((src_len >> 24) & 0xff);
	dst[1] = (uint8_t)((src_len >> 16) & 0xff);
	dst[2] = (uint8_t)((src_len >> 8) & 0xff);
	dst[3] = (uint8_t)(src_len & 0xff);
	produced = (uLongf)(dst_cap - 4);
	rc = compress2(dst + 4, &produced, src, (uLong)src_len, Z_DEFAULT_COMPRESSION);

	if (rc != Z_OK) {
		return 0;
	}

	return (size_t)produced + 4;
}

/*
===============
fairlead_quncompress
Inflate a framed stream. The uncompressed size must match the header.
===============
*/
size_t fairlead_quncompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap) {
	size_t	expected;
	uLongf	produced;
	int		rc;

	if (src == NULL || dst == NULL || src_len < 6 || dst_cap == 0) {
		return 0;
	}

	expected = ((size_t)src[0] << 24) | ((size_t)src[1] << 16) | ((size_t)src[2] << 8) | (size_t)src[3];

	if (expected == 0 || expected > 8000000 || expected > dst_cap) {
		return 0;
	}

	produced = (uLongf)dst_cap;
	rc = uncompress(dst, &produced, src + 4, (uLong)(src_len - 4));

	if (rc != Z_OK || (size_t)produced != expected) {
		return 0;
	}

	return (size_t)produced;
}
