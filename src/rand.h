/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef KSUID_RAND_H
#define KSUID_RAND_H

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
int ksuid_os_random_bytes (uint8_t * buf, size_t n);

#endif /* KSUID_RAND_H */
