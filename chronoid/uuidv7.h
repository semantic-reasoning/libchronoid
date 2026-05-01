/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 * UUIDv7 (RFC 9562) implementation. No upstream lineage.
 */
#ifndef CHRONOID_UUIDV7_H
#define CHRONOID_UUIDV7_H

#include <chronoid/chronoid_version.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(CHRONOID_BUILDING)
#    define CHRONOID_PUBLIC __declspec(dllexport)
#  else
#    define CHRONOID_PUBLIC __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define CHRONOID_PUBLIC __attribute__((visibility("default")))
#else
#  define CHRONOID_PUBLIC
#endif

/* --------------------------------------------------------------------------
 * Wire-format constants (RFC 9562 §5.7).
 * -------------------------------------------------------------------------- */

#define CHRONOID_UUIDV7_BYTES        16 /* binary length                          */
#define CHRONOID_UUIDV7_STRING_LEN   36 /* canonical 8-4-4-4-12 string length     */

  typedef struct chronoid_uuidv7
  {
    uint8_t b[CHRONOID_UUIDV7_BYTES];
  } chronoid_uuidv7_t;

/* The static-storage initializer macros below assume chronoid_uuidv7_t is
 * exactly its byte array -- no padding, no extra fields. If a future
 * change adds a field, the assertion fails at compile time and forces
 * the macro author to update CHRONOID_UUIDV7_NIL_INIT / CHRONOID_UUIDV7_MAX_INIT
 * in lockstep. C11 spells the assertion `_Static_assert`; C++ since
 * C++11 spells it `static_assert`. Gate so the public header
 * compiles for both, since this file lives inside extern "C" for
 * C++ consumers. */
#ifdef __cplusplus
    static_assert (sizeof (chronoid_uuidv7_t) == CHRONOID_UUIDV7_BYTES,
      "chronoid_uuidv7_t must be exactly CHRONOID_UUIDV7_BYTES; UUIDV7_*_INIT macros depend on it");
#else
    _Static_assert (sizeof (chronoid_uuidv7_t) == CHRONOID_UUIDV7_BYTES,
      "chronoid_uuidv7_t must be exactly CHRONOID_UUIDV7_BYTES; UUIDV7_*_INIT macros depend on it");
#endif

  typedef enum chronoid_uuidv7_err
  {
    CHRONOID_UUIDV7_OK = 0,
    CHRONOID_UUIDV7_ERR_SIZE = -1,       /* bad binary length                     */
    CHRONOID_UUIDV7_ERR_STR_SIZE = -2,   /* bad string length                     */
    CHRONOID_UUIDV7_ERR_STR_VALUE = -3,  /* string contains non-hex / wrong hyphens */
    /* slot -4 reserved (parallel to CHRONOID_KSUID_ERR_PAYLOAD_SIZE; UUIDv7 has   */
    /* no payload-size error today, but the slot is left unallocated to keep      */
    /* enum positions aligned across formats for future cross-format helpers).    */
    /* RNG (-5) / EXHAUSTED (-6) added in later commits */
    CHRONOID_UUIDV7_ERR_TIME_RANGE = -7  /* unix_ms outside 48-bit range          */
  } chronoid_uuidv7_err_t;

/* Two forms of the same sentinel values:
 *
 *   - CHRONOID_UUIDV7_NIL / CHRONOID_UUIDV7_MAX (extern const chronoid_uuidv7_t)
 *       Use these for runtime comparison and parameter passing:
 *           if (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_NIL) == 0) ...
 *
 *   - CHRONOID_UUIDV7_NIL_INIT / CHRONOID_UUIDV7_MAX_INIT (aggregate-initializer macros)
 *       Use these as constant expressions in a declaration:
 *           static const chronoid_uuidv7_t g_zero = CHRONOID_UUIDV7_NIL_INIT;
 *       The macro form is REQUIRED on Windows DLL builds, where
 *       CHRONOID_PUBLIC expands to __declspec(dllimport) and the
 *       symbol is therefore not a constant expression in user TUs.
 *
 * MAX is the all-0xff sentinel -- it does NOT carry the version=7 /
 * variant=0b10 nibbles. The same convention as CHRONOID_KSUID_MAX:
 * MAX is the lexicographic maximum of the binary representation,
 * not a syntactically valid UUIDv7. The two forms are guaranteed
 * byte-for-byte equal; tests/test_uuidv7_smoke.c pins the equivalence
 * with ASSERT_EQ_BYTES. */
  CHRONOID_PUBLIC extern const chronoid_uuidv7_t CHRONOID_UUIDV7_NIL;
  CHRONOID_PUBLIC extern const chronoid_uuidv7_t CHRONOID_UUIDV7_MAX;

#define CHRONOID_UUIDV7_NIL_INIT { { 0 } }
#define CHRONOID_UUIDV7_MAX_INIT                                            \
  { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,                       \
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } }

