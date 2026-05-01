/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Coverage for chronoid_uuidv7_format / chronoid_uuidv7_parse: golden
 * vector, lowercase invariant, length / hyphen / hex validation,
 * untouched-on-failure, and the round-trip with a populated unix_ms
 * field that exercises bytes 0..5 only (R3.6).
 */
#include <chronoid/uuidv7.h>
#include "test_util.h"

/* Known 16-byte vector with version nibble 0x7 (high nibble of byte 6
 * = 0x7) and variant 0b10 (top two bits of byte 8 = 0x80). The
 * canonical text below is therefore a syntactically valid UUIDv7. */
static const uint8_t kSampleBytes[CHRONOID_UUIDV7_BYTES] = {
  0x01, 0x8c, 0xc2, 0x51, 0xf4, 0x00, 0x7c, 0x00,
  0x80, 0xc0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
};

static const char *const kSampleStr = "018cc251-f400-7c00-80c0-112233445566";
static const char *const kSampleStrUpper = "018CC251-F400-7C00-80C0-112233445566";

static void
test_format_known_vector (void)
{
  chronoid_uuidv7_t id;
  ASSERT_EQ_INT (chronoid_uuidv7_from_bytes (&id, kSampleBytes,
          CHRONOID_UUIDV7_BYTES), CHRONOID_UUIDV7_OK);

  char out[CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_format (&id, out);
  ASSERT_EQ_STRN (out, kSampleStr, CHRONOID_UUIDV7_STRING_LEN);

  /* Implicitly pin the version + variant nibble positions: chars 14
   * (version high nibble) must be '7', and char 19 (variant high
   * nibble) must be '8' (top bits 0b10 -> 0x8 or 0x9 hex digit). */
  ASSERT_EQ_INT (out[14], '7');
  ASSERT_TRUE (out[19] == '8' || out[19] == '9'
      || out[19] == 'a' || out[19] == 'b');
}

static void
test_format_roundtrip (void)
{
  /* Build via from_parts so the canonical version/variant nibbles
   * are written by the library; format -> parse -> compare. */
  static const uint8_t rand_b[8] = {
    0xa5, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
  };
  chronoid_uuidv7_t in;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&in,
          (int64_t) 0x123456789012LL, 0x0abc, rand_b),
      CHRONOID_UUIDV7_OK);

  char str[CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_format (&in, str);

  chronoid_uuidv7_t round;
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&round, str,
          CHRONOID_UUIDV7_STRING_LEN), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (round.b, in.b, CHRONOID_UUIDV7_BYTES);
}

static void
test_format_never_emits_uppercase (void)
{
  /* Feed all-0xff so every alpha position is forced to 'f'. Catches
   * an accidental %X / strtoupper. */
  char out[CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_format (&CHRONOID_UUIDV7_MAX, out);
  for (size_t i = 0; i < CHRONOID_UUIDV7_STRING_LEN; ++i) {
    char c = out[i];
    /* Hyphens at the four canonical offsets, otherwise hex. */
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      ASSERT_EQ_INT (c, '-');
    } else {
      ASSERT_TRUE ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
      ASSERT_FALSE (c >= 'A' && c <= 'F');
    }
  }
}

static void
test_format_does_not_overrun (void)
{
  /* 37-byte buffer with sentinel at index 36 -- chronoid_uuidv7_format
   * promises a 36-byte write only, so the sentinel must survive. */
  char out[CHRONOID_UUIDV7_STRING_LEN + 1];
  out[CHRONOID_UUIDV7_STRING_LEN] = (char) 0xCC;
  chronoid_uuidv7_format (&CHRONOID_UUIDV7_MAX, out);
  ASSERT_EQ_INT ((unsigned char) out[CHRONOID_UUIDV7_STRING_LEN], 0xCC);
}

static void
test_parse_case_insensitive (void)
{
  chronoid_uuidv7_t lo, hi;
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&lo, kSampleStr,
          CHRONOID_UUIDV7_STRING_LEN), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&hi, kSampleStrUpper,
          CHRONOID_UUIDV7_STRING_LEN), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (lo.b, hi.b, CHRONOID_UUIDV7_BYTES);
  ASSERT_EQ_BYTES (lo.b, kSampleBytes, CHRONOID_UUIDV7_BYTES);
}

static void
test_parse_size_errors (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, kSampleStr, 0),
      CHRONOID_UUIDV7_ERR_STR_SIZE);
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, kSampleStr, 35),
      CHRONOID_UUIDV7_ERR_STR_SIZE);
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, kSampleStr, 37),
      CHRONOID_UUIDV7_ERR_STR_SIZE);
  /* On STR_SIZE the destination must remain MAX. */
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);
}

static void
test_parse_rejects_bad_hyphen_positions (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;

  /* All vectors below are EXACTLY 36 characters so the size check
   * cannot mask the position-of-hyphen check. Build them via memcpy
   * mutation of the canonical sample so the only deviation in each
   * case is the hyphen positioning we want to exercise. */

  /* Hyphen at offset 7 (one position too early), hex at offset 8.
   * Form: "018cc25-1f400xxxxxxxxxxxxxxxxxxxxxxx" -- but we want the
   * remaining hyphens still valid and 36 chars total. Easiest: take
   * the canonical 36-char sample and swap chars 7 and 8 (a hex
   * digit and a hyphen). */
  char bad1[36];
  memcpy (bad1, kSampleStr, 36);
  /* Swap positions 7 and 8: kSampleStr[7] = '1', [8] = '-'. */
  bad1[7] = '-';
  bad1[8] = '1';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad1, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);

  /* Hyphen at offset 24 instead of 23. Mirror the above swap on the
   * last hyphen pair (positions 23 and 24). */
  char bad2[36];
  memcpy (bad2, kSampleStr, 36);
  /* kSampleStr[23] = '-', [24] = '1'. Swap to put the hyphen at 24. */
  bad2[23] = '1';
  bad2[24] = '-';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad2, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);

  /* A bare hex digit at hyphen position 8 (with the other three
   * hyphens still in place). 36 chars exactly. */
  char bad3[36];
  memcpy (bad3, kSampleStr, 36);
  bad3[8] = '0';                /* was '-', is now hex */
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad3, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);

  /* Missing hyphen at position 13: replace with a hex digit. */
  char bad4[36];
  memcpy (bad4, kSampleStr, 36);
  bad4[13] = 'a';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad4, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);

  /* Extra hyphen where a hex digit belongs -- position 0. 36 chars. */
  char bad5[36];
  memcpy (bad5, kSampleStr, 36);
  bad5[0] = '-';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad5, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);
}

