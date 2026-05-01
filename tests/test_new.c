/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <chronoid/ksuid.h>
#include "test_util.h"

#include <time.h>

static void
test_new_returns_ok_and_recent_timestamp (void)
{
  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_new (&id), CHRONOID_KSUID_OK);
  /* The timestamp must fall in roughly the same minute as time(NULL).
   * CHRONOID_KSUID_EPOCH_SECONDS shifts the wall-clock back to the KSUID
   * domain, so we compare using the unix-seconds accessor. */
  int64_t now = (int64_t) time (NULL);
  int64_t ts = chronoid_ksuid_time_unix (&id);
  ASSERT_TRUE (ts >= now - 5);
  ASSERT_TRUE (ts <= now + 5);
}

static void
test_new_payload_is_random (void)
{
  chronoid_ksuid_t a, b;
  ASSERT_EQ_INT (chronoid_ksuid_new (&a), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (chronoid_ksuid_new (&b), CHRONOID_KSUID_OK);
  /* Two consecutive draws must differ in the payload region, even if
   * they happen to share a timestamp second. */
  ASSERT_TRUE (memcmp (chronoid_ksuid_payload (&a), chronoid_ksuid_payload (&b),
          CHRONOID_KSUID_PAYLOAD_LEN) != 0);
}

static void
test_new_with_time_pins_timestamp (void)
{
  /* Use a known mid-range timestamp: 2025-01-01 00:00:00 UTC. */
  int64_t unix_s = 1735689600LL;
  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_new_with_time (&id, unix_s), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (chronoid_ksuid_time_unix (&id), unix_s);
}

static void
test_new_with_time_rejects_out_of_range (void)
{
  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_new_with_time (&id, 0),
      CHRONOID_KSUID_ERR_TIME_RANGE);
  int64_t past = CHRONOID_KSUID_EPOCH_SECONDS + (int64_t) UINT32_MAX + 1;
  ASSERT_EQ_INT (chronoid_ksuid_new_with_time (&id, past),
      CHRONOID_KSUID_ERR_TIME_RANGE);
}

/* A test-only RNG that always returns a fixed byte pattern; lets us
 * pin the payload deterministically and verify chronoid_set_rand wires
 * it through chronoid_ksuid_new. */
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
test_set_rand_overrides_default_source (void)
{
  chronoid_test_rng_ctx_t ctx = {.fill = 0xa5,.call_count = 0 };
  chronoid_set_rand (test_rng_fixed, &ctx);

  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_new (&id), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (ctx.call_count, 1);
  uint8_t expected_payload[CHRONOID_KSUID_PAYLOAD_LEN];
  memset (expected_payload, 0xa5, sizeof expected_payload);
  ASSERT_EQ_BYTES (chronoid_ksuid_payload (&id), expected_payload,
      CHRONOID_KSUID_PAYLOAD_LEN);

  /* Restore default. */
  chronoid_set_rand (NULL, NULL);
  chronoid_ksuid_t id2;
  ASSERT_EQ_INT (chronoid_ksuid_new (&id2), CHRONOID_KSUID_OK);
  /* After restore, the payload must NOT be all 0xa5 (vanishingly
   * improbable for a real CSPRNG). */
  uint8_t allmatch = 1;
  for (size_t i = 0; i < CHRONOID_KSUID_PAYLOAD_LEN; ++i)
    if (chronoid_ksuid_payload (&id2)[i] != 0xa5) {
      allmatch = 0;
      break;
    }
  ASSERT_FALSE (allmatch);
}

static void
test_set_rand_failure_propagates (void)
{
  chronoid_set_rand (test_rng_failing, NULL);
  chronoid_ksuid_t id = CHRONOID_KSUID_MAX;
  ASSERT_EQ_INT (chronoid_ksuid_new (&id), CHRONOID_KSUID_ERR_RNG);
  /* On RNG failure |id| must be unchanged. */
  ASSERT_EQ_INT (chronoid_ksuid_compare (&id, &CHRONOID_KSUID_MAX), 0);
  chronoid_set_rand (NULL, NULL);
}

int
main (void)
{
  RUN_TEST (test_new_returns_ok_and_recent_timestamp);
  RUN_TEST (test_new_payload_is_random);
  RUN_TEST (test_new_with_time_pins_timestamp);
  RUN_TEST (test_new_with_time_rejects_out_of_range);
  RUN_TEST (test_set_rand_overrides_default_source);
  RUN_TEST (test_set_rand_failure_propagates);
  TEST_MAIN_END ();
}
