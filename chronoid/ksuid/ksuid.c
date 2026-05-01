/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * Core KSUID type, constants, accessors, and ordering primitives.
 *
 * Derived from segmentio/ksuid (MIT, Copyright (c) 2017 Segment.io):
 *   - 20-byte layout, CHRONOID_KSUID_NIL / CHRONOID_KSUID_MAX semantics: ksuid.go:15-58
 *   - Compare = bytes.Compare(a, b):                   ksuid.go:308-311
 *   - FromBytes / FromParts / Timestamp / Payload:     ksuid.go:74-81, 247-294
 */
#include <chronoid/ksuid.h>

#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <chronoid/ksuid/base62.h>
#include <chronoid/byteorder.h>
#include <chronoid/ksuid/compare_simd.h>
#include <chronoid/rand.h>

/* Drive both definitions from the public KSUID_*_INIT macros so the
 * runtime symbols and the static-storage initializer form can never
 * drift out of byte-for-byte agreement -- the regression
 * test_init_macros_match_symbols pins the same equivalence at runtime
 * but a single source of truth at the definition site removes the
 * possibility entirely. */
CHRONOID_PUBLIC const chronoid_ksuid_t CHRONOID_KSUID_NIL = CHRONOID_KSUID_NIL_INIT;
CHRONOID_PUBLIC const chronoid_ksuid_t CHRONOID_KSUID_MAX = CHRONOID_KSUID_MAX_INIT;

bool
chronoid_ksuid_is_nil (const chronoid_ksuid_t *id)
{
  static const uint8_t zero[CHRONOID_KSUID_BYTES] = { 0 };
  return memcmp (id->b, zero, CHRONOID_KSUID_BYTES) == 0;
}

int
chronoid_ksuid_compare (const chronoid_ksuid_t *a, const chronoid_ksuid_t *b)
{
  /* Dispatch through CHRONOID_KSUID_COMPARE20 -- compile-time-selected
   * SSE2 / NEON / scalar kernel. All three implementations agree
   * on the {-1, 0, +1} contract and on lexicographic byte order;
   * tests/test_compare_parity.c pins the equivalence. */
  return CHRONOID_KSUID_COMPARE20 (a->b, b->b);
}

chronoid_ksuid_err_t
chronoid_ksuid_from_bytes (chronoid_ksuid_t *out, const uint8_t *b, size_t n)
{
  if (n != CHRONOID_KSUID_BYTES)
    return CHRONOID_KSUID_ERR_SIZE;
  memcpy (out->b, b, CHRONOID_KSUID_BYTES);
  return CHRONOID_KSUID_OK;
}

chronoid_ksuid_err_t
chronoid_ksuid_from_parts (chronoid_ksuid_t *out,
    int64_t unix_seconds, const uint8_t *payload, size_t payload_len)
{
  if (payload_len != CHRONOID_KSUID_PAYLOAD_LEN)
    return CHRONOID_KSUID_ERR_PAYLOAD_SIZE;
  int64_t corrected = unix_seconds - CHRONOID_KSUID_EPOCH_SECONDS;
  if (corrected < 0 || corrected > (int64_t) UINT32_MAX)
    return CHRONOID_KSUID_ERR_TIME_RANGE;
  chronoid_be32_store (out->b, (uint32_t) corrected);
  memcpy (out->b + CHRONOID_KSUID_TIMESTAMP_LEN, payload, CHRONOID_KSUID_PAYLOAD_LEN);
  return CHRONOID_KSUID_OK;
}

uint32_t
chronoid_ksuid_timestamp (const chronoid_ksuid_t *id)
{
  return chronoid_be32_load (id->b);
}

int64_t
chronoid_ksuid_time_unix (const chronoid_ksuid_t *id)
{
  return (int64_t) chronoid_ksuid_timestamp (id) + CHRONOID_KSUID_EPOCH_SECONDS;
}

const uint8_t *
chronoid_ksuid_payload (const chronoid_ksuid_t *id)
{
  return id->b + CHRONOID_KSUID_TIMESTAMP_LEN;
}

