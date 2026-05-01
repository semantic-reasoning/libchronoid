/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Internal (private) hex codec used by chronoid_uuidv7_format /
 * chronoid_uuidv7_parse. Lowercase output, hyphenated 8-4-4-4-12 form
 * per RFC 9562 §4 (the canonical UUID textual representation).
 *
 * No upstream lineage; the codec is a straight scalar walk over the
 * 16-byte / 36-char layout. A SIMD acceleration lands in a later
 * commit and shares this header's contract.
 */
#ifndef CHRONOID_UUIDV7_HEX_H
#define CHRONOID_UUIDV7_HEX_H

#include <stddef.h>
#include <stdint.h>

/* Encode 16 bytes as 36 ASCII chars in canonical UUID form:
 *   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 * lowercase hex digits, hyphens at offsets 8, 13, 18, 23. The output
 * is NOT NUL-terminated; callers needing a C string must size their
 * buffer to 37 bytes and append '\0' themselves. Never fails: every
 * 16-byte input encodes by construction. */
void chronoid_hex_encode_lower (char out[36], const uint8_t in[16]);

/* Decode 36 chars to 16 bytes. Returns 0 on success, -1 on:
 *   - any char at a hex position not in [0-9A-Fa-f]
 *   - any char at a hyphen position (offsets 8, 13, 18, 23) not '-'
 * Case-insensitive: 'A'..'F' and 'a'..'f' produce the same nibble.
 * On failure |out| is left untouched -- the decoder writes to a
 * stack-local 16-byte temporary and only memcpys on full-success.
 * |s| does NOT need to be NUL-terminated; |len| must be exactly 36. */
int chronoid_hex_decode (uint8_t out[16], const char *s, size_t len);

#endif /* CHRONOID_UUIDV7_HEX_H */
