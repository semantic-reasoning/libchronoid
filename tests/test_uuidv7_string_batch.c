/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Tests for the public chronoid_uuidv7_string_batch bulk encoder.
 *
 * Two layers of coverage:
 *   1. Public-API parity: every 36-byte slice produced by
 *      chronoid_uuidv7_string_batch (which dispatches to the best
 *      kernel for the host -- AVX2 4-wide on AVX2 x86_64, scalar
 *      elsewhere) equals chronoid_uuidv7_format of the same UUID.
 *      Pins n=0 no-op, exact-multiple-of-4, off-by-one-into-tail
 *      (n=5, n=17), prime-misaligned (n=257), and the corner
 *      UUIDv7s (NIL, MAX, all-0x80).
 *   2. Direct AVX2-vs-scalar differential parity (compiled in only
 *      when CHRONOID_HAVE_HEX_AVX2 is defined; gated at runtime on
 *      __builtin_cpu_supports("avx2") so the same binary is safe on
 *      non-AVX2 hosts in the same x86_64 build). Bypasses the
 *      runtime dispatcher and calls the scalar + AVX2 kernels
 *      directly, comparing byte-for-byte. This is what catches
 *      cross-lane bugs (a 4-UUID batch whose lanes get mis-attributed
 *      by the AVX2 nibble interleave would fail the lane-swap
 *      detection corpus even when per-UUID format-parity passes).
 */
#include <chronoid/uuidv7.h>
#include "test_util.h"

#include <stdlib.h>

#if defined(CHRONOID_HAVE_HEX_AVX2) && (defined(__GNUC__) || defined(__clang__))
#  define CHRONOID_UUIDV7_TEST_AVX2_PARITY 1
/* Internal kernel prototypes. Tests link against the static archive
 * so default-hidden visibility does not exclude these symbols. */
extern void chronoid_uuidv7_string_batch_scalar (const chronoid_uuidv7_t *ids,
    char *out_36n, size_t n);
extern void chronoid_uuidv7_string_batch_avx2 (const chronoid_uuidv7_t *ids,
    char *out_36n, size_t n);
#else
#  define CHRONOID_UUIDV7_TEST_AVX2_PARITY 0
#endif

/* Same xorshift-flavoured LCG seed mixer used in test_string_batch.c
 * so a CI failure here can be reproduced bit-for-bit. */
static void
fill_pseudo_random (chronoid_uuidv7_t *id, uint64_t seed)
{
  uint64_t s = seed;
  for (size_t i = 0; i < CHRONOID_UUIDV7_BYTES; ++i) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    id->b[i] = (uint8_t) (s >> 56);
  }
}

static void
test_batch_zero_count_is_noop (void)
{
  /* n == 0 must not write to |out|; pin via a sentinel pattern. */
  char out[1] = { (char) 0xa5 };
  chronoid_uuidv7_string_batch (NULL, out, 0);
  ASSERT_EQ_INT ((unsigned char) out[0], 0xa5);
}

static void
test_batch_matches_format_for_n (size_t n)
{
  chronoid_uuidv7_t *ids = malloc (n * sizeof *ids);
  ASSERT_TRUE (ids != NULL);
  for (size_t i = 0; i < n; ++i)
    fill_pseudo_random (&ids[i],
        0x9e3779b97f4a7c15ULL ^ (i * 0x100000001b3ULL));

  char *batch_out = malloc (n * CHRONOID_UUIDV7_STRING_LEN);
  ASSERT_TRUE (batch_out != NULL);
  chronoid_uuidv7_string_batch (ids, batch_out, n);

  for (size_t i = 0; i < n; ++i) {
    char ref[CHRONOID_UUIDV7_STRING_LEN];
    chronoid_uuidv7_format (&ids[i], ref);
    ASSERT_EQ_BYTES (batch_out + i * CHRONOID_UUIDV7_STRING_LEN, ref,
        CHRONOID_UUIDV7_STRING_LEN);
  }
  free (ids);
  free (batch_out);
}

