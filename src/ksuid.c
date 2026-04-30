/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * Core KSUID type, constants, and ordering primitives.
 *
 * Derived from segmentio/ksuid (MIT, Copyright (c) 2017 Segment.io):
 *   - 20-byte layout, KSUID_NIL / KSUID_MAX semantics: ksuid.go:15-58
 *   - Compare = bytes.Compare(a, b):                   ksuid.go:308-311
 */
#include <ksuid.h>

#include <string.h>

KSUID_PUBLIC const ksuid_t KSUID_NIL = { .b = {0} };
KSUID_PUBLIC const ksuid_t KSUID_MAX = {
    .b = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    },
};

bool ksuid_is_nil(const ksuid_t *id) {
    static const uint8_t zero[KSUID_BYTES] = {0};
    return memcmp(id->b, zero, KSUID_BYTES) == 0;
}

int ksuid_compare(const ksuid_t *a, const ksuid_t *b) {
    /* memcmp returns the byte difference on glibc but the spec only
     * guarantees the sign; normalize to {-1, 0, +1} for portability. */
    int r = memcmp(a->b, b->b, KSUID_BYTES);
    return (r > 0) - (r < 0);
}
