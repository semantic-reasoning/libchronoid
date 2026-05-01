/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <chronoid/ksuid.h>
#include "test_util.h"

static const uint8_t kSampleBytes[CHRONOID_KSUID_BYTES] = {
  /* 0ujtsYcgvSTl8PAuAdqWYSMnLOv -- segmentio/ksuid README */
  0x06, 0x69, 0xF7, 0xEF,
  0xB5, 0xA1, 0xCD, 0x34, 0xB5, 0xF9, 0x9D, 0x11,
  0x54, 0xFB, 0x68, 0x53, 0x34, 0x5C, 0x97, 0x35,
};

#define SAMPLE_TS UINT32_C(107608047)   /* 0x0669F7EF */

static void
test_from_bytes_round_trip (void)
{
  chronoid_ksuid_t id = CHRONOID_KSUID_NIL;
  ASSERT_EQ_INT (chronoid_ksuid_from_bytes (&id, kSampleBytes, CHRONOID_KSUID_BYTES), CHRONOID_KSUID_OK);
  ASSERT_EQ_BYTES (id.b, kSampleBytes, CHRONOID_KSUID_BYTES);
}

static void
test_from_bytes_size_errors (void)
{
  chronoid_ksuid_t id = CHRONOID_KSUID_MAX;
  ASSERT_EQ_INT (chronoid_ksuid_from_bytes (&id, kSampleBytes, 0), CHRONOID_KSUID_ERR_SIZE);
  ASSERT_EQ_INT (chronoid_ksuid_from_bytes (&id, kSampleBytes, 19), CHRONOID_KSUID_ERR_SIZE);
  ASSERT_EQ_INT (chronoid_ksuid_from_bytes (&id, kSampleBytes, 21), CHRONOID_KSUID_ERR_SIZE);
  /* On error the output must not be silently mutated. */
  ASSERT_EQ_BYTES (id.b, CHRONOID_KSUID_MAX.b, CHRONOID_KSUID_BYTES);
}

static void
test_from_parts_writes_be_timestamp_and_payload (void)
{
  chronoid_ksuid_t id = CHRONOID_KSUID_NIL;
  int64_t unix_s = (int64_t) SAMPLE_TS + CHRONOID_KSUID_EPOCH_SECONDS;
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id, unix_s,
          kSampleBytes + CHRONOID_KSUID_TIMESTAMP_LEN, CHRONOID_KSUID_PAYLOAD_LEN), CHRONOID_KSUID_OK);
  ASSERT_EQ_BYTES (id.b, kSampleBytes, CHRONOID_KSUID_BYTES);
}

static void
test_from_parts_rejects_short_payload (void)
{
  chronoid_ksuid_t id = CHRONOID_KSUID_MAX;
  int64_t unix_s = CHRONOID_KSUID_EPOCH_SECONDS;
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id, unix_s,
          kSampleBytes + CHRONOID_KSUID_TIMESTAMP_LEN, 15), CHRONOID_KSUID_ERR_PAYLOAD_SIZE);
  ASSERT_EQ_BYTES (id.b, CHRONOID_KSUID_MAX.b, CHRONOID_KSUID_BYTES);
}

static void
test_from_parts_rejects_out_of_range_time (void)
{
  chronoid_ksuid_t id;
  uint8_t pl[CHRONOID_KSUID_PAYLOAD_LEN] = { 0 };
  /* Before epoch is invalid. */
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id, 0, pl, CHRONOID_KSUID_PAYLOAD_LEN),
      CHRONOID_KSUID_ERR_TIME_RANGE);
  /* Past epoch + UINT32_MAX is invalid. */
  int64_t past = CHRONOID_KSUID_EPOCH_SECONDS + (int64_t) UINT32_MAX + 1;
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id, past, pl, CHRONOID_KSUID_PAYLOAD_LEN),
      CHRONOID_KSUID_ERR_TIME_RANGE);
  /* Both endpoints are valid (closed interval). */
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id, CHRONOID_KSUID_EPOCH_SECONDS,
          pl, CHRONOID_KSUID_PAYLOAD_LEN), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (chronoid_ksuid_from_parts (&id,
          CHRONOID_KSUID_EPOCH_SECONDS + (int64_t) UINT32_MAX,
          pl, CHRONOID_KSUID_PAYLOAD_LEN), CHRONOID_KSUID_OK);
}

static void
test_accessors (void)
{
  chronoid_ksuid_t id;
  ASSERT_EQ_INT (chronoid_ksuid_from_bytes (&id, kSampleBytes, CHRONOID_KSUID_BYTES), CHRONOID_KSUID_OK);
  ASSERT_EQ_INT (chronoid_ksuid_timestamp (&id), SAMPLE_TS);
  ASSERT_EQ_INT (chronoid_ksuid_time_unix (&id),
      (int64_t) SAMPLE_TS + CHRONOID_KSUID_EPOCH_SECONDS);
  ASSERT_EQ_BYTES (chronoid_ksuid_payload (&id),
      kSampleBytes + CHRONOID_KSUID_TIMESTAMP_LEN, CHRONOID_KSUID_PAYLOAD_LEN);
}

static void
test_nil_and_max_accessors (void)
{
  ASSERT_EQ_INT (chronoid_ksuid_timestamp (&CHRONOID_KSUID_NIL), 0);
  ASSERT_EQ_INT (chronoid_ksuid_timestamp (&CHRONOID_KSUID_MAX), UINT32_MAX);
  ASSERT_EQ_INT (chronoid_ksuid_time_unix (&CHRONOID_KSUID_NIL), CHRONOID_KSUID_EPOCH_SECONDS);
  ASSERT_EQ_INT (chronoid_ksuid_time_unix (&CHRONOID_KSUID_MAX),
      CHRONOID_KSUID_EPOCH_SECONDS + (int64_t) UINT32_MAX);
}

int
main (void)
{
  RUN_TEST (test_from_bytes_round_trip);
  RUN_TEST (test_from_bytes_size_errors);
  RUN_TEST (test_from_parts_writes_be_timestamp_and_payload);
  RUN_TEST (test_from_parts_rejects_short_payload);
  RUN_TEST (test_from_parts_rejects_out_of_range_time);
  RUN_TEST (test_accessors);
  RUN_TEST (test_nil_and_max_accessors);
  TEST_MAIN_END ();
}