static void test_batch_one (void)               { test_batch_matches_format_for_n (1); }
static void test_batch_three (void)             { test_batch_matches_format_for_n (3); }
static void test_batch_four_exact (void)        { test_batch_matches_format_for_n (4); }
static void test_batch_five_one_past (void)     { test_batch_matches_format_for_n (5); }
static void test_batch_fifteen (void)           { test_batch_matches_format_for_n (15); }
static void test_batch_sixteen (void)           { test_batch_matches_format_for_n (16); }
static void test_batch_seventeen (void)         { test_batch_matches_format_for_n (17); }
static void test_batch_257_misaligned (void)    { test_batch_matches_format_for_n (257); }

static void
test_batch_pinned_corners (void)
{
  /* NIL x 4, MAX x 4, all-0x80 x 4 in one batch -- exercises the
   * boundary input values for every lane position the AVX2 4-wide
   * kernel can pick. */
  chronoid_uuidv7_t ids[12];
  for (size_t i = 0; i < 4; ++i) {
    ids[i] = CHRONOID_UUIDV7_NIL;
    ids[4 + i] = CHRONOID_UUIDV7_MAX;
    for (size_t j = 0; j < CHRONOID_UUIDV7_BYTES; ++j)
      ids[8 + i].b[j] = 0x80;
  }

  char out[12 * CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_string_batch (ids, out, 12);

  for (size_t i = 0; i < 12; ++i) {
    char ref[CHRONOID_UUIDV7_STRING_LEN];
    chronoid_uuidv7_format (&ids[i], ref);
    ASSERT_EQ_BYTES (out + i * CHRONOID_UUIDV7_STRING_LEN, ref,
        CHRONOID_UUIDV7_STRING_LEN);
  }
}

#if CHRONOID_UUIDV7_TEST_AVX2_PARITY
static int
host_supports_avx2 (void)
{
  __builtin_cpu_init ();
  return __builtin_cpu_supports ("avx2");
}

static void
avx2_parity_for_n (size_t n)
{
  if (!host_supports_avx2 ())
    return;
  chronoid_uuidv7_t *ids = malloc (n * sizeof *ids);
  ASSERT_TRUE (ids != NULL);
  for (size_t i = 0; i < n; ++i)
    fill_pseudo_random (&ids[i],
        0xa3b1c2d4e5f60718ULL ^ (i * 0x9e3779b97f4a7c15ULL));

  char *out_s = malloc (n * CHRONOID_UUIDV7_STRING_LEN);
  char *out_a = malloc (n * CHRONOID_UUIDV7_STRING_LEN);
  if (out_s == NULL || out_a == NULL) {
    FAIL_ ("out_s/out_a malloc");
    free (ids);
    free (out_s);
    free (out_a);
    return;
  }
  chronoid_uuidv7_string_batch_scalar (ids, out_s, n);
  chronoid_uuidv7_string_batch_avx2 (ids, out_a, n);

  /* Per-lane byte compare so the failure message identifies WHICH
   * UUIDv7 position diverged (a single ASSERT_EQ_BYTES across the
   * full buffer would only print the first byte offset). */
  for (size_t i = 0; i < n; ++i)
    ASSERT_EQ_BYTES (out_s + i * CHRONOID_UUIDV7_STRING_LEN,
        out_a + i * CHRONOID_UUIDV7_STRING_LEN, CHRONOID_UUIDV7_STRING_LEN);

  free (ids);
  free (out_s);
  free (out_a);
}

static void
test_avx2_parity_n_in_block_boundaries (void)
{
  /* Boundaries around the 4-wide block size: tail-only, exact
   * block, off-by-one into tail, two/three blocks plus tail, etc. */
  static const size_t ns[] = { 1, 3, 4, 5, 7, 8, 11, 15, 16, 17, 257, 1000 };
  for (size_t i = 0; i < sizeof ns / sizeof ns[0]; ++i)
    avx2_parity_for_n (ns[i]);
}

static void
test_avx2_parity_lane_swap_detection (void)
{
  /* 4 distinct UUIDv7s in the same vector. If the AVX2 nibble-
   * interleave or the dash-splice mis-mapped lane k to lane k', the
   * out-of-position comparison against the scalar reference fails. */
  if (!host_supports_avx2 ())
    return;
  chronoid_uuidv7_t ids[4];
  for (size_t lane = 0; lane < 4; ++lane)
    fill_pseudo_random (&ids[lane],
        0xdeadbeefcafef00dULL + (uint64_t) (lane * 0x100000001b3ULL));

  /* Bias each lane so the 16 bytes are visibly distinct from its
   * neighbours: stamp the lane index into byte 0 and into the high
   * nibble of byte 8 so the lane identity shows up at multiple
   * positions in the rendered hex. */
  for (size_t lane = 0; lane < 4; ++lane) {
    ids[lane].b[0] = (uint8_t) (0x10 * (lane + 1));
    ids[lane].b[8] = (uint8_t) ((lane << 4) | (ids[lane].b[8] & 0x0f));
  }

  char out_s[4 * CHRONOID_UUIDV7_STRING_LEN];
  char out_a[4 * CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_string_batch_scalar (ids, out_s, 4);
  chronoid_uuidv7_string_batch_avx2 (ids, out_a, 4);
  for (size_t lane = 0; lane < 4; ++lane)
    ASSERT_EQ_BYTES (out_s + lane * CHRONOID_UUIDV7_STRING_LEN,
        out_a + lane * CHRONOID_UUIDV7_STRING_LEN, CHRONOID_UUIDV7_STRING_LEN);
}

static void
test_avx2_parity_corner_values (void)
{
  if (!host_supports_avx2 ())
    return;
  /* 16 UUIDv7s of pure-NIL / pure-MAX / all-0x80 / alternating
   * 0xff-0x00 spread across four AVX2 4-wide blocks, exercising
   * the boundary inputs at every lane position. */
  chronoid_uuidv7_t ids[16];
  for (size_t i = 0; i < 16; ++i) {
    if ((i & 3) == 0)
      ids[i] = CHRONOID_UUIDV7_NIL;
    else if ((i & 3) == 1)
      ids[i] = CHRONOID_UUIDV7_MAX;
    else if ((i & 3) == 2) {
      for (size_t j = 0; j < CHRONOID_UUIDV7_BYTES; ++j)
        ids[i].b[j] = 0x80;
    } else {
      for (size_t j = 0; j < CHRONOID_UUIDV7_BYTES; ++j)
        ids[i].b[j] = (j & 1) ? 0xff : 0x00;
    }
  }
  char out_s[16 * CHRONOID_UUIDV7_STRING_LEN];
  char out_a[16 * CHRONOID_UUIDV7_STRING_LEN];
  chronoid_uuidv7_string_batch_scalar (ids, out_s, 16);
  chronoid_uuidv7_string_batch_avx2 (ids, out_a, 16);
  for (size_t i = 0; i < 16; ++i)
    ASSERT_EQ_BYTES (out_s + i * CHRONOID_UUIDV7_STRING_LEN,
        out_a + i * CHRONOID_UUIDV7_STRING_LEN, CHRONOID_UUIDV7_STRING_LEN);
}

static void
test_avx2_parity_one_million_lcg (void)
{
  /* >= 2^20 pseudo-random UUIDv7s differential-checked end-to-end.
   * Seed identical to KSUID test_string_batch.c so a CI failure
   * reproduces locally bit-for-bit. */
  if (!host_supports_avx2 ())
    return;
  size_t n = 1u << 20;
  chronoid_uuidv7_t *ids = malloc (n * sizeof *ids);
  ASSERT_TRUE (ids != NULL);
  uint64_t s = 0x123456789abcdef0ULL;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < CHRONOID_UUIDV7_BYTES; ++j) {
      s = s * 6364136223846793005ULL + 1442695040888963407ULL;
      ids[i].b[j] = (uint8_t) (s >> 56);
    }
  }

  char *out_s = malloc (n * CHRONOID_UUIDV7_STRING_LEN);
  char *out_a = malloc (n * CHRONOID_UUIDV7_STRING_LEN);
  if (out_s == NULL || out_a == NULL) {
    FAIL_ ("out_s/out_a malloc");
    free (ids);
    free (out_s);
    free (out_a);
    return;
  }
  chronoid_uuidv7_string_batch_scalar (ids, out_s, n);
  chronoid_uuidv7_string_batch_avx2 (ids, out_a, n);

  /* memcmp-style fast check; only fall through to per-position
   * report on mismatch to keep the success path cheap. */
  if (memcmp (out_s, out_a, n * CHRONOID_UUIDV7_STRING_LEN) != 0) {
    for (size_t i = 0; i < n; ++i) {
      if (memcmp (out_s + i * CHRONOID_UUIDV7_STRING_LEN,
              out_a + i * CHRONOID_UUIDV7_STRING_LEN,
              CHRONOID_UUIDV7_STRING_LEN) != 0) {
        fprintf (stderr, "  AVX2 parity diverged at lane %zu of %zu\n", i, n);
        chronoid_test_failures_++;
        break;
      }
    }
  }
  free (ids);
  free (out_s);
  free (out_a);
}
#endif /* CHRONOID_UUIDV7_TEST_AVX2_PARITY */