chronoid_ksuid_err_t
chronoid_ksuid_parse (chronoid_ksuid_t *out, const char *s, size_t len)
{
  if (len != CHRONOID_KSUID_STRING_LEN)
    return CHRONOID_KSUID_ERR_STR_SIZE;
  /* The decoder partially writes its destination before detecting an
   * overflow; route through a stack temporary so the caller's |*out|
   * is never observed in a half-decoded state, matching the size-error
   * "untouched on failure" guarantee. */
  chronoid_ksuid_t tmp;
  chronoid_ksuid_err_t e = chronoid_base62_decode (tmp.b, (const uint8_t *) s);
  if (e != CHRONOID_KSUID_OK)
    return e;
  *out = tmp;
  return CHRONOID_KSUID_OK;
}

void
chronoid_ksuid_format (const chronoid_ksuid_t *id, char out[CHRONOID_KSUID_STRING_LEN])
{
  chronoid_base62_encode ((uint8_t *) out, id->b);
}

/* Atomic-pointer overrides for chronoid_set_rand. Readers use acquire
 * loads, writers use release stores so a swap mid-flight cannot tear
 * the (fn, ctx) pair. The two atomics are independent, so a concurrent
 * swap during a draw can still observe one half from the old override
 * and the other half from the new -- documented as caller's
 * responsibility (the user must not flip rng sources mid-load).
 *
 * _Atomic is spelled as a type qualifier (rather than _Atomic(T)) so
 * gst-indent does not parse it as a function call and reflow the
 * declarations weirdly. Static storage zero-initializes both pointers
 * to NULL, which matches the "no override installed" state. */
static _Atomic chronoid_rng_fn g_rng_fn;
static void *_Atomic g_rng_ctx;

void
chronoid_set_rand (chronoid_rng_fn fn, void *ctx)
{
  atomic_store_explicit (&g_rng_ctx, ctx, memory_order_release);
  atomic_store_explicit (&g_rng_fn, fn, memory_order_release);
}

static int
chronoid_ksuid_fill_payload (uint8_t *buf, size_t n)
{
  chronoid_rng_fn fn = atomic_load_explicit (&g_rng_fn, memory_order_acquire);
  if (fn != NULL) {
    void *ctx = atomic_load_explicit (&g_rng_ctx, memory_order_acquire);
    return fn (ctx, buf, n);
  }
  return chronoid_random_bytes (buf, n);
}

chronoid_ksuid_err_t
chronoid_ksuid_new_with_time (chronoid_ksuid_t *out, int64_t unix_seconds)
{
  int64_t corrected = unix_seconds - CHRONOID_KSUID_EPOCH_SECONDS;
  if (corrected < 0 || corrected > (int64_t) UINT32_MAX)
    return CHRONOID_KSUID_ERR_TIME_RANGE;
  /* Fill the payload first into a temporary so a partial RNG failure
   * cannot leak half a payload into |*out|. */
  uint8_t payload[CHRONOID_KSUID_PAYLOAD_LEN];
  if (chronoid_ksuid_fill_payload (payload, CHRONOID_KSUID_PAYLOAD_LEN) != 0)
    return CHRONOID_KSUID_ERR_RNG;
  chronoid_be32_store (out->b, (uint32_t) corrected);
  memcpy (out->b + CHRONOID_KSUID_TIMESTAMP_LEN, payload, CHRONOID_KSUID_PAYLOAD_LEN);
  return CHRONOID_KSUID_OK;
}

chronoid_ksuid_err_t
chronoid_ksuid_new (chronoid_ksuid_t *out)
{
  /* Use timespec_get rather than time(NULL) for portability with
   * Windows CRT and to share the path tested in rand_tls.c. */
  struct timespec ts;
  if (timespec_get (&ts, TIME_UTC) != TIME_UTC)
    return CHRONOID_KSUID_ERR_TIME_RANGE;
  return chronoid_ksuid_new_with_time (out, (int64_t) ts.tv_sec);
}
