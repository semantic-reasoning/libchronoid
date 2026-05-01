/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Tests for chronoid_uuidv7_new and chronoid_uuidv7_new_with_time:
 *
 *   - basic generation produces a syntactically-valid UUIDv7 with
 *     the correct version + variant nibbles;
 *   - the embedded ms timestamp tracks the wall clock;
 *   - new_with_time honours the supplied ms verbatim;
 *   - out-of-range ms returns CHRONOID_UUIDV7_ERR_TIME_RANGE without
 *     mutating |*out|;
 *   - the chronoid_set_rand override (shared with KSUID) routes
 *     UUIDv7 random bytes through the supplied callback;
 *   - RNG failure is surfaced as CHRONOID_UUIDV7_ERR_RNG and |*out|
 *     stays unchanged.
 */
#include <chronoid/ksuid.h>      /* for chronoid_set_rand */
#include <chronoid/uuidv7.h>
#include "test_util.h"

#include <time.h>

static void
test_new_returns_valid_uuidv7 (void)
{
  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_new (&id), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&id), 0x2);
}

static void
test_new_timestamp_tracks_wallclock (void)
{
  /* Read the wall clock manually around the chronoid_uuidv7_new call.
   * The embedded ms must lie within [pre - 5ms, post + 50ms] -- a
   * generous tolerance for slow CI runners. timespec_get is C11 and
   * matches what chronoid_now_ms does internally. */
  struct timespec ts;
  ASSERT_EQ_INT (timespec_get (&ts, TIME_UTC), TIME_UTC);
  int64_t pre_ms = (int64_t) ts.tv_sec * 1000 + (int64_t) (ts.tv_nsec / 1000000);

  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_new (&id), CHRONOID_UUIDV7_OK);

  ASSERT_EQ_INT (timespec_get (&ts, TIME_UTC), TIME_UTC);
  int64_t post_ms = (int64_t) ts.tv_sec * 1000 + (int64_t) (ts.tv_nsec / 1000000);

  int64_t embedded = chronoid_uuidv7_unix_ms (&id);
  ASSERT_TRUE (embedded >= pre_ms - 5);
  ASSERT_TRUE (embedded <= post_ms + 50);
}

static void
test_new_with_time_pins_timestamp (void)
{
  /* Mid-range ms. Anything in [0, 2^48 - 1] is acceptable; pick a
   * value typical of a 2009-ish wall clock so a debugger printout
   * is recognisable. */
  const int64_t unix_ms = 1234567890123LL;
  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, unix_ms), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&id), unix_ms);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&id), 0x2);
}

static void
test_new_with_time_rejects_out_of_range (void)
{
  /* Sentinel: pre-fill with the MAX pattern so we can verify
   * |*out| is left untouched on the error path. */
  chronoid_uuidv7_t guard = CHRONOID_UUIDV7_MAX;
  chronoid_uuidv7_t id = guard;

  /* Negative ms (pre-1970). */
  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, -1),
      CHRONOID_UUIDV7_ERR_TIME_RANGE);
  ASSERT_EQ_BYTES (id.b, guard.b, CHRONOID_UUIDV7_BYTES);

  /* Just above the 48-bit ceiling. */
  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, 1LL << 48),
      CHRONOID_UUIDV7_ERR_TIME_RANGE);
  ASSERT_EQ_BYTES (id.b, guard.b, CHRONOID_UUIDV7_BYTES);

  /* Far above. */
  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, INT64_MAX),
      CHRONOID_UUIDV7_ERR_TIME_RANGE);
  ASSERT_EQ_BYTES (id.b, guard.b, CHRONOID_UUIDV7_BYTES);

  /* Sanity: the largest valid ms succeeds. */
  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, (1LL << 48) - 1),
      CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&id), (1LL << 48) - 1);
}

/* Test-only deterministic RNG: returns the byte stored in |fill|.
 * Same shape as the helper in tests/test_new.c so the KSUID and
 * UUIDv7 paths exercise chronoid_set_rand the same way. */
typedef struct
{
  uint8_t fill;
  int call_count;
} chronoid_test_rng_ctx_t;

static int
test_rng_fixed (void *opaque, uint8_t *buf, size_t n)
{
  chronoid_test_rng_ctx_t *c = opaque;
  ++c->call_count;
  memset (buf, c->fill, n);
  return 0;
}

static int
test_rng_failing (void *opaque, uint8_t *buf, size_t n)
{
  (void) opaque;
  (void) buf;
  (void) n;
  return -1;
}

