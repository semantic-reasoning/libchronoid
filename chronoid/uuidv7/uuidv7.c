/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 * UUIDv7 (RFC 9562) implementation. No upstream lineage.
 *
 * Core type, constants, accessors, and ordering primitives. Format/parse,
 * monotonic sequence, and SIMD hex are split into later commits.
 */
#include <chronoid/uuidv7.h>

#include <string.h>

#include <chronoid/rand.h>
#include <chronoid/uuidv7/hex.h>

/* Drive both definitions from the public *_INIT macros so the runtime
 * symbols and the static-storage initializer form can never drift out
 * of byte-for-byte agreement -- mirrors the convention used in
 * chronoid/ksuid/ksuid.c. The smoke test pins the equivalence at
 * runtime, but a single source of truth at the definition site
 * removes the possibility entirely. */
CHRONOID_PUBLIC const chronoid_uuidv7_t CHRONOID_UUIDV7_NIL =
    CHRONOID_UUIDV7_NIL_INIT;
CHRONOID_PUBLIC const chronoid_uuidv7_t CHRONOID_UUIDV7_MAX =
    CHRONOID_UUIDV7_MAX_INIT;

/* Inclusive upper bound on the 48-bit Unix-ms timestamp encodable in
 * bytes 0..5 of a UUIDv7. Anything outside [0, UUIDV7_MAX_UNIX_MS] is
 * rejected by chronoid_uuidv7_from_parts with CHRONOID_UUIDV7_ERR_TIME_RANGE. */
#define UUIDV7_MAX_UNIX_MS ((int64_t) ((1LL << 48) - 1))

bool
chronoid_uuidv7_is_nil (const chronoid_uuidv7_t *id)
{
  static const uint8_t zero[CHRONOID_UUIDV7_BYTES] = { 0 };
  return memcmp (id->b, zero, CHRONOID_UUIDV7_BYTES) == 0;
}

int
chronoid_uuidv7_compare (const chronoid_uuidv7_t *a, const chronoid_uuidv7_t *b)
{
  /* Lexicographic over the 16-byte representation. A SIMD kernel may
   * land in a later commit; for now memcmp is the contract. */
  return memcmp (a->b, b->b, CHRONOID_UUIDV7_BYTES);
}

chronoid_uuidv7_err_t
chronoid_uuidv7_from_bytes (chronoid_uuidv7_t *out, const uint8_t *b, size_t n)
{
  if (n != CHRONOID_UUIDV7_BYTES)
    return CHRONOID_UUIDV7_ERR_SIZE;
  memcpy (out->b, b, CHRONOID_UUIDV7_BYTES);
  return CHRONOID_UUIDV7_OK;
}

chronoid_uuidv7_err_t
chronoid_uuidv7_from_parts (chronoid_uuidv7_t *out,
    int64_t unix_ms, uint16_t rand_a_12bit, const uint8_t rand_b[8])
{
  if (unix_ms < 0 || unix_ms > UUIDV7_MAX_UNIX_MS)
    return CHRONOID_UUIDV7_ERR_TIME_RANGE;

  /* Build into a stack temporary so a successful return is the only
   * path that mutates |*out|. Mirrors chronoid_ksuid_from_parts'
   * "untouched on failure" guarantee even though the only failure
   * mode (time range) is checked up front -- keeps the pattern
   * uniform for future error cases that interleave with byte
   * writes. */
  chronoid_uuidv7_t tmp;
  uint64_t ms = (uint64_t) unix_ms;

  /* Bytes 0..5: 48-bit big-endian timestamp. Byte-by-byte writes
   * (no host-endian uint64_t cast) so the same code is correct on
   * big- and little-endian hosts and on strict-alignment targets. */
  tmp.b[0] = (uint8_t) (ms >> 40);
  tmp.b[1] = (uint8_t) (ms >> 32);
  tmp.b[2] = (uint8_t) (ms >> 24);
  tmp.b[3] = (uint8_t) (ms >> 16);
  tmp.b[4] = (uint8_t) (ms >> 8);
  tmp.b[5] = (uint8_t) (ms);

  /* Mask the caller's rand_a to 12 bits before splitting: bits 12-15
   * are not part of the rand_a field, and the version nibble is
   * library-controlled regardless of what the caller passes. */
  uint16_t rand_a = rand_a_12bit & 0x0FFF;
  tmp.b[6] = (uint8_t) (0x70 | ((rand_a >> 8) & 0x0F));
  tmp.b[7] = (uint8_t) (rand_a & 0xFF);

  /* Byte 8: top 2 bits = 0b10 variant, bottom 6 bits from rand_b[0]. */
  tmp.b[8] = (uint8_t) (0x80 | (rand_b[0] & 0x3F));

  /* Bytes 9..15: remaining seven bytes of rand_b verbatim. */
  memcpy (tmp.b + 9, rand_b + 1, 7);

  memcpy (out->b, tmp.b, CHRONOID_UUIDV7_BYTES);
  return CHRONOID_UUIDV7_OK;
}

