/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <chronoid/ksuid.h>
#include "test_util.h"

static const uint8_t kSampleBytes[CHRONOID_KSUID_BYTES] = {
  0x06, 0x69, 0xF7, 0xEF,
  0xB5, 0xA1, 0xCD, 0x34, 0xB5, 0xF9, 0x9D, 0x11,
  0x54, 0xFB, 0x68, 0x53, 0x34, 0x5C, 0x97, 0x35,
};

static const char *const kSampleStr = "0ujtsYcgvSTl8PAuAdqWYSMnLOv";
static const char *const kNilStr = "000000000000000000000000000";
static const char *const kMaxStr = "aWgEPTl1tmebfsQzFP4bxwgy80V";

static void
test_parse_golden (void)
{
  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kSampleStr, CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_OK);
  ASSERT_EQ_BYTES (id.b, kSampleBytes, CHRONOID_KSUID_BYTES);

  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kNilStr, CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_OK);
  ASSERT_TRUE (chronoid_ksuid_is_nil (&id));

  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kMaxStr, CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (chronoid_ksuid_compare (&id, &CHRONOID_KSUID_MAX), 0);
}

static void
test_parse_size_errors (void)
{
  chronoid_ksuid_t id = CHRONOID_KSUID_MAX;
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kSampleStr, 0), CHRONOID_KSUID_ERR_STR_SIZE);
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kSampleStr, 26), CHRONOID_KSUID_ERR_STR_SIZE);
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, kSampleStr, 28), CHRONOID_KSUID_ERR_STR_SIZE);
  /* On size error |out| must not have been mutated. */
  ASSERT_EQ_INT (chronoid_ksuid_compare (&id, &CHRONOID_KSUID_MAX), 0);
}

static void
test_parse_value_errors (void)
{
  chronoid_ksuid_t id;
  /* Mirrors upstream TestIssue25 (ksuid_test.go:100-111). */
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, "aaaaaaaaaaaaaaaaaaaaaaaaaaa",
          CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, "aWgEPTl1tmebfsQzFP4bxwgy80!",
          CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_ERR_STR_VALUE);
  /* One past Max -- value overflow rather than alphabet violation. */
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, "aWgEPTl1tmebfsQzFP4bxwgy80W",
          CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_ERR_STR_VALUE);
}

static void
test_parse_value_error_does_not_mutate_out (void)
{
  /* The decoder partially writes its destination before detecting
   * overflow. chronoid_ksuid_parse must therefore use a temporary internally
   * and leave the caller's |*out| untouched on every failure path. */
  chronoid_ksuid_t pre = CHRONOID_KSUID_MAX;
  chronoid_ksuid_t id = pre;
  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, "aWgEPTl1tmebfsQzFP4bxwgy80W",
          CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_ERR_STR_VALUE);
  ASSERT_EQ_BYTES (id.b, pre.b, CHRONOID_KSUID_BYTES);

  ASSERT_EQ_INT (chronoid_ksuid_parse (&id, "!!!!!!!!!!!!!!!!!!!!!!!!!!!",
          CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_ERR_STR_VALUE);
  ASSERT_EQ_BYTES (id.b, pre.b, CHRONOID_KSUID_BYTES);
}

static void
test_format_golden (void)
{
  char out[CHRONOID_KSUID_STRING_LEN];

  chronoid_ksuid_format (&CHRONOID_KSUID_NIL, out);
  ASSERT_EQ_STRN (out, kNilStr, CHRONOID_KSUID_STRING_LEN);

  chronoid_ksuid_format (&CHRONOID_KSUID_MAX, out);
  ASSERT_EQ_STRN (out, kMaxStr, CHRONOID_KSUID_STRING_LEN);

  chronoid_ksuid_t sample;
  chronoid_ksuid_from_bytes (&sample, kSampleBytes, CHRONOID_KSUID_BYTES);
  chronoid_ksuid_format (&sample, out);
  ASSERT_EQ_STRN (out, kSampleStr, CHRONOID_KSUID_STRING_LEN);
}

static void
test_round_trip (void)
{
  /* Exercise format . parse for a deterministic byte pattern that
   * walks through a wide range of timestamp and payload values. */
  for (size_t trial = 0; trial < 128; ++trial) {
    chronoid_ksuid_t in;
    uint64_t s = 0xcbf29ce484222325ULL ^ (trial * 0x100000001b3ULL);
    for (size_t i = 0; i < CHRONOID_KSUID_BYTES; ++i) {
      s = s * 6364136223846793005ULL + 1442695040888963407ULL;
      in.b[i] = (uint8_t) (s >> 56);
    }
    char str[CHRONOID_KSUID_STRING_LEN];
    chronoid_ksuid_format (&in, str);
    chronoid_ksuid_t round;
    ASSERT_EQ_INT (chronoid_ksuid_parse (&round, str, CHRONOID_KSUID_STRING_LEN), CHRONOID_KSUID_OK);
    ASSERT_EQ_BYTES (round.b, in.b, CHRONOID_KSUID_BYTES);
  }
}

int
main (void)
{
  RUN_TEST (test_parse_golden);
  RUN_TEST (test_parse_size_errors);
  RUN_TEST (test_parse_value_errors);
  RUN_TEST (test_parse_value_error_does_not_mutate_out);
  RUN_TEST (test_format_golden);
  RUN_TEST (test_round_trip);
  TEST_MAIN_END ();
}
