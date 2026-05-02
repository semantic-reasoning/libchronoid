/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <chronoid/ksuid.h>
#include "test_util.h"

/* File-scope statics initialised from the public macros. The point of
 * this test pattern is that exactly this codepath -- aggregate
 * initialisation at static storage from a public sentinel -- is what
 * fails to compile on Windows DLL with the symbol form (issue #1). If
 * this TU compiles, KSUID_*_INIT works as a constant expression on
 * the target. The runtime memcmp below proves the bytes are right. */
static const chronoid_ksuid_t kStaticNilInit = CHRONOID_KSUID_NIL_INIT;
static const chronoid_ksuid_t kStaticMaxInit = CHRONOID_KSUID_MAX_INIT;

static void
test_constants_have_expected_layout (void)
{
  ASSERT_EQ_INT (CHRONOID_KSUID_BYTES, 20);
  ASSERT_EQ_INT (CHRONOID_KSUID_STRING_LEN, 27);
  ASSERT_EQ_INT (CHRONOID_KSUID_PAYLOAD_LEN, 16);
  ASSERT_EQ_INT (CHRONOID_KSUID_EPOCH_SECONDS, 1400000000LL);
  ASSERT_EQ_INT (sizeof (chronoid_ksuid_t), CHRONOID_KSUID_BYTES);
}

static void
test_nil_is_all_zero (void)
{
  static const uint8_t zero[CHRONOID_KSUID_BYTES] = { 0 };
  ASSERT_EQ_BYTES (CHRONOID_KSUID_NIL.b, zero, CHRONOID_KSUID_BYTES);
  ASSERT_TRUE (chronoid_ksuid_is_nil (&CHRONOID_KSUID_NIL));
}

static void
test_max_is_all_ff (void)
{
  uint8_t ff[CHRONOID_KSUID_BYTES];
  memset (ff, 0xff, sizeof ff);
  ASSERT_EQ_BYTES (CHRONOID_KSUID_MAX.b, ff, CHRONOID_KSUID_BYTES);
  ASSERT_FALSE (chronoid_ksuid_is_nil (&CHRONOID_KSUID_MAX));
}

static void
test_compare_orders_lex (void)
{
  ASSERT_TRUE (chronoid_ksuid_compare (&CHRONOID_KSUID_NIL,
          &CHRONOID_KSUID_MAX) < 0);
  ASSERT_TRUE (chronoid_ksuid_compare (&CHRONOID_KSUID_MAX,
          &CHRONOID_KSUID_NIL) > 0);
  ASSERT_EQ_INT (chronoid_ksuid_compare (&CHRONOID_KSUID_NIL,
          &CHRONOID_KSUID_NIL), 0);
  ASSERT_EQ_INT (chronoid_ksuid_compare (&CHRONOID_KSUID_MAX,
          &CHRONOID_KSUID_MAX), 0);
}

static void
test_compare_first_byte_dominates (void)
{
  chronoid_ksuid_t a = CHRONOID_KSUID_NIL, b = CHRONOID_KSUID_NIL;
  a.b[0] = 0x01;
  b.b[19] = 0xff;
  ASSERT_TRUE (chronoid_ksuid_compare (&a, &b) > 0);
  ASSERT_TRUE (chronoid_ksuid_compare (&b, &a) < 0);
}

static void
test_init_macros_match_symbols (void)
{
  /* File-scope statics: the codepath that fails on Windows DLL today.
   * Byte-for-byte parity with the runtime symbols proves the macros
   * encode the right constants. */
  ASSERT_EQ_BYTES (kStaticNilInit.b, CHRONOID_KSUID_NIL.b,
      CHRONOID_KSUID_BYTES);
  ASSERT_EQ_BYTES (kStaticMaxInit.b, CHRONOID_KSUID_MAX.b,
      CHRONOID_KSUID_BYTES);
  ASSERT_TRUE (chronoid_ksuid_is_nil (&kStaticNilInit));

  /* Block-scope static storage: distinct codepath from file scope on
   * some compilers, worth pinning separately. */
  static const chronoid_ksuid_t local_nil = CHRONOID_KSUID_NIL_INIT;
  static const chronoid_ksuid_t local_max = CHRONOID_KSUID_MAX_INIT;
  ASSERT_TRUE (chronoid_ksuid_is_nil (&local_nil));
  ASSERT_EQ_BYTES (local_max.b, CHRONOID_KSUID_MAX.b, CHRONOID_KSUID_BYTES);
}

static void
test_version_macros_are_consistent (void)
{
  /* DELIBERATE SYNC POINT: these literal values must equal the
   * `version :` field in the top-level meson.build. The test exists
   * to prove that meson.project_version() flows through the
   * configure_file substitution into chronoid_version.h.in -- a
   * `>= 0` check would silently accept an empty @VERSION_MAJOR@
   * substitution that the C preprocessor turns into 0. A real
   * regression in the substitution chain therefore fails here.
   *
   * When you bump meson.build's project version you MUST update
   * these four asserts in the same commit. */
  ASSERT_EQ_INT (CHRONOID_VERSION_MAJOR, 1);
  ASSERT_EQ_INT (CHRONOID_VERSION_MINOR, 1);
  ASSERT_EQ_INT (CHRONOID_VERSION_PATCH, 0);
  ASSERT_EQ_STR (CHRONOID_VERSION_STRING, "1.1.0");

  /* The composite CHRONOID_VERSION must equal the documented
   * (MAJOR << 16) | (MINOR << 8) | PATCH layout for `#if
   * CHRONOID_VERSION >= ...` arithmetic to behave the way callers
   * expect. */
  int composed = (CHRONOID_VERSION_MAJOR << 16)
      | (CHRONOID_VERSION_MINOR << 8)
      | (CHRONOID_VERSION_PATCH);
  ASSERT_EQ_INT (CHRONOID_VERSION, composed);
}

int
main (void)
{
  RUN_TEST (test_constants_have_expected_layout);
  RUN_TEST (test_nil_is_all_zero);
  RUN_TEST (test_max_is_all_ff);
  RUN_TEST (test_compare_orders_lex);
  RUN_TEST (test_compare_first_byte_dominates);
  RUN_TEST (test_init_macros_match_symbols);
  RUN_TEST (test_version_macros_are_consistent);
  TEST_MAIN_END ();
}