int64_t
chronoid_uuidv7_unix_ms (const chronoid_uuidv7_t *id)
{
  /* Read bytes 0..5 as a 48-bit big-endian unsigned integer. The
   * top two bytes of the returned int64_t are 0 by construction, so
   * the value is non-negative and fits in 48 bits. */
  uint64_t ms = ((uint64_t) id->b[0] << 40)
      | ((uint64_t) id->b[1] << 32)
      | ((uint64_t) id->b[2] << 24)
      | ((uint64_t) id->b[3] << 16)
      | ((uint64_t) id->b[4] << 8)
      | ((uint64_t) id->b[5]);
  return (int64_t) ms;
}

uint8_t
chronoid_uuidv7_version (const chronoid_uuidv7_t *id)
{
  return (uint8_t) ((id->b[6] >> 4) & 0x0F);
}

uint8_t
chronoid_uuidv7_variant (const chronoid_uuidv7_t *id)
{
  return (uint8_t) ((id->b[8] >> 6) & 0x03);
}

void
chronoid_uuidv7_format (const chronoid_uuidv7_t *id,
    char out[CHRONOID_UUIDV7_STRING_LEN])
{
  /* The hex codec is the wire-format authority -- this wrapper is
   * just a type-safe entry point that fans the public chronoid_uuidv7_t
   * pointer into the raw 16-byte view the codec expects. No NUL
   * terminator: caller's buffer is exactly 36 bytes. */
  chronoid_hex_encode_lower (out, id->b);
}

chronoid_uuidv7_err_t
chronoid_uuidv7_parse (chronoid_uuidv7_t *out, const char *s, size_t len)
{
  /* Length is the cheap upfront check; do it first so the wrong-length
   * branch never touches the codec. The codec also length-checks
   * internally, but distinguishing STR_SIZE from STR_VALUE at the
   * public-API surface requires the split. */
  if (len != CHRONOID_UUIDV7_STRING_LEN)
    return CHRONOID_UUIDV7_ERR_STR_SIZE;

  /* The codec writes to a stack-local temp on its own (see hex.c)
   * and only commits via memcpy on full-success, so |*out| is
   * guaranteed untouched on the failure path. */
  uint8_t tmp[CHRONOID_UUIDV7_BYTES];
  if (chronoid_hex_decode (tmp, s, len) != 0)
    return CHRONOID_UUIDV7_ERR_STR_VALUE;

  memcpy (out->b, tmp, CHRONOID_UUIDV7_BYTES);
  return CHRONOID_UUIDV7_OK;
}

/* --------------------------------------------------------------------------
 * Generation: chronoid_uuidv7_new / chronoid_uuidv7_new_with_time.
 *
 * Each call is independent; no inter-call monotonicity guarantee. For
 * monotonic runs use chronoid_uuidv7_sequence_t. The 12-bit rand_a
 * field is filled with two random bytes (high 4 bits masked off by
 * chronoid_uuidv7_from_parts), the 8-byte rand_b tail is filled
 * directly. All ten random bytes come from one chronoid_internal_fill_random
 * draw so a partial RNG failure cannot produce a half-random ID.
 * -------------------------------------------------------------------------- */

chronoid_uuidv7_err_t
chronoid_uuidv7_new_with_time (chronoid_uuidv7_t *out, int64_t unix_ms)
{
  if (unix_ms < 0 || unix_ms > UUIDV7_MAX_UNIX_MS)
    return CHRONOID_UUIDV7_ERR_TIME_RANGE;

  /* Single contiguous draw: 2 bytes for rand_a (12 bits used) + 8
   * bytes for rand_b. Fail fast and leave |*out| untouched if the
   * RNG declines. */
  uint8_t rnd[10];
  if (chronoid_internal_fill_random (rnd, sizeof rnd) != 0)
    return CHRONOID_UUIDV7_ERR_RNG;

  uint16_t rand_a_12bit = (uint16_t) (((uint16_t) rnd[0] << 8) | rnd[1]);
  return chronoid_uuidv7_from_parts (out, unix_ms, rand_a_12bit, rnd + 2);
}

chronoid_uuidv7_err_t
chronoid_uuidv7_new (chronoid_uuidv7_t *out)
{
  int64_t now = chronoid_now_ms ();
  if (now < 0 || now > UUIDV7_MAX_UNIX_MS)
    return CHRONOID_UUIDV7_ERR_TIME_RANGE;
  return chronoid_uuidv7_new_with_time (out, now);
}
