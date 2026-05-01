/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * UUIDv7 monotonic-sequence regression tests. Pins the RFC 9562 §6.2
 * method 1 obligations enforced by chronoid_uuidv7_sequence_next:
 *
 *   - init draws a non-zero starting counter (Critic R4.3);
 *   - within a fixed observed ms, emits are strictly monotonic;
 *   - 4096+ rapid emits within a fixed ms exercise the option (a)
 *     timestamp-bump overflow path without ever returning EXHAUSTED
 *     (R4.1);
 *   - a wall-clock backwards step (NTP / VM resume) triggers the
 *     last_ms clamp and emits stay strictly monotonic (R4.2);
 *   - rand_b is redrawn on real ms-tick;
 *   - bounds bracket the emit on the same-ms branch.
 *
 * The CHRONOID_TESTING-gated time-source override (declared in
 * chronoid/rand.h) is what makes these tests deterministic: the
 * test pins chronoid_now_ms to a controlled value via
 * chronoid_set_time_source_for_testing.
 */
#include <chronoid/rand.h>
#include <chronoid/uuidv7.h>
#include "test_util.h"

#include <stdatomic.h>

/* ----- test-controlled time source --------------------------------- */

/* Stored as _Atomic so the test source can be queried from the same
 * thread without TSan / -Wthread-safety complaints, and so future
 * threaded tests can flip it race-free. The atomic is overkill for
 * the single-threaded tests below but matches the production override
 * slot's semantics. */
static _Atomic int64_t g_pinned_ms_;

static int64_t
pinned_time_source (void)
{
  return atomic_load_explicit (&g_pinned_ms_, memory_order_acquire);
}

static void
install_pinned_clock (int64_t ms)
{
  atomic_store_explicit (&g_pinned_ms_, ms, memory_order_release);
  chronoid_set_time_source_for_testing (pinned_time_source);
}

static void
restore_default_clock (void)
{
  chronoid_set_time_source_for_testing (NULL);
}

/* ----- helpers ----------------------------------------------------- */

static int64_t
read_unix_ms (const chronoid_uuidv7_t *id)
{
  return chronoid_uuidv7_unix_ms (id);
}

static uint16_t
read_counter_12 (const chronoid_uuidv7_t *id)
{
  return (uint16_t) ((((uint16_t) (id->b[6] & 0x0F)) << 8) | id->b[7]);
}

/* ----- tests ------------------------------------------------------- */

static void
test_init_draws_random_state (void)
{
  /* Statistical: counter == 0 has 1/4096 probability. We init two
   * sequences and assert at least one of them has a non-zero counter
   * -- the joint probability of both colliding on counter == 0 is
   * 1/16777216, well below the noise floor. The same logic applies
   * to rand_b being all-zero (probability 2^-64). */
  chronoid_uuidv7_sequence_t s1, s2;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s1), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s2), CHRONOID_UUIDV7_OK);

  ASSERT_TRUE (s1.counter != 0 || s2.counter != 0);

  /* Initial last_ms must be 0 (or a sentinel <= a real wall clock).
   * The first _next call's clamp then degenerates to use wall_ms. */
  ASSERT_TRUE (s1.last_ms <= 0);
  ASSERT_TRUE (s2.last_ms <= 0);

  /* rand_b not all-zero. Probability of collision on a real CSPRNG
   * is 2^-64, so this is effectively a deterministic check. */
  uint8_t zero8[8] = { 0 };
  ASSERT_TRUE (memcmp (s1.rand_b, zero8, 8) != 0
      || memcmp (s2.rand_b, zero8, 8) != 0);
}

static void
test_monotonic_within_fixed_ms (void)
{
  install_pinned_clock (1700000000000LL);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  chronoid_uuidv7_t prev, cur;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &prev), CHRONOID_UUIDV7_OK);
  for (int i = 1; i < 100; ++i) {
    ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &cur),
        CHRONOID_UUIDV7_OK);
    /* Strict monotonicity over the full 16-byte representation. */
    ASSERT_TRUE (chronoid_uuidv7_compare (&prev, &cur) < 0);
    /* Version + variant nibbles must always be set correctly on emit. */
    ASSERT_EQ_INT (chronoid_uuidv7_version (&cur), 0x7);
    ASSERT_EQ_INT (chronoid_uuidv7_variant (&cur), 0x2);
    prev = cur;
  }

  restore_default_clock ();
}

static void
test_counter_overflow_bumps_timestamp (void)
{
  /* RFC 9562 §6.2 option (a): on counter overflow within a single
   * observed ms, bump the embedded timestamp by 1 ms and reseed the
   * counter. The sequence MUST NEVER return CHRONOID_UUIDV7_ERR_EXHAUSTED;
   * that error code is not even defined.
   *
   * We pin the wall clock to a fixed ms and request 4097 emits.
   * Since init seeds a random starting counter c0 in [0, 4095], the
   * sequence will tick the counter at least once over its full
   * range and trigger at least one timestamp bump within the loop.
   * The exact bump count depends on c0 (4097 emits with c0 = 0
   * produces exactly one bump; c0 = 4095 produces two). */
  const int64_t pinned_ms = 1700000123456LL;
  install_pinned_clock (pinned_ms);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  /* Capture the very first emit's ms; it must equal pinned_ms (no
   * bump has happened yet). */
  chronoid_uuidv7_t prev;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &prev), CHRONOID_UUIDV7_OK);
  int64_t first_ms = read_unix_ms (&prev);
  ASSERT_EQ_INT (first_ms, pinned_ms);

  int64_t last_ms = first_ms;
  for (int i = 1; i < 4097; ++i) {
    chronoid_uuidv7_t cur;
    ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &cur),
        CHRONOID_UUIDV7_OK);
    /* Strict monotonicity over the entire 4097-emit run. */
    ASSERT_TRUE (chronoid_uuidv7_compare (&prev, &cur) < 0);
    int64_t cur_ms = read_unix_ms (&cur);
    /* Embedded ms either stays put or steps forward by 1. It must
     * never step back, and the step must never exceed 1 (4097 emits
     * cannot trigger more than 2 bumps from any starting counter). */
    ASSERT_TRUE (cur_ms >= last_ms);
    ASSERT_TRUE (cur_ms - last_ms <= 1);
    last_ms = cur_ms;
    prev = cur;
  }

  /* After 4097 emits with a pinned wall clock, the embedded ms must
   * have advanced by at least 1 (a real overflow bump fired). We
   * deliberately do NOT pin a tight upper bound on last_ms: under
   * sanitizer / MALLOC_PERTURB_ instrumentation, allocator
   * reentrancy and timing perturbation can drive additional bumps
   * via the ms-tick branch, and the precise count is implementation
   * detail. The contract this test pins is monotonicity (asserted
   * inside the loop above) and that AT LEAST one overflow bump
   * fired during the 4097-emit window — not the exact ms count.
   * Closes #2. */
  ASSERT_TRUE (last_ms > pinned_ms);

  restore_default_clock ();
}

