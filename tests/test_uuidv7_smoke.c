/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 * UUIDv7 (RFC 9562) implementation. No upstream lineage.
 */
#include <chronoid/uuidv7.h>
#include "test_util.h"

/* File-scope statics initialised from the public macros. The point of
 * this test pattern is that exactly this codepath -- aggregate
 * initialisation at static storage from a public sentinel -- is what
 * fails to compile on Windows DLL with the symbol form (mirrors the
 * KSUID coverage in tests/test_smoke.c). If this TU compiles,
 * UUIDV7_*_INIT works as a constant expression on the target. The
 * runtime memcmp below proves the bytes are right. */
static const chronoid_uuidv7_t kStaticNilInit = CHRONOID_UUIDV7_NIL_INIT;
static const chronoid_uuidv7_t kStaticMaxInit = CHRONOID_UUIDV7_MAX_INIT;

static void
test_constants_have_expected_layout (void)
{
  ASSERT_EQ_INT (CHRONOID_UUIDV7_BYTES, 16);
  ASSERT_EQ_INT (CHRONOID_UUIDV7_STRING_LEN, 36);
  ASSERT_EQ_INT (sizeof (chronoid_uuidv7_t), CHRONOID_UUIDV7_BYTES);
}

static void
test_nil_is_all_zero (void)
{
  static const uint8_t zero[CHRONOID_UUIDV7_BYTES] = { 0 };
  ASSERT_EQ_BYTES (CHRONOID_UUIDV7_NIL.b, zero, CHRONOID_UUIDV7_BYTES);
  ASSERT_TRUE (chronoid_uuidv7_is_nil (&CHRONOID_UUIDV7_NIL));
}

static void
test_max_is_all_ff (void)
{
  uint8_t ff[CHRONOID_UUIDV7_BYTES];
  memset (ff, 0xff, sizeof ff);
  ASSERT_EQ_BYTES (CHRONOID_UUIDV7_MAX.b, ff, CHRONOID_UUIDV7_BYTES);
  ASSERT_FALSE (chronoid_uuidv7_is_nil (&CHRONOID_UUIDV7_MAX));
}

static void
test_compare_orders_lex (void)
{
  ASSERT_TRUE (chronoid_uuidv7_compare (&CHRONOID_UUIDV7_NIL, &CHRONOID_UUIDV7_MAX) < 0);
  ASSERT_TRUE (chronoid_uuidv7_compare (&CHRONOID_UUIDV7_MAX, &CHRONOID_UUIDV7_NIL) > 0);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&CHRONOID_UUIDV7_NIL, &CHRONOID_UUIDV7_NIL), 0);
  ASSERT_EQ_INT (chronoid_uuidv7_compare (&CHRONOID_UUIDV7_MAX, &CHRONOID_UUIDV7_MAX), 0);
}

static void
test_compare_first_byte_dominates (void)
{
  chronoid_uuidv7_t a = CHRONOID_UUIDV7_NIL, b = CHRONOID_UUIDV7_NIL;
  a.b[0] = 0x01;
  b.b[15] = 0xff;
  ASSERT_TRUE (chronoid_uuidv7_compare (&a, &b) > 0);
  ASSERT_TRUE (chronoid_uuidv7_compare (&b, &a) < 0);
}

static void
test_init_macros_match_symbols (void)
{
  /* File-scope statics: the codepath that fails on Windows DLL today.
   * Byte-for-byte parity with the runtime symbols proves the macros
   * encode the right constants. */
  ASSERT_EQ_BYTES (kStaticNilInit.b, CHRONOID_UUIDV7_NIL.b, CHRONOID_UUIDV7_BYTES);
  ASSERT_EQ_BYTES (kStaticMaxInit.b, CHRONOID_UUIDV7_MAX.b, CHRONOID_UUIDV7_BYTES);
  ASSERT_TRUE (chronoid_uuidv7_is_nil (&kStaticNilInit));

  /* Block-scope static storage: distinct codepath from file scope on
   * some compilers, worth pinning separately. */
  static const chronoid_uuidv7_t local_nil = CHRONOID_UUIDV7_NIL_INIT;
  static const chronoid_uuidv7_t local_max = CHRONOID_UUIDV7_MAX_INIT;
  ASSERT_TRUE (chronoid_uuidv7_is_nil (&local_nil));
  ASSERT_EQ_BYTES (local_max.b, CHRONOID_UUIDV7_MAX.b, CHRONOID_UUIDV7_BYTES);
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
  TEST_MAIN_END ();
}
