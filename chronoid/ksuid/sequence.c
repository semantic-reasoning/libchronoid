/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * Sequence: deterministic 65536-element ordered run from one seed.
 *
 * Derived from segmentio/ksuid sequence.go (MIT, Copyright (c) 2017
 * Segment.io). Same uint32 counter trick to detect uint16 overflow,
 * same big-endian write of the count into the last two payload bytes.
 */
#include <chronoid/ksuid.h>

#include <chronoid/byteorder.h>

_Static_assert (UINT16_MAX == 65535,
    "uint16 overflow guard expects 16-bit max");

static void
chronoid_ksuid_sequence_apply_count (chronoid_ksuid_t *id, uint16_t n)
{
  uint8_t *p = id->b + (CHRONOID_KSUID_BYTES - 2);
  p[0] = (uint8_t) (n >> 8);
  p[1] = (uint8_t) (n);
}

void
chronoid_ksuid_sequence_init (chronoid_ksuid_sequence_t *s,
    const chronoid_ksuid_t *seed)
{
  s->seed = *seed;
  s->count = 0;
}

chronoid_ksuid_err_t
chronoid_ksuid_sequence_next (chronoid_ksuid_sequence_t *s,
    chronoid_ksuid_t *out)
{
  if (s->count > UINT16_MAX)
    return CHRONOID_KSUID_ERR_EXHAUSTED;
  *out = s->seed;
  chronoid_ksuid_sequence_apply_count (out, (uint16_t) s->count);
  ++s->count;
  return CHRONOID_KSUID_OK;
}

void
chronoid_ksuid_sequence_bounds (const chronoid_ksuid_sequence_t *s,
    chronoid_ksuid_t *min, chronoid_ksuid_t *max)
{
  uint32_t lo = s->count;
  if (lo > UINT16_MAX)
    lo = UINT16_MAX;
  *min = s->seed;
  chronoid_ksuid_sequence_apply_count (min, (uint16_t) lo);
  *max = s->seed;
  chronoid_ksuid_sequence_apply_count (max, UINT16_MAX);
}