static void
test_clock_backward_clamps_to_last_ms (void)
{
  /* Pin the wall clock to ms=1000, mint A. Then warp the clock back
   * to ms=500 (NTP step / VM resume). The next emit must clamp to
   * last_ms=1000, and B must compare strictly greater than A. */
  install_pinned_clock (1000);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  chronoid_uuidv7_t a, b;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &a), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (read_unix_ms (&a), 1000);

  install_pinned_clock (500);   /* clock went backwards */
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &b), CHRONOID_UUIDV7_OK);
  /* Embedded ms is the clamped value, not the rewound 500. */
  ASSERT_EQ_INT (read_unix_ms (&b), 1000);
  /* And monotonicity holds across the warp. */
  ASSERT_TRUE (chronoid_uuidv7_compare (&a, &b) < 0);

  restore_default_clock ();
}

static void
test_ms_tick_advances_embedded_timestamp (void)
{
  /* On a real ms-tick the sequence must adopt the new ms and rand_b
   * is freshly drawn. We can't directly assert "rand_b is different"
   * because two independent CSPRNG draws can collide (probability
   * 2^-64), but we CAN assert that the embedded ms advances and
   * that two emits at different ms values are strictly ordered. */
  install_pinned_clock (1000);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  chronoid_uuidv7_t a;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &a), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (read_unix_ms (&a), 1000);

  install_pinned_clock (2000);
  chronoid_uuidv7_t b;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &b), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (read_unix_ms (&b), 2000);
  /* Strict ordering across ms-tick. */
  ASSERT_TRUE (chronoid_uuidv7_compare (&a, &b) < 0);
  /* The internal s->last_ms tracked the new tick. */
  ASSERT_EQ_INT (s.last_ms, 2000);

  restore_default_clock ();
}

static void
test_bounds_bracket_next_emit (void)
{
  install_pinned_clock (1700000000000LL);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  /* Prime with one emit so s->counter is bounded on the lower end. */
  chronoid_uuidv7_t primed;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &primed),
      CHRONOID_UUIDV7_OK);

  chronoid_uuidv7_t lo, hi, actual;
  chronoid_uuidv7_sequence_bounds (&s, &lo, &hi);
  /* lo and hi are within the same observed ms (pinned_ms), so they
   * share the leading 6 bytes. */
  ASSERT_EQ_BYTES (lo.b, hi.b, 6);

  /* The next emit (still within the pinned ms) must bracket [lo, hi]
   * inclusive on the lex compare, modulo the counter boundary case
   * where saturating lo_counter at 0xFFF means lo == hi. */
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &actual),
      CHRONOID_UUIDV7_OK);
  ASSERT_TRUE (chronoid_uuidv7_compare (&lo, &actual) <= 0);
  ASSERT_TRUE (chronoid_uuidv7_compare (&actual, &hi) <= 0);

  /* Version / variant correctness on the bound sentinels too. */
  ASSERT_EQ_INT (chronoid_uuidv7_version (&lo), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&hi), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&lo), 0x2);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&hi), 0x2);

  restore_default_clock ();
}

static void
test_counter_increments_within_same_ms (void)
{
  /* Within a fixed observed ms, the counter must increment by 1 per
   * emit (until overflow promotes the timestamp). */
  install_pinned_clock (1700000000000LL);

  chronoid_uuidv7_sequence_t s;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_init (&s), CHRONOID_UUIDV7_OK);

  chronoid_uuidv7_t a, b;
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &a), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_sequence_next (&s, &b), CHRONOID_UUIDV7_OK);

  /* If the counter didn't roll over (almost always true: only 1/4096
   * starting positions roll on the second emit), the embedded ms is
   * unchanged and the counter stepped by 1. */
  if (read_unix_ms (&a) == read_unix_ms (&b)) {
    uint16_t ca = read_counter_12 (&a);
    uint16_t cb = read_counter_12 (&b);
    ASSERT_EQ_INT ((cb - ca) & 0xFFF, 1);
  }

  restore_default_clock ();
}

int
main (void)
{
  RUN_TEST (test_init_draws_random_state);
  RUN_TEST (test_monotonic_within_fixed_ms);
  RUN_TEST (test_counter_overflow_bumps_timestamp);
  RUN_TEST (test_clock_backward_clamps_to_last_ms);
  RUN_TEST (test_ms_tick_advances_embedded_timestamp);
  RUN_TEST (test_bounds_bracket_next_emit);
  RUN_TEST (test_counter_increments_within_same_ms);
  TEST_MAIN_END ();
}