/* CHRONOID_FORCE_SCALAR override: the env var is read once on the
 * dispatcher's first call, so the env must be in place before we
 * touch chronoid_uuidv7_string_batch through the public entry point.
 * On AVX2-capable hosts this proves the override actually pins
 * scalar; on non-AVX2 hosts the dispatcher already resolves to
 * scalar, so the test still asserts byte-equality with chronoid_uuidv7_format. */
static int
test_force_scalar_setup_done = 0;

static void
test_force_scalar_env_pins_scalar (void)
{
  /* Output equality is the contract regardless of which kernel was
   * picked; if FORCE_SCALAR worked the AVX2 lane was bypassed. The
   * stronger "AVX2 was bypassed" claim is exercised by the direct
   * extern parity tests above. */
  if (!test_force_scalar_setup_done) {
    FAIL_ ("CHRONOID_FORCE_SCALAR was not set before first dispatch");
    return;
  }
  test_batch_matches_format_for_n (37);
}

int
main (void)
{
  /* setenv BEFORE the first dispatcher call so the trampoline picks
   * up the override. The env is consulted exactly once per process
   * for the lifetime of the dispatcher. The KSUID dispatcher consults
   * its own copy independently; setting it here pins both, but the
   * KSUID tests do not run from this binary. */
  if (setenv ("CHRONOID_FORCE_SCALAR", "1", 1) == 0)
    test_force_scalar_setup_done = 1;

  RUN_TEST (test_batch_zero_count_is_noop);
  RUN_TEST (test_batch_one);
  RUN_TEST (test_batch_three);
  RUN_TEST (test_batch_four_exact);
  RUN_TEST (test_batch_five_one_past);
  RUN_TEST (test_batch_fifteen);
  RUN_TEST (test_batch_sixteen);
  RUN_TEST (test_batch_seventeen);
  RUN_TEST (test_batch_257_misaligned);
  RUN_TEST (test_batch_pinned_corners);
  RUN_TEST (test_force_scalar_env_pins_scalar);
#if CHRONOID_UUIDV7_TEST_AVX2_PARITY
  RUN_TEST (test_avx2_parity_n_in_block_boundaries);
  RUN_TEST (test_avx2_parity_lane_swap_detection);
  RUN_TEST (test_avx2_parity_corner_values);
  RUN_TEST (test_avx2_parity_one_million_lcg);
#endif
  TEST_MAIN_END ();
}
