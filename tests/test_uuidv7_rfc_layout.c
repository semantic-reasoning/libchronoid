/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * RFC 9562 §5.7 / §6.13 byte-layout pin. Vectors are synthesized from
 * the RFC text itself (including the published example in Appendix A.1)
 * with no dependency on any specific implementation's output. The
 * purpose is to anchor libchronoid's UUIDv7 byte layout against the
 * normative spec, independent of util-linux / google/uuid / PG18 lifecycles.
 *
 * Each vector is exercised four ways:
 *   1. chronoid_uuidv7_from_parts(...) -> 16 expected bytes (byte-pin)
 *   2. chronoid_uuidv7_format(out)     -> expected canonical lowercase string
 *   3. chronoid_uuidv7_parse(string)   -> 16 bytes equal vector 1
 *   4. chronoid_uuidv7_unix_ms / version / variant readback equal RFC values
 */
#include <chronoid/uuidv7.h>
#include "test_util.h"

/* Each vector specifies the RFC §5.7 inputs (unix_ms, rand_a 12-bit,
 * rand_b[8]) plus the expected 16-byte binary representation and the
 * expected 36-char canonical lowercase string.
 *
 * The expected bytes follow RFC 9562 §5.7 layout:
 *   bytes 0..5 = unix_ms big-endian (48 bits)
 *   byte    6  = 0x70 | ((rand_a >> 8) & 0x0F)
 *   byte    7  = rand_a & 0xFF
 *   byte    8  = 0x80 | (rand_b[0] & 0x3F)
 *   bytes 9..15= rand_b[1..7] verbatim
 *
 * The library is responsible for masking caller-supplied bits in
 * |rand_a| (high 4 bits) and |rand_b[0]| (top 2 bits) before overlaying
 * the version (0x7) and variant (0b10) nibbles. Vectors 5 and 6 below
 * stress that masking explicitly. */
typedef struct rfc_vector
{
  const char *name;
  int64_t unix_ms;
  uint16_t rand_a_12bit;        /* caller-supplied; library masks to 12 bits */
  uint8_t rand_b[8];            /* caller-supplied; library masks rand_b[0]'s
                                 * top 2 bits before overlaying variant */
  uint8_t expected_bytes[16];
  char expected_string[36];     /* canonical lowercase, NOT NUL-terminated */
} rfc_vector_t;

/* Vector 1: RFC 9562 Appendix A.1 / §6.13 published example.
 *
 *   017F22E2-79B0-7CC3-98C4-DC0C0C07398F
 *
 * Breakdown per the RFC:
 *   unix_ts_ms = 0x017F22E279B0 = 1645557742000 (2022-02-22 14:02:22.000 UTC)
 *   version    = 0x7
 *   rand_a     = 0xCC3 (12 bits)
 *   variant    = 0b10
 *   rand_b     = 62 bits whose serialized layout is bytes 8..15 of the
 *                UUID with the variant overlay in the top 2 bits of byte 8.
 *
 * To feed chronoid_uuidv7_from_parts we need the rand_b[8] inputs that
 * the library will mask + variant-overlay to produce bytes 8..15 of the
 * RFC vector. Bytes 8..15 of the published UUID are 98 C4 DC 0C 0C 07
 * 39 8F. The library sets byte 8 = 0x80 | (rand_b[0] & 0x3F), so we
 * pass rand_b[0] as 0x18 (the low 6 bits of 0x98 = 011000 = 0x18); any
 * value with the same low 6 bits would produce the same output, but
 * 0x18 keeps the input bytes minimal. rand_b[1..7] pass through
 * verbatim so we set them to the published bytes 9..15. */