/* --------------------------------------------------------------------------
 * Predicates and ordering.
 * -------------------------------------------------------------------------- */

  CHRONOID_PUBLIC bool chronoid_uuidv7_is_nil (const chronoid_uuidv7_t *id);

/* Lexicographic comparison over the full 16-byte representation, matching
 * memcmp semantics. Returns <0, 0, or >0. */
  CHRONOID_PUBLIC int chronoid_uuidv7_compare (const chronoid_uuidv7_t *a,
      const chronoid_uuidv7_t *b);

/* --------------------------------------------------------------------------
 * Construction from raw inputs.
 * -------------------------------------------------------------------------- */

/* Copy the binary UUIDv7 at |b| (which must be exactly CHRONOID_UUIDV7_BYTES
 * long) into |out|. On error |out| is left untouched. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_from_bytes (
      chronoid_uuidv7_t *out, const uint8_t *b, size_t n);

/* Build |out| from a Unix millisecond timestamp, a 12-bit rand_a value,
 * and an 8-byte rand_b buffer, per RFC 9562 §5.7.
 *
 *   bytes 0..5 = unix_ms big-endian (48 bits)
 *   byte    6  = (0x7 << 4) | ((rand_a_12bit >> 8) & 0x0F)
 *   byte    7  =  rand_a_12bit & 0xFF
 *   byte    8  = (0b10 << 6) | (rand_b[0] & 0x3F)
 *   bytes 9..15= rand_b[1..7]
 *
 * |unix_ms| must lie in [0, (1LL << 48) - 1]; out-of-range returns
 * CHRONOID_UUIDV7_ERR_TIME_RANGE and leaves |*out| untouched. The version
 * (0x7) and variant (0b10) nibbles are written by the library
 * unconditionally; bits 12-15 of |rand_a_12bit| and the top two bits
 * of |rand_b[0]| supplied by the caller are masked off. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_from_parts (
      chronoid_uuidv7_t *out, int64_t unix_ms,
      uint16_t rand_a_12bit, const uint8_t rand_b[8]);

/* --------------------------------------------------------------------------
 * Field accessors.
 * -------------------------------------------------------------------------- */

/* The UUIDv7's 48-bit big-endian millisecond timestamp at bytes 0..5,
 * interpreted as Unix milliseconds. The return value fits in 48 bits
 * by construction (no sign extension surprise). */
  CHRONOID_PUBLIC int64_t chronoid_uuidv7_unix_ms (const chronoid_uuidv7_t *id);

/* Version nibble: high 4 bits of byte 6. Reads 0x7 for properly-
 * constructed UUIDv7s. */
  CHRONOID_PUBLIC uint8_t chronoid_uuidv7_version (const chronoid_uuidv7_t *id);

/* Variant: top 2 bits of byte 8. Reads 0b10 (= 2) for properly-
 * constructed UUIDv7s (the RFC 9562 variant). */
  CHRONOID_PUBLIC uint8_t chronoid_uuidv7_variant (const chronoid_uuidv7_t *id);

/* --------------------------------------------------------------------------
 * Hex string conversion (RFC 9562 §4 canonical 8-4-4-4-12 form).
 * -------------------------------------------------------------------------- */

/* Decode |len| characters of |s| (which must be exactly
 * CHRONOID_UUIDV7_STRING_LEN, i.e. 36 chars in canonical
 * xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx form, no NUL terminator
 * required) into |out|. Returns CHRONOID_UUIDV7_ERR_STR_SIZE if |len|
 * is wrong, or CHRONOID_UUIDV7_ERR_STR_VALUE if the input contains a
 * non-hex digit at a hex position, a non-'-' byte at a hyphen position
 * (offsets 8, 13, 18, 23), or any whitespace. Parsing is case-
 * insensitive: 'A'..'F' and 'a'..'f' produce the same nibble. On any
 * error the contents of |*out| are guaranteed unchanged -- decoding
 * writes to a stack temporary first and only copies into |out| once
 * the input has been fully validated. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_parse (
      chronoid_uuidv7_t *out, const char *s, size_t len);

/* Write the 36-character canonical hyphenated lowercase representation
 * of |id| into |out|. The output is NOT NUL-terminated; callers needing
 * a C string should size their buffer to CHRONOID_UUIDV7_STRING_LEN + 1
 * and append '\0' themselves. No error path: every 16-byte UUIDv7
 * encodes by construction. */
  CHRONOID_PUBLIC void chronoid_uuidv7_format (
      const chronoid_uuidv7_t *id, char out[CHRONOID_UUIDV7_STRING_LEN]);

#ifdef __cplusplus
}                               /* extern "C" */
#endif

#endif                          /* CHRONOID_UUIDV7_H */