static void
test_set_rand_overrides_uuidv7_random (void)
{
  chronoid_test_rng_ctx_t ctx = { .fill = 0xa5, .call_count = 0 };
  chronoid_set_rand (test_rng_fixed, &ctx);

  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_new (&id), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (ctx.call_count, 1);

  /* Predicted byte layout when every random byte is 0xa5:
   *   bytes 0..5 : 48-bit big-endian wall-clock ms (uncontrolled).
   *   byte 6     : 0x70 | ((rnd[0] >> 4) & 0x0F)
   *                 with rnd[0] = 0xa5  ⇒ (0xa5 >> 4) = 0x0a
   *                 ⇒ 0x70 | 0x0a = 0x7a
   *   byte 7     : rnd[1] = 0xa5
   *   byte 8     : 0x80 | (rnd[2] & 0x3F)
   *                 with rnd[2] = 0xa5 (low 6 bits = 0x25)
   *                 ⇒ 0x80 | 0x25 = 0xa5
   *   bytes 9..15: rnd[3..9] = 0xa5 each
   *
   * (rnd[0] is the high byte of the 12-bit rand_a; emit reads
   * rand_a's high nibble from bits 8..11 of the 16-bit value, i.e.
   * the low 4 bits of rnd[0]'s top nibble after shift-right-by-8 of
   * the masked 12-bit value. Since rnd[0] = 0xa5, the 12-bit value
   * is (0xa5 << 8 | 0xa5) & 0x0FFF = 0x5a5; high nibble is 0x5,
   * so byte 6 = 0x70 | 0x5 = 0x75 and byte 7 = 0xa5. Recompute. */
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&id), 0x2);
  /* High nibble of byte 6 is forced to 0x7 (version); low nibble is
   * the high 4 bits of the 12-bit counter = (0xa5a5 & 0x0FFF) >> 8 =
   * 0x5. So byte 6 == 0x75. */
  ASSERT_EQ_INT (id.b[6], 0x75);
  ASSERT_EQ_INT (id.b[7], 0xa5);
  /* Byte 8: 0x80 | (0xa5 & 0x3F) = 0x80 | 0x25 = 0xa5. */
  ASSERT_EQ_INT (id.b[8], 0xa5);
  /* Bytes 9..15: rnd[3..9] = 0xa5 each. */
  for (int i = 9; i < 16; ++i)
    ASSERT_EQ_INT (id.b[i], 0xa5);

  chronoid_set_rand (NULL, NULL);
  /* Sanity: a real draw after restore must NOT match the all-0xa5
   * tail (probability 2^-72 of collision -- effectively impossible). */
  chronoid_uuidv7_t id2;
  ASSERT_EQ_INT (chronoid_uuidv7_new (&id2), CHRONOID_UUIDV7_OK);
  uint8_t a5_pattern[7];
  memset (a5_pattern, 0xa5, sizeof a5_pattern);
  ASSERT_TRUE (memcmp (id2.b + 9, a5_pattern, sizeof a5_pattern) != 0
      || id2.b[8] != 0xa5);
}

static void
test_new_propagates_rng_failure (void)
{
  chronoid_set_rand (test_rng_failing, NULL);
  chronoid_uuidv7_t guard = CHRONOID_UUIDV7_MAX;
  chronoid_uuidv7_t id = guard;
  ASSERT_EQ_INT (chronoid_uuidv7_new (&id), CHRONOID_UUIDV7_ERR_RNG);
  /* RNG failure: |*out| must be left untouched. */
  ASSERT_EQ_BYTES (id.b, guard.b, CHRONOID_UUIDV7_BYTES);

  ASSERT_EQ_INT (chronoid_uuidv7_new_with_time (&id, 1234567890123LL),
      CHRONOID_UUIDV7_ERR_RNG);
  ASSERT_EQ_BYTES (id.b, guard.b, CHRONOID_UUIDV7_BYTES);

  chronoid_set_rand (NULL, NULL);
}

int
main (void)
{
  RUN_TEST (test_new_returns_valid_uuidv7);
  RUN_TEST (test_new_timestamp_tracks_wallclock);
  RUN_TEST (test_new_with_time_pins_timestamp);
  RUN_TEST (test_new_with_time_rejects_out_of_range);
  RUN_TEST (test_set_rand_overrides_uuidv7_random);
  RUN_TEST (test_new_propagates_rng_failure);
  TEST_MAIN_END ();
}
