/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef CHRONOID_RAND_H
#define CHRONOID_RAND_H

#include <stddef.h>
#include <stdint.h>

/* Fill |buf| with |n| cryptographically-secure random bytes obtained
 * from the operating system's entropy source. Returns 0 on success
 * and -1 on failure (no entropy source available). The implementation
 * tries, in order: getrandom(2), getentropy(3), BCryptGenRandom (on
 * Windows), and finally a /dev/urandom read. None of these fall back
 * to a non-cryptographic source: a failure here MUST propagate to the
 * caller -- silently producing zero or low-entropy bytes from an ID
 * library would be a worst-case correctness bug. */
int chronoid_os_random_bytes (uint8_t * buf, size_t n);

/* Fill |buf| with |n| random bytes from the per-thread CSPRNG
 * (ChaCha20 keyed from chronoid_os_random_bytes). State is _Thread_local
 * so concurrent calls from distinct threads need no synchronisation;
 * concurrent calls from the same thread are NOT supported. The state
 * is reseeded on first use, after fork(), if the wall clock moves
 * backwards or runs forward by more than an hour, and after every 1
 * MiB of keystream. Returns 0 on success and -1 if the underlying OS
 * entropy source is unavailable at the moment of (re)seed. */
int chronoid_random_bytes (uint8_t * buf, size_t n);

/* For testing: force the calling thread's CSPRNG state to reseed on
 * its next chronoid_random_bytes call. */
void chronoid_random_force_reseed (void);

/* Wall-clock time in milliseconds since the Unix epoch. Internal helper
 * used by chronoid_uuidv7_new / chronoid_uuidv7_sequence_next. The
 * implementation calls timespec_get(TIME_UTC) and combines tv_sec /
 * tv_nsec into ms; on clock_gettime failure it returns -1. Callers
 * surface that sentinel as CHRONOID_UUIDV7_ERR_TIME_RANGE.
 *
 * NOT exported from the shared library; the prototype lives here only
 * because rand_tls.c is the natural home for the wall-clock helper
 * (it already abstracts timespec_get for the reseed cadence) and
 * uuidv7_sequence.c plus uuidv7.c need to call it from a sibling TU. */
int64_t chronoid_now_ms (void);

/* Internal: fill |buf| with |n| bytes of random data, routing through
 * the chronoid_set_rand override if installed and otherwise through
 * the per-thread CSPRNG (chronoid_random_bytes). Returns 0 on success
 * and non-zero on failure. The function pointer + ctx slots backing
 * the override are private to ksuid.c (where chronoid_set_rand is
 * defined to honour the public ksuid.h ABI), so this helper exists
 * to give uuidv7.c / uuidv7_sequence.c access to the same override
 * without duplicating the override slots.
 *
 * The same override governs both formats: a single chronoid_set_rand
 * call routes both chronoid_ksuid_new and chronoid_uuidv7_new through
 * the supplied function. */
int chronoid_internal_fill_random (uint8_t *buf, size_t n);

/* Issue #4 thread-exit hook. Wipes the calling thread's CSPRNG
 * state in place via chronoid_explicit_bzero so the 64-byte ChaCha20
 * state and the 64-byte keystream window do not survive the thread
 * after it exits.
 *
 * Commit 1 of the issue #4 series provides the function body; the
 * platform-specific automatic registration (__cxa_thread_atexit_impl
 * on glibc / libc++abi / MUSL >= 1.2.0; FlsAlloc on Windows) lands
 * in commit 2. Without that registration the function is reachable
 * only via the test harness or a manual call from a downstream
 * caller. */
void chronoid_random_thread_state_wipe (void);

#ifdef CHRONOID_TESTING
/* Test-only hooks compiled into the test binary via -DCHRONOID_TESTING=1
 * (set per-test in tests/meson.build). They give tests/test_rand_tls.c
 * a way to drive the wipe path deterministically without depending on
 * thread-exit timing, and to peek at the post-wipe TLS state to prove
 * the bytes were actually zeroed. None of these symbols are exported
 * from the library; production builds compile without -DCHRONOID_TESTING
 * and never see the prototypes. */

/* Atomic counter incremented on every entry to
 * chronoid_random_thread_state_wipe. The test asserts it ticks when the
 * destructor runs at thread exit. */
extern _Atomic int chronoid_thread_exit_wipes_observed;

/* Fill the calling thread's TLS RNG state with a known sentinel
 * pattern (0xa5 throughout, including the seeded flag) so the next
 * call to chronoid_random_thread_state_wipe has something non-zero to
 * erase. Must be called before any draw on the same thread. */
void chronoid_random_thread_state_set_sentinel_for_testing (void);

/* Copy the calling thread's TLS RNG state bytes into |out| (which
 * must be at least sizeof(chronoid_tls_rng_t) -- the test is allowed to
 * over-allocate). Used to assert the wipe actually zeroed the
 * region. */
void chronoid_random_thread_state_peek_for_testing (uint8_t * out, size_t out_len);

/* Size in bytes that a peek buffer must accommodate. */
size_t chronoid_random_thread_state_size_for_testing (void);

/* Test-only: replace the wall-clock source used by chronoid_uuidv7_*
 * generation. Pass a non-NULL fn to install; pass NULL to restore the
 * default timespec_get-backed source. The override is process-global
 * and atomic-pointer-protected, so tests can swap it mid-flight
 * race-free; tests are responsible for restoring the default before
 * exiting. fn returns ms since the Unix epoch. The override must be
 * thread-safe if multiple threads exercise UUIDv7 generation
 * concurrently. */
typedef int64_t (*chronoid_time_source_fn) (void);
void chronoid_set_time_source_for_testing (chronoid_time_source_fn fn);
#endif

#endif /* CHRONOID_RAND_H */