static void
test_parse_rejects_non_hex (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;

  /* 'g' at position 0. */
  const char *bad1 = "g18cc251-f400-7c00-80c0-112233445566";
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad1, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  /* '!' at position 1. */
  const char *bad2 = "0!8cc251-f400-7c00-80c0-112233445566";
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad2, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  /* Interior NUL at position 2. memcmp / strn-style consumers must
   * not bail on the NUL: parse takes (s, len), no NUL terminator
   * required. The NUL is just an invalid hex char and must be
   * rejected as STR_VALUE. */
  char bad3[36];
  memcpy (bad3, kSampleStr, 36);
  bad3[2] = '\0';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad3, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);
}

static void
test_parse_rejects_whitespace (void)
{
  chronoid_uuidv7_t id = CHRONOID_UUIDV7_MAX;

  /* Leading space. Chars 0..35: space + first 35 chars of valid
   * sample. The hyphen-position check fires (position 8 of the
   * shifted string is '-' from kSampleStr's position 7, which is a
   * hex digit '1', NOT a hyphen). Either way, must reject. */
  char bad1[36];
  bad1[0] = ' ';
  memcpy (bad1 + 1, kSampleStr, 35);
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad1, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  /* Trailing space inside the 36-char window: replace last hex
   * position (35) with a space. */
  char bad2[36];
  memcpy (bad2, kSampleStr, 36);
  bad2[35] = ' ';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad2, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  /* Tab at a hex position. */
  char bad3[36];
  memcpy (bad3, kSampleStr, 36);
  bad3[5] = '\t';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad3, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);

  ASSERT_EQ_INT (chronoid_uuidv7_compare (&id, &CHRONOID_UUIDV7_MAX), 0);
}

static void
test_parse_failure_does_not_mutate_out (void)
{
  /* Pre-fill |out| with MAX and confirm every malformed parse leaves
   * the buffer byte-identical. The decoder writes to a stack-local
   * temp internally and only memcpys on full-success, so *every*
   * failure path -- size, hyphen, hex -- must preserve |out|. */
  chronoid_uuidv7_t pre = CHRONOID_UUIDV7_MAX;
  chronoid_uuidv7_t id = pre;

  /* Wrong length. */
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, kSampleStr, 35),
      CHRONOID_UUIDV7_ERR_STR_SIZE);
  ASSERT_EQ_BYTES (id.b, pre.b, CHRONOID_UUIDV7_BYTES);

  /* Bad hyphen: hex digit at hyphen position 8, exactly 36 chars. */
  char bad_hyphen[36];
  memcpy (bad_hyphen, kSampleStr, 36);
  bad_hyphen[8] = '0';
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad_hyphen, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_BYTES (id.b, pre.b, CHRONOID_UUIDV7_BYTES);

  /* Bad hex digit (G at position 0). */
  const char *bad_hex = "G18cc251-f400-7c00-80c0-112233445566";
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&id, bad_hex, 36),
      CHRONOID_UUIDV7_ERR_STR_VALUE);
  ASSERT_EQ_BYTES (id.b, pre.b, CHRONOID_UUIDV7_BYTES);
}

static void
test_unix_ms_reads_only_first_six_bytes (void)
{
  /* R3.6: chronoid_uuidv7_unix_ms must NOT read into byte 6 (the
   * version nibble). Build a UUIDv7 via from_parts with a known
   * 48-bit ms, format it, parse it back, and confirm unix_ms still
   * returns the original value -- not a value contaminated by 0x7
   * leaking up from byte 6. */
  static const uint8_t rand_b[8] = {
    0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88
  };
  const int64_t kMs = (int64_t) 0x123456789012LL;
  chronoid_uuidv7_t in;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&in, kMs, 0x0fff, rand_b),
      CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&in), kMs);

  char str[CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_format (&in, str);

  chronoid_uuidv7_t out;
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&out, str,
          CHRONOID_UUIDV7_STRING_LEN), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&out), kMs);
  /* And the version nibble survived the round trip. */
  ASSERT_EQ_INT (chronoid_uuidv7_version (&out), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&out), 0x2);
}

int
main (void)
{
  RUN_TEST (test_format_known_vector);
  RUN_TEST (test_format_roundtrip);
  RUN_TEST (test_format_never_emits_uppercase);
  RUN_TEST (test_format_does_not_overrun);
  RUN_TEST (test_parse_case_insensitive);
  RUN_TEST (test_parse_size_errors);
  RUN_TEST (test_parse_rejects_bad_hyphen_positions);
  RUN_TEST (test_parse_rejects_non_hex);
  RUN_TEST (test_parse_rejects_whitespace);
  RUN_TEST (test_parse_failure_does_not_mutate_out);
  RUN_TEST (test_unix_ms_reads_only_first_six_bytes);
  TEST_MAIN_END ();
}
