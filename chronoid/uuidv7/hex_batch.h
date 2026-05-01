/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Internal declarations for chronoid_uuidv7_string_batch and the dispatch
 * scaffolding it sits on. Public callers go through
 * chronoid/uuidv7.h's CHRONOID_PUBLIC chronoid_uuidv7_string_batch.
 *
 * Mirrors the pattern in chronoid/ksuid/encode_batch.h: a single
 * _Atomic function pointer is initialised at load time to a "trampoline"
 * that, on first call, runs feature detection (CPUID on x86_64),
 * atomic-stores the resolved kernel pointer, and tail-calls it.
 * Idempotent: if N threads hit the trampoline concurrently, each
 * performs detection (cheap, one CPUID) and each writes the same
 * pointer; loser stores are harmless. Subsequent calls take a single
 * acquire-load and an indirect call -- ~free vs the encode body.
 *
 * The CHRONOID_FORCE_SCALAR environment variable that gates the KSUID
 * batch dispatcher (chronoid/ksuid/encode_batch.c) is honoured here
 * too -- one env var, two dispatchers, each consults it once on its
 * own first-call init.
 */
#ifndef CHRONOID_UUIDV7_HEX_BATCH_H
#define CHRONOID_UUIDV7_HEX_BATCH_H

#include <stddef.h>

#include <chronoid/uuidv7.h>

typedef void (*chronoid_uuidv7_string_batch_fn) (const chronoid_uuidv7_t * ids,
    char *out_36n, size_t n);

/* Always-compiled scalar reference. Used by tests as the parity
 * baseline regardless of which production kernel is selected. */
void chronoid_uuidv7_string_batch_scalar (const chronoid_uuidv7_t * ids,
    char *out_36n, size_t n);

#if defined(CHRONOID_HAVE_HEX_AVX2)
/* AVX2 4-wide kernel. Linked in only when meson detects an x86_64
 * host with -Davx2_batch enabled (the same option also gates the
 * KSUID AVX2 batch kernel). Tail (n % 4) handled by falling
 * through to the scalar loop inside the kernel itself. */
void chronoid_uuidv7_string_batch_avx2 (const chronoid_uuidv7_t * ids,
    char *out_36n, size_t n);
#endif

#endif /* CHRONOID_UUIDV7_HEX_BATCH_H */