static const rfc_vector_t kVectors[] = {
  {
        .name = "rfc9562 appendix A.1 published example",
        .unix_ms = (int64_t) 0x017F22E279B0LL,
        .rand_a_12bit = 0x0CC3,
        .rand_b = {0x18, 0xC4, 0xDC, 0x0C, 0x0C, 0x07, 0x39, 0x8F},
        .expected_bytes = {
              0x01, 0x7F, 0x22, 0xE2, 0x79, 0xB0, 0x7C, 0xC3,
            0x98, 0xC4, 0xDC, 0x0C, 0x0C, 0x07, 0x39, 0x8F},
        .expected_string = {
              '0', '1', '7', 'f', '2', '2', 'e', '2', '-',
              '7', '9', 'b', '0', '-',
              '7', 'c', 'c', '3', '-',
              '9', '8', 'c', '4', '-',
            'd', 'c', '0', 'c', '0', 'c', '0', '7', '3', '9', '8', 'f'},
      },
  {
        .name = "all-zero inputs",
        .unix_ms = 0,
        .rand_a_12bit = 0,
        .rand_b = {0, 0, 0, 0, 0, 0, 0, 0},
        .expected_bytes = {
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .expected_string = {
              '0', '0', '0', '0', '0', '0', '0', '0', '-',
              '0', '0', '0', '0', '-',
              '7', '0', '0', '0', '-',
              '8', '0', '0', '0', '-',
            '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0'},
      },
  {
        .name = "all-max inputs (within RFC ranges)",
        .unix_ms = (int64_t) ((1LL << 48) - 1),
        .rand_a_12bit = 0x0FFF,
        .rand_b = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .expected_bytes = {
              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF,
            0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .expected_string = {
              'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', '-',
              'f', 'f', 'f', 'f', '-',
              '7', 'f', 'f', 'f', '-',
              'b', 'f', 'f', 'f', '-',
            'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f'},
      },
  {
        .name = "boundary timestamp unix_ms = 1",
        .unix_ms = 1,
        .rand_a_12bit = 0x0123,
        .rand_b = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA},
        .expected_bytes = {
              0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x71, 0x23,
            0xB3, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA},
        .expected_string = {
              '0', '0', '0', '0', '0', '0', '0', '0', '-',
              '0', '0', '0', '1', '-',
              '7', '1', '2', '3', '-',
              'b', '3', '4', '4', '-',
            '5', '5', '6', '6', '7', '7', '8', '8', '9', '9', 'a', 'a'},
      },
  {
        /* Caller passes 0xFFFF for rand_a; the library MUST mask to 12 bits
         * and write the version nibble itself. With low 12 bits = 0x0FFF,
         * byte 6 = 0x7F, byte 7 = 0xFF -- regardless of the high 4 bits the
         * caller supplied. This vector PROVES the version nibble is
         * library-controlled. */
        .name = "version nibble masking: caller bits ignored",
        .unix_ms = (int64_t) 0x000123456789LL,
        .rand_a_12bit = (uint16_t) 0xFFFF,
        .rand_b = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
        .expected_bytes = {
              0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0x7F, 0xFF,
            0x80, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
        .expected_string = {
              '0', '0', '0', '1', '2', '3', '4', '5', '-',
              '6', '7', '8', '9', '-',
              '7', 'f', 'f', 'f', '-',
              '8', '0', '1', '1', '-',
            '2', '2', '3', '3', '4', '4', '5', '5', '6', '6', '7', '7'},
      },
  {
        /* rand_b[0] = 0xFF; the library must strip the high 2 bits and
         * overlay variant 0b10, yielding byte 8 = 0xBF. rand_b[1..7] pass
         * through verbatim. This vector PROVES the variant bits are
         * library-controlled. */
        .name = "variant bits masking: caller bits ignored",
        .unix_ms = (int64_t) 0x010000000000LL,
        .rand_a_12bit = 0x0456,
        .rand_b = {0xFF, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11},
        .expected_bytes = {
              0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x56,
            0xBF, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11},
        .expected_string = {
              '0', '1', '0', '0', '0', '0', '0', '0', '-',
              '0', '0', '0', '0', '-',
              '7', '4', '5', '6', '-',
              'b', 'f', 'a', 'a', '-',
            'b', 'b', 'c', 'c', 'd', 'd', 'e', 'e', 'f', 'f', '1', '1'},
      },
};

#define NUM_VECTORS (sizeof (kVectors) / sizeof (kVectors[0]))

static void
exercise_vector (const rfc_vector_t *v)
{
  fprintf (stderr, "  vector: %s\n", v->name);

  /* (1) byte-pin: chronoid_uuidv7_from_parts must produce exactly the
   * RFC-derived 16 bytes. */
  chronoid_uuidv7_t out;
  ASSERT_EQ_INT (chronoid_uuidv7_from_parts (&out, v->unix_ms,
          v->rand_a_12bit, v->rand_b), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (out.b, v->expected_bytes, CHRONOID_UUIDV7_BYTES);

  /* (2) canonical string-pin: chronoid_uuidv7_format must emit exactly
   * the lowercase hyphenated 36-char form (no NUL terminator). */
  char s[CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_format (&out, s);
  ASSERT_EQ_BYTES (s, v->expected_string, CHRONOID_UUIDV7_STRING_LEN);

  /* (3) parse round-trip: chronoid_uuidv7_parse must invert format
   * exactly. */
  chronoid_uuidv7_t parsed;
  ASSERT_EQ_INT (chronoid_uuidv7_parse (&parsed, v->expected_string,
          CHRONOID_UUIDV7_STRING_LEN), CHRONOID_UUIDV7_OK);
  ASSERT_EQ_BYTES (parsed.b, v->expected_bytes, CHRONOID_UUIDV7_BYTES);

  /* (4) field-accessor readback against RFC values. unix_ms must round-
   * trip the original 48-bit input, version must read 0x7, variant
   * must read 0b10 = 0x2. */
  ASSERT_EQ_INT (chronoid_uuidv7_unix_ms (&parsed), v->unix_ms);
  ASSERT_EQ_INT (chronoid_uuidv7_version (&parsed), 0x7);
  ASSERT_EQ_INT (chronoid_uuidv7_variant (&parsed), 0x2);
}

static void
test_rfc_layout_vectors (void)
{
  for (size_t i = 0; i < NUM_VECTORS; ++i) {
    exercise_vector (&kVectors[i]);
  }
}

int
main (void)
{
  RUN_TEST (test_rfc_layout_vectors);
  TEST_MAIN_END ();
}
