//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// qcompress.h -- Qt qCompress framing: a big-endian uncompressed length, then zlib.
// Amnezia vpn:// payloads use this. Windows and the Mac package link the same file.
//
//===============================================================

#ifndef QCOMPRESS_H
#define QCOMPRESS_H

#include <stddef.h>
#include <stdint.h>

size_t	fairlead_qcompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);
size_t	fairlead_quncompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);

#endif
