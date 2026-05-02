/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * chronoid_uuidv7_string_batch dispatcher and scalar reference. The AVX2
 * 4-wide kernel lives in hex_avx2.c (compiled only when meson
 * detects an x86_64 host with -Davx2_batch enabled, default auto).
 *
 * Structurally a mirror of chronoid/ksuid/encode_batch.c -- same
 * trampoline-as-initial-pointer idiom, same CHRONOID_FORCE_SCALAR
 * env-var override, same atomic acquire/release on the resolved
 * pointer. Two dispatchers, ONE shared environment variable, each
 * read once at first dispatch.
 */
#include <chronoid/uuidv7/hex_batch.h>

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
/* See chronoid/ksuid/encode_batch.c for the clang-cl rationale: it
 * defines both __clang__ and _MSC_VER but lld-link cannot resolve
 * __builtin_cpu_supports's compiler-rt symbols, so route it through
 * the MSVC __cpuidex path. Issue #12. */
#  if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#    include <cpuid.h>
#  elif defined(_MSC_VER)
#    include <intrin.h>
#  endif
#endif

void
chronoid_uuidv7_string_batch_scalar (const chronoid_uuidv7_t *ids,
    char *out_36n, size_t n)
{
  /* Plain per-UUID loop calling the existing scalar formatter (which
   * itself fans out to the SSSE3 single-UUID kernel on x86_64 via
   * chronoid_hex_encode_lower / hex_simd.h). The compiler can inline
   * the per-UUID hex encode but the outer loop is the reference
   * shape regardless of host. */
  for (size_t i = 0; i < n; ++i)
    chronoid_uuidv7_format (&ids[i], out_36n + i * CHRONOID_UUIDV7_STRING_LEN);
}

#if defined(CHRONOID_HAVE_HEX_AVX2)

/* CPUID-based AVX2 detection. Same belt-and-braces shape as
 * chronoid_ksuid_cpu_supports_avx2 in encode_batch.c -- glibc's
 * __builtin_cpu_supports already verifies XGETBV bit 2 on recent
 * versions, but the redundant explicit leaf-7/EBX bit-5 check costs
 * essentially nothing and survives a hypothetical future libc
 * regression that drops the OS-state guarantee. */
static int
chronoid_uuidv7_cpu_supports_avx2 (void)
{
#  if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
  __builtin_cpu_init ();
  if (!__builtin_cpu_supports ("avx2"))
    return 0;
  unsigned eax, ebx, ecx, edx;
  if (__get_cpuid_count (7, 0, &eax, &ebx, &ecx, &edx) == 0)
    return 0;
  return (ebx & (1u << 5)) != 0;
#  elif defined(_MSC_VER)
  int regs[4];
  __cpuidex (regs, 7, 0);
  if ((regs[1] & (1 << 5)) == 0)
    return 0;
  unsigned long long xcr = _xgetbv (0);
  return (xcr & 0x6) == 0x6;
#  else
  return 0;
#  endif
}

#endif /* CHRONOID_HAVE_HEX_AVX2 */

static void
chronoid_uuidv7_string_batch_init_trampoline (const chronoid_uuidv7_t * ids,
    char *out_36n, size_t n);

/* _Atomic-qualified pointer, not _Atomic(T) shorthand -- the latter
 * confuses gst-indent (it parses _Atomic(T) as a function call). */
static _Atomic chronoid_uuidv7_string_batch_fn g_hex_batch_impl =
    &chronoid_uuidv7_string_batch_init_trampoline;

/* CHRONOID_FORCE_SCALAR override. Reading getenv on the first dispatch
 * only is safe -- the resolved pointer is cached for the lifetime of
 * the process and the env var is consulted exactly once per dispatcher
 * (so two getenv calls per process across the KSUID and UUIDv7
 * dispatchers, both at first use). The override exists so production
 * deployments can pin the scalar path at startup if a future
 * regression in the AVX2 kernel is discovered after rollout, without
 * rebuilding the library.
 *
 * Recognised values: any non-empty, non-"0", non-"false" string
 * disables the AVX2 kernel. NULL or unset = use the best kernel
 * available on the host. */
static int
chronoid_uuidv7_force_scalar_env (void)
{
  const char *v = getenv ("CHRONOID_FORCE_SCALAR");
  if (v == NULL || v[0] == '\0')
    return 0;
  if (strcmp (v, "0") == 0 || strcmp (v, "false") == 0
      || strcmp (v, "FALSE") == 0)
    return 0;
  return 1;
}

static void
chronoid_uuidv7_string_batch_init_trampoline (const chronoid_uuidv7_t *ids,
    char *out_36n, size_t n)
{
  chronoid_uuidv7_string_batch_fn resolved =
      &chronoid_uuidv7_string_batch_scalar;
#if defined(CHRONOID_HAVE_HEX_AVX2)
  if (!chronoid_uuidv7_force_scalar_env ()
      && chronoid_uuidv7_cpu_supports_avx2 ())
    resolved = &chronoid_uuidv7_string_batch_avx2;
#else
  (void) chronoid_uuidv7_force_scalar_env;      /* silence unused-static warning */
#endif
  atomic_store_explicit (&g_hex_batch_impl, resolved, memory_order_release);
  resolved (ids, out_36n, n);
}

void
chronoid_uuidv7_string_batch (const chronoid_uuidv7_t *ids, char *out_36n,
    size_t n)
{
  /* The n == 0 early-out lives here, before the dispatch indirect
   * call, so callers passing 0 don't pay for the atomic load. */
  if (n == 0)
    return;
  chronoid_uuidv7_string_batch_fn f =
      atomic_load_explicit (&g_hex_batch_impl, memory_order_acquire);
  f (ids, out_36n, n);
}
