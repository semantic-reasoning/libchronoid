/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Compile-time SIMD selection for the UUIDv7 hex encoder. Mirrors the
 * pattern in chronoid/ksuid/base62_simd.h: the scalar fallback is
 * always present, and an SSSE3 kernel is wired in on x86_64 via the
 * isolated static_library('chronoid_hex_ssse3', ..., c_args:['-mssse3'])
 * target in the top-level meson.build.
 *
 * SSSE3 is x86-64-v2, NOT part of the AMD64 psABI v1 baseline (which
 * mandates only SSE2). We compile the kernel with a per-file -mssse3
 * flag and rely on its near-universal availability since 2006 (Intel
 * Core 2 / AMD K10 and later). Targets that want to widen support to
 * pre-Core-2 SSE2-only x86_64 hosts would need a runtime CPUID gate;
 * that work is deferred until/unless the AVX2 dispatcher in commit #8
 * needs to add an SSE2-only fallback rung.
 *
 * Decode stays scalar-only in this commit; only the encode kernel is
 * SIMD-accelerated.
 */
#ifndef CHRONOID_UUIDV7_HEX_SIMD_H
#define CHRONOID_UUIDV7_HEX_SIMD_H

#include <stddef.h>
#include <stdint.h>

/* Always present (defined in hex.c). */
void chronoid_hex_encode_lower_scalar (char out[36], const uint8_t in[16]);

#if defined(CHRONOID_HAVE_HEX_SSSE3)
void chronoid_hex_encode_lower_ssse3 (char out[36], const uint8_t in[16]);
#  define CHRONOID_HEX_ENCODE16_LOWER chronoid_hex_encode_lower_ssse3
#else
#  define CHRONOID_HEX_ENCODE16_LOWER chronoid_hex_encode_lower_scalar
#endif

#endif /* CHRONOID_UUIDV7_HEX_SIMD_H */
