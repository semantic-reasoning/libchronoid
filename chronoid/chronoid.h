/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 *
 * Umbrella header. Pulls in every public surface of libchronoid so a
 * downstream consumer can write
 *
 *   #include <chronoid/chronoid.h>
 *
 * and have access to KSUID, UUIDv7, and the library version macros
 * without tracking individual format-specific includes. For TUs that
 * only need one format, prefer the narrower
 *
 *   #include <chronoid/ksuid.h>
 *   #include <chronoid/uuidv7.h>
 *
 * to keep compile units lean.
 *
 * libchronoid is the successor to segmentio/ksuid's C11 port libksuid
 * (https://github.com/semantic-reasoning/libksuid, archived). The
 * KSUID half of this library is wire-compatible with segmentio/ksuid
 * and inherits libksuid 1.0.0's algorithms verbatim. The UUIDv7 half
 * is a clean-room implementation per RFC 9562 (May 2024).
 */
#ifndef CHRONOID_H
#define CHRONOID_H

#include <chronoid/chronoid_version.h>
#include <chronoid/ksuid.h>
#include <chronoid/uuidv7.h>

#endif /* CHRONOID_H */
