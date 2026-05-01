/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * Internal (private) base62 encode/decode used by chronoid_ksuid_parse/chronoid_ksuid_format.
 * Derived from segmentio/ksuid (MIT, Copyright (c) 2017 Segment.io)
 *   - base62.go:39-82 (fastEncodeBase62)
 *   - base62.go:102-175 (fastDecodeBase62)
 */
#ifndef KSUID_BASE62_H
#define KSUID_BASE62_H

#include <stdint.h>
#include <stddef.h>

#include <chronoid/ksuid.h>

/* Encode |in| (exactly CHRONOID_KSUID_BYTES = 20 bytes) as |out| (exactly
 * CHRONOID_KSUID_STRING_LEN = 27 base62 characters). Output is left-padded with
 * '0' if the value is shorter than 27 base62 digits. Never fails. */
void chronoid_base62_encode (uint8_t out[CHRONOID_KSUID_STRING_LEN],
    const uint8_t in[CHRONOID_KSUID_BYTES]);

/* Decode |in| (exactly CHRONOID_KSUID_STRING_LEN base62 characters) into |out|
 * (exactly CHRONOID_KSUID_BYTES bytes). Returns CHRONOID_KSUID_ERR_STR_VALUE if any input
 * character is outside the base62 alphabet, or if the decoded value
 * overflows 2^160 (i.e. the input string is lexicographically greater
 * than CHRONOID_KSUID_MAX's encoding "aWgEPTl1tmebfsQzFP4bxwgy80V"). On error the
 * contents of |out| are undefined. */
chronoid_ksuid_err_t chronoid_base62_decode (uint8_t out[CHRONOID_KSUID_BYTES],
    const uint8_t in[CHRONOID_KSUID_STRING_LEN]);

#endif /* KSUID_BASE62_H */
