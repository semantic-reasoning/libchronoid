/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * libksuid -- pure C11 port of github.com/segmentio/ksuid.
 *
 * The KSUID specification, binary layout, base62 alphabet and encoding
 * scheme are derived from segmentio/ksuid (MIT, Copyright (c) 2017
 * Segment.io). See LICENSE.MIT and NOTICE in the project root.
 */
#ifndef KSUID_H
#define KSUID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(KSUID_BUILDING)
#    define KSUID_PUBLIC __declspec(dllexport)
#  else
#    define KSUID_PUBLIC __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define KSUID_PUBLIC __attribute__((visibility("default")))
#else
#  define KSUID_PUBLIC
#endif

/* --------------------------------------------------------------------------
 * Wire-format constants (compatible with segmentio/ksuid).
 * -------------------------------------------------------------------------- */

#define KSUID_BYTES          20    /* binary length                         */
#define KSUID_STRING_LEN     27    /* base62 string length (no NUL)         */
#define KSUID_PAYLOAD_LEN    16    /* random payload length                 */
#define KSUID_TIMESTAMP_LEN  4     /* big-endian uint32 prefix              */
#define KSUID_EPOCH_SECONDS  1400000000LL  /* 2014-05-13 16:53:20 UTC       */

typedef struct ksuid {
    uint8_t b[KSUID_BYTES];
} ksuid_t;

typedef enum ksuid_err {
    KSUID_OK                =  0,
    KSUID_ERR_SIZE          = -1, /* bad binary length                     */
    KSUID_ERR_STR_SIZE      = -2, /* bad string length                     */
    KSUID_ERR_STR_VALUE     = -3, /* string contains non-base62 / overflow */
    KSUID_ERR_PAYLOAD_SIZE  = -4, /* payload != KSUID_PAYLOAD_LEN          */
    KSUID_ERR_RNG           = -5, /* OS random source unavailable          */
    KSUID_ERR_EXHAUSTED     = -6, /* sequence exhausted                    */
    KSUID_ERR_TIME_RANGE    = -7  /* unix_seconds outside KSUID epoch range*/
} ksuid_err_t;

KSUID_PUBLIC extern const ksuid_t KSUID_NIL;
KSUID_PUBLIC extern const ksuid_t KSUID_MAX;

/* --------------------------------------------------------------------------
 * Predicates and ordering.
 * -------------------------------------------------------------------------- */

KSUID_PUBLIC bool ksuid_is_nil(const ksuid_t *id);

/* Lexicographic comparison over the full 20-byte representation, matching
 * the Go implementation's bytes.Compare semantics. Returns <0, 0, or >0. */
KSUID_PUBLIC int  ksuid_compare(const ksuid_t *a, const ksuid_t *b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KSUID_H */
