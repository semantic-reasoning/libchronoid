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
    CHRONOID_UUIDV7_ERR_RNG = -5,        /* OS random source unavailable          */
    /* slot -6 intentionally not defined: RFC 9562 §6.2 method 1 mandates that    */
    /* counter overflow bumps the timestamp instead of returning an error, so a   */
    /* monotonic UUIDv7 sequence has no "exhausted" state. The slot stays         */
    /* unallocated to leave room for a future cross-format _ERR_EXHAUSTED         */
    /* numeric parity with CHRONOID_KSUID_ERR_EXHAUSTED.                          */
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

/* Bulk variant of chronoid_uuidv7_format. Writes |n| UUIDv7s into
 * |out_36n|, which must be sized to at least n * CHRONOID_UUIDV7_STRING_LEN
 * bytes -- 36 bytes per UUIDv7, no NUL terminator anywhere in the buffer.
 * The UUID at index i lands at out_36n[i * 36 .. (i+1) * 36 - 1].
 *
 * Dispatch is resolved lazily on the first call (atomic, thread-safe)
 * and the resolved pointer is reused for the lifetime of the process.
 * On x86_64 hosts that advertise AVX2 the dispatcher resolves to a
 * 4-wide AVX2 kernel (4 UUIDs per outer iteration); on other hosts
 * (including non-AVX2 x86_64) the dispatcher resolves to a per-UUID
 * scalar loop equivalent to chronoid_uuidv7_format() N times. Output
 * is byte-identical across kernels.
 *
 * The CHRONOID_FORCE_SCALAR environment variable (shared with
 * chronoid_ksuid_string_batch) pins the dispatcher to the scalar
 * path at first dispatch.
 *
 * No error path: every 16-byte UUIDv7 encodes by construction. n == 0
 * is a no-op. Thread-safe for concurrent invocations on disjoint
 * output buffers; callers must not race two threads on the same
 * |out_36n| slice. */
  CHRONOID_PUBLIC void chronoid_uuidv7_string_batch (
      const chronoid_uuidv7_t *ids, char *out_36n, size_t n);

/* --------------------------------------------------------------------------
 * Generation.
 *
 * Random bytes come from the per-thread ChaCha20 CSPRNG keyed from
 * the OS entropy source (getrandom on Linux, getentropy on macOS,
 * BCryptGenRandom on Windows). The same chronoid_set_rand override
 * registered for KSUID generation also routes UUIDv7 random draws --
 * one global CSPRNG hookup serves both formats. On entropy-source
 * failure the function returns CHRONOID_UUIDV7_ERR_RNG and leaves
 * |*out| untouched.
 * -------------------------------------------------------------------------- */

/* Generate a new UUIDv7 stamped with the current wall-clock time. Each
 * call is independent -- no cross-call monotonicity. For monotonic
 * runs use chronoid_uuidv7_sequence_t. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_new (
      chronoid_uuidv7_t *out);

/* Generate a new UUIDv7 stamped with |unix_ms|. The timestamp must lie
 * in [0, (1LL << 48) - 1] just like chronoid_uuidv7_from_parts;
 * out-of-range returns CHRONOID_UUIDV7_ERR_TIME_RANGE. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_new_with_time (
      chronoid_uuidv7_t *out, int64_t unix_ms);

/* --------------------------------------------------------------------------
 * Monotonic sequence -- RFC 9562 §6.2 method 1 (12-bit sub-ms counter).
 *
 * Within a single millisecond, the 12-bit counter (rand_a field) acts
 * as a monotonic sub-ms tiebreaker; the 62 bits of rand_b are
 * redrawn on every real ms-tick. On counter overflow within a single
 * ms the sequence bumps the embedded timestamp by 1 ms and reseeds
 * the counter, per RFC option (a) -- it NEVER returns an
 * "exhausted" error and never stalls. If the system clock moves
 * backwards (NTP step, VM resume) the sequence clamps to its last
 * emitted ms so monotonicity is preserved.
 *
 * NOT thread-safe: one chronoid_uuidv7_sequence_t instance per thread.
 * Concurrent calls from multiple threads on the same instance are
 * undefined behaviour; multiple threads should each own their own.
 * -------------------------------------------------------------------------- */

  typedef struct chronoid_uuidv7_sequence
  {
    int64_t last_ms;            /* most recent ms emitted, or 0 if uninitialised */
    uint16_t counter;           /* 12-bit counter, masked to 0x0FFF              */
    uint8_t rand_b[8];          /* 62-bit random tail (top 2 bits of rand_b[0]   */
                                /* are overwritten with the variant on emit)    */
    /* No version / opaque-padding field in this revision; new fields
     * may be appended in the future. Treat the struct as opaque from
     * the consumer's perspective and use chronoid_uuidv7_sequence_init
     * to set it up. */
  } chronoid_uuidv7_sequence_t;

/* Initialise |s| with a fresh CSPRNG-drawn 12-bit counter and rand_b.
 * Returns CHRONOID_UUIDV7_ERR_RNG on entropy-source failure; on failure
 * |*s| is left untouched. The first chronoid_uuidv7_sequence_next call
 * after init reads the wall clock and emits at the current ms; the
 * counter is initialised to a random non-zero (with overwhelming
 * probability) starting value rather than 0 so consecutive sequences
 * sharing the same start ms are not predictable from each other's
 * tails. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_sequence_init (
      chronoid_uuidv7_sequence_t *s);

/* Emit the next monotonic UUIDv7 from |s| into |*out|. Always returns
 * either CHRONOID_UUIDV7_OK on success or CHRONOID_UUIDV7_ERR_RNG on
 * entropy-source failure (during a redraw on real ms-tick or counter
 * overflow). Never returns "exhausted": counter overflow is handled
 * internally per RFC 9562 §6.2 method 1 option (a) by bumping the
 * embedded timestamp by 1 ms.
 *
 * Clock-backward clamp: if the wall clock is observed to move
 * backwards (NTP step, VM resume), |s|'s last emitted ms is reused
 * so the resulting UUID still strictly succeeds the previous one.
 * The sequence will eventually re-track real wall-clock time once
 * the system clock catches up. */
  CHRONOID_PUBLIC chronoid_uuidv7_err_t chronoid_uuidv7_sequence_next (
      chronoid_uuidv7_sequence_t *s, chronoid_uuidv7_t *out);

/* Compute the lexicographic lower and upper bounds of the UUIDv7 that
 * the next chronoid_uuidv7_sequence_next call from |s| could produce
 * given its current |last_ms| and |counter| state. The bounds assume
 * the next call lands in the same ms (no real ms-tick observed): the
 * counter increments deterministically by 1, and rand_b spans its
 * full 62-bit range. After a real ms-tick or counter overflow the
 * actual emit may land outside [min, max] (later ms => greater
 * lexicographically), but every call within the same observed ms
 * stays inside the bounds.
 *
 * No error path: |*min_out| and |*max_out| are unconditionally written
 * from |s|. Both must be non-NULL. */
  CHRONOID_PUBLIC void chronoid_uuidv7_sequence_bounds (
      const chronoid_uuidv7_sequence_t *s,
      chronoid_uuidv7_t *min_out, chronoid_uuidv7_t *max_out);

#ifdef __cplusplus
}                               /* extern "C" */
#endif

#endif                          /* CHRONOID_UUIDV7_H */
