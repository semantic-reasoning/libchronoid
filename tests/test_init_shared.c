/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Companion to test_smoke.c that links against the libchronoid SHARED
 * library rather than the static archive. This is the Windows DLL
 * consumer scenario in miniature: CHRONOID_PUBLIC expands to
 * __declspec(dllimport) here (because CHRONOID_BUILDING is undefined for
 * this TU), and CHRONOID_KSUID_NIL / CHRONOID_KSUID_MAX are therefore *not* constant
 * expressions in this translation unit. If the build of this file
 * succeeds, the public KSUID_*_INIT macros really do work as static-
 * storage initialisers under the same constraints downstream
 * consumers will face. The test then memcmp's against the symbols
 * to prove the bytes match.
 *
 * This file exists because tests/test_smoke.c links the static
 * archive (CHRONOID_BUILDING is in scope, no dllimport, the symbol is a
 * normal const) and so does NOT exercise the very codepath issue #1
 * was filed about. */

#include <chronoid/ksuid.h>
#include <chronoid/uuidv7.h>
#include "test_util.h"

/* File-scope static storage from the macro form. On Windows DLL
 * consumers the equivalent `static const chronoid_ksuid_t g = CHRONOID_KSUID_NIL;`
 * fails with C2099 "initializer is not a constant" -- that is the
 * regression we are guarding against. */
static const chronoid_ksuid_t kSharedNilInit = CHRONOID_KSUID_NIL_INIT;
static const chronoid_ksuid_t kSharedMaxInit = CHRONOID_KSUID_MAX_INIT;
static const chronoid_uuidv7_t kSharedUuidv7NilInit = CHRONOID_UUIDV7_NIL_INIT;
static const chronoid_uuidv7_t kSharedUuidv7MaxInit = CHRONOID_UUIDV7_MAX_INIT;

static void
test_macros_at_static_storage_match_shared_symbols (void)
{
  /* If CHRONOID_KSUID_NIL / CHRONOID_KSUID_MAX really are dllimport on this build (the
   * Windows lane) the runtime symbol resolution lands here at
   * comparison time, after the file-scope statics above were already
   * frozen at load time from the macro values. */
  ASSERT_EQ_BYTES (kSharedNilInit.b, CHRONOID_KSUID_NIL.b,
      CHRONOID_KSUID_BYTES);
  ASSERT_EQ_BYTES (kSharedMaxInit.b, CHRONOID_KSUID_MAX.b,
      CHRONOID_KSUID_BYTES);
  ASSERT_TRUE (chronoid_ksuid_is_nil (&kSharedNilInit));

  /* Same regression guard for the UUIDv7 sentinels: with
   * CHRONOID_BUILDING undefined here, CHRONOID_UUIDV7_NIL /
   * CHRONOID_UUIDV7_MAX resolve via __declspec(dllimport) on Windows
   * and are NOT constant expressions in this TU. The macro form must
   * still work as a static-storage initializer and yield byte-for-byte
   * equal contents. */
  ASSERT_EQ_BYTES (kSharedUuidv7NilInit.b, CHRONOID_UUIDV7_NIL.b,
      CHRONOID_UUIDV7_BYTES);
  ASSERT_EQ_BYTES (kSharedUuidv7MaxInit.b, CHRONOID_UUIDV7_MAX.b,
      CHRONOID_UUIDV7_BYTES);
  ASSERT_TRUE (chronoid_uuidv7_is_nil (&kSharedUuidv7NilInit));
  ASSERT_FALSE (chronoid_uuidv7_is_nil (&kSharedUuidv7MaxInit));
}

int
main (void)
{
  RUN_TEST (test_macros_at_static_storage_match_shared_symbols);
  TEST_MAIN_END ();
}
