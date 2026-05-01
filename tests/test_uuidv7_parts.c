/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 * UUIDv7 (RFC 9562) implementation. No upstream lineage.
 */
#include <chronoid/uuidv7.h>
#include "test_util.h"

/* A representative non-trivial timestamp: 2024-01-01T00:00:00Z in
 * Unix milliseconds = 1704067200000. Comfortably mid-range for the
 * 48-bit field, all six bytes non-zero, easy to verify by hand. */
#define SAMPLE_UNIX_MS ((int64_t) 1704067200000LL)

/* 1704067200000 = 0x0000018CC251F400, so bytes 0..5 = 01 8C C2 51 F4 00. */
static const uint8_t kSampleTimestampBE[6] = {
  0x01, 0x8C, 0xC2, 0x51, 0xF4, 0x00,
};

static const uint8_t kSampleRandB[8] = {
  0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};

#define SAMPLE_RAND_A12 ((uint16_t) 0x0ABC)

static void
test_from_parts_writes_be_timestamp (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, SAMPLE_UNIX_MS,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (id.b, kSampleTimestampBE, 6);
}

static void
test_from_parts_sets_version_nibble (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, SAMPLE_UNIX_MS,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  /* High nibble of byte 6 is the version, low nibble is the top of rand_a. */
  ASSERT_EQ_INT ((id.b[6] >> 4) & 0x0F, 0x7);
  ASSERT_EQ_INT (id.b[6] & 0x0F, (SAMPLE_RAND_A12 >> 8) & 0x0F);
  ASSERT_EQ_INT (id.b[7], SAMPLE_RAND_A12 & 0xFF);
}

static void
test_from_parts_sets_variant_bits (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, SAMPLE_UNIX_MS,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&id), 0x2);
  /* Top two bits of byte 8 are 0b10; the low six bits come from rand_b[0]. */
  ASSERT_EQ_INT ((id.b[8] >> 6) & 0x03, 0x2);
  ASSERT_EQ_INT (id.b[8] & 0x3F, kSampleRandB[0] & 0x3F);
  /* Bytes 9..15 are rand_b[1..7] verbatim. */
  ASSERT_EQ_BYTES (id.b + 9, kSampleRandB + 1, 7);
}

static void
test_from_parts_unix_ms_roundtrip (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, SAMPLE_UNIX_MS,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&id), SAMPLE_UNIX_MS);

  /* Boundary cases roundtrip cleanly too. */
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, 0,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&id), 0);

  int64_t max_ms = (1LL << 48) - 1;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, max_ms,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&id), max_ms);
}

static void
test_from_parts_masks_high_rand_a_bits (void)
{
  /* Caller passes 0xFFFF; the library must mask to 12 bits and write
   * the version nibble itself. The result still reads version 7, and
   * the low 12 bits of rand_a are all 1s, so byte 6 = 0x7F and
   * byte 7 = 0xFF. */
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, SAMPLE_UNIX_MS,
          (uint16_t) 0xFFFF, kSampleRandB), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  ASSERT_EQ_INT (id.b[6], 0x7F);
  ASSERT_EQ_INT (id.b[7], 0xFF);
}

static void
test_from_parts_rejects_negative_time (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, -1,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_ERR_TIME_RANGE);
  /* On error |*out| must not be silently mutated. */
  ASSERT_EQ_BYTES (id.b, CHRONOID_UUIDV7_MAX.b, CHRONOID_UUIDV7_BYTES);
}

static void
test_from_parts_rejects_overflow_time (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;
  /* (1 << 48) is one past the largest encodable Unix-ms value. */
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, (int64_t) (1LL << 48),
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_ERR_TIME_RANGE);
  ASSERT_EQ_BYTES (id.b, CHRONOID_UUIDV7_MAX.b, CHRONOID_UUIDV7_BYTES);
}

static void
test_from_parts_accepts_boundaries (void)
{
  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, 0,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
  int64_t max_ms = (1LL << 48) - 1;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&id, max_ms,
          SAMPLE_RAND_A12, kSampleRandB), CHRONOID_UUIDV7_OK);
}

static void
test_from_bytes_round_trip (void)
{
  /* A handcrafted 16-byte value that is ALSO a syntactically valid
   * UUIDv7 (version=7, variant=0b10) so the accessors return the
   * expected values from the parsed form. */
  static const uint8_t kSample[CHRONOID_UUIDV7_BYTES] = {
    0x01, 0x8C, 0xC2, 0x51, 0xF4, 0x00,
    0x7A, 0xBC,                 /* version 7, rand_a low = 0xABC */
    0x91, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
  };
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_NIL;
  ASSERT_EQ_INT (chronoid_uuidv7_from_bytes (&id, kSample,
          CHRONOID_UUIDV7_BYTES), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (id.b, kSample, CHRONOID_UUIDV7_BYTES);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&id), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&id), 0x2);
}

static void
test_from_bytes_size_errors (void)
{
  static const uint8_t kSample[CHRONOID_UUIDV7_BYTES] = { 0 };
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;
  ASSERT_EQ_INT (chronoid_uuidv7_from_bytes (&id, kSample, 0),
      CHRONOID_UUIDV7_ERR_SIZE);
  ASSERT_EQ_INT (chronoid_uuidv7_from_bytes (&id, kSample, 15),
      CHRONOID_UUIDV7_ERR_SIZE);
  ASSERT_EQ_INT (chronoid_uuidv7_from_bytes (&id, kSample, 17),
      CHRONOID_UUIDV7_ERR_SIZE);
  /* On error the output must not be silently mutated. */
  ASSERT_EQ_BYTES (id.b, CHRONOID_UUIDV7_MAX.b, CHRONOID_UUIDV7_BYTES);
}

int
main (void)
{
  RUN_TEST (test_from_parts_writes_be_timestamp);
  RUN_TEST (test_from_parts_sets_version_nibble);
  RUN_TEST (test_from_parts_sets_variant_bits);
  RUN_TEST (test_from_parts_unix_ms_roundtrip);
  RUN_TEST (test_from_parts_masks_high_rand_a_bits);
  RUN_TEST (test_from_parts_rejects_negative_time);
  RUN_TEST (test_from_parts_rejects_overflow_time);
  RUN_TEST (test_from_parts_accepts_boundaries);
  RUN_TEST (test_from_bytes_round_trip);
  RUN_TEST (test_from_bytes_size_errors);
  TEST_MAIN_END ();
}
