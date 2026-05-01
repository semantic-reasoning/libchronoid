/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * libchronoid -- C11 toolkit for time-ordered identifiers.
 * UUIDv7 monotonic sequence (RFC 9562 §6.2 method 1). No upstream lineage.
 *
 * RFC 9562 §6.2 method 1 ("Fixed-Length Dedicated Counter Bits"):
 *   - 12 bits of the rand_a field act as a sub-millisecond monotonic
 *     counter shared across emits within the same observed ms;
 *   - the counter is seeded with a random value at every real ms-tick
 *     so consecutive runs sharing a starting ms are not predictable
 *     from each other's tails;
 *   - on counter overflow within a single ms (option (a) of the RFC)
 *     the embedded timestamp is bumped by 1 ms and the counter is
 *     reseeded -- the sequence NEVER returns "exhausted";
 *   - if the wall clock moves backwards (NTP step, VM resume) the
 *     last emitted ms is reused so monotonicity holds across the
 *     time warp.
 *
 * One sequence per thread; concurrent calls on a single instance are
 * undefined.
 */
#include <chronoid/uuidv7.h>

#include <string.h>

#include <chronoid/rand.h>

/* Same upper bound as chronoid_uuidv7_from_parts; defined here as a
 * private const so we don't need to expose the constant from
 * uuidv7.c. */
#define UUIDV7_MAX_UNIX_MS_INTERNAL ((int64_t) ((1LL << 48) - 1))

/* Mask the caller's 12-bit counter into a uint16_t with the version
 * nibble already in place at bits 12-15 (high 4 bits of byte 6). */
#define UUIDV7_COUNTER_MASK 0x0FFFu

static chronoid_uuidv7_err_t
chronoid_uuidv7_sequence_draw_random (uint8_t *buf, size_t n)
{
  if (chronoid_internal_fill_random (buf, n) != 0)
    return CHRONOID_UUIDV7_ERR_RNG;
  return CHRONOID_UUIDV7_OK;
}

/* Draw a fresh 12-bit counter from the CSPRNG (already masked). */
static chronoid_uuidv7_err_t
chronoid_uuidv7_sequence_draw_counter (uint16_t *out)
{
  uint8_t two[2];
  chronoid_uuidv7_err_t e =
      chronoid_uuidv7_sequence_draw_random (two, sizeof two);
  if (e != CHRONOID_UUIDV7_OK)
    return e;
  *out = (uint16_t) ((((uint16_t) two[0] << 8) | two[1]) & UUIDV7_COUNTER_MASK);
  return CHRONOID_UUIDV7_OK;
}

chronoid_uuidv7_err_t
chronoid_uuidv7_sequence_init (chronoid_uuidv7_sequence_t *s)
{
  /* Build into a stack temp so a partial RNG failure cannot leave
   * |*s| half-initialised. The "untouched on failure" guarantee
   * matches the rest of the chronoid_uuidv7_* API. */
  chronoid_uuidv7_sequence_t tmp;

  /* INT64_MIN-class sentinel would force wall_ms > tmp.last_ms on the
   * first _next call so the clock-backward clamp degenerates to "use
   * wall_ms"; 0 is also fine because real wall clocks always exceed
   * 0 by many decades. Use 0 for clarity / debugger readability. */
  tmp.last_ms = 0;

  uint16_t counter;
  chronoid_uuidv7_err_t e = chronoid_uuidv7_sequence_draw_counter (&counter);
  if (e != CHRONOID_UUIDV7_OK)
    return e;
  tmp.counter = counter;

  e = chronoid_uuidv7_sequence_draw_random (tmp.rand_b, sizeof tmp.rand_b);
  if (e != CHRONOID_UUIDV7_OK)
    return e;

  *s = tmp;
  return CHRONOID_UUIDV7_OK;
}

/* Compose the 16-byte UUIDv7 wire-format from |unix_ms|, the 12-bit
 * |counter|, and the 8-byte |rand_b|. The version nibble (0x7) and
 * the variant bits (0b10) are forced. Mirror of chronoid_uuidv7_from_parts'
 * byte layout but inlined here so the sequence path doesn't pay for the
 * up-front time-range check (already validated by the caller). */
static void
chronoid_uuidv7_sequence_emit (chronoid_uuidv7_t *out,
    int64_t unix_ms, uint16_t counter, const uint8_t rand_b[8])
{
  uint64_t ms = (uint64_t) unix_ms;
  out->b[0] = (uint8_t) (ms >> 40);
  out->b[1] = (uint8_t) (ms >> 32);
  out->b[2] = (uint8_t) (ms >> 24);
  out->b[3] = (uint8_t) (ms >> 16);
  out->b[4] = (uint8_t) (ms >> 8);
  out->b[5] = (uint8_t) (ms);

  uint16_t c = counter & UUIDV7_COUNTER_MASK;
  out->b[6] = (uint8_t) (0x70 | ((c >> 8) & 0x0F));
  out->b[7] = (uint8_t) (c & 0xFF);

  out->b[8] = (uint8_t) (0x80 | (rand_b[0] & 0x3F));
  memcpy (out->b + 9, rand_b + 1, 7);
}

chronoid_uuidv7_err_t
chronoid_uuidv7_sequence_next (chronoid_uuidv7_sequence_t *s,
    chronoid_uuidv7_t *out)
{
  /* Read the wall clock through the (possibly test-overridden) source.
   * chronoid_now_ms returns -1 on clock failure; treat that as
   * "no forward progress observed" and clamp to s->last_ms below.
   * Negative or out-of-48-bit-range times also clamp -- the embedded
   * timestamp must fit in 48 bits regardless of what the test source
   * returns. */
  int64_t wall_ms = chronoid_now_ms ();

  /* RFC 9562 §6.2 monotonicity guarantee R4.2: never let the embedded
   * timestamp go backwards relative to a previously-emitted ID from
   * this sequence. If wall < last we reuse last. wall == -1 (clock
   * failure) and any out-of-range wall_ms also fall through to
   * last_ms; the sequence will re-track real time once the clock
   * reports something sane and >= last_ms again. */
  int64_t effective_ms = wall_ms;
  if (effective_ms < s->last_ms || effective_ms > UUIDV7_MAX_UNIX_MS_INTERNAL)
    effective_ms = s->last_ms;

  /* If the resolved effective_ms is still negative (e.g. brand-new
   * sequence + clock failure), bail with TIME_RANGE rather than
   * forging a UUID with a negative ms. last_ms = 0 from init plus
   * a successful clock read of 0+ keeps this branch unreachable in
   * practice; it exists for defence in depth. */
  if (effective_ms < 0 || effective_ms > UUIDV7_MAX_UNIX_MS_INTERNAL)
    return CHRONOID_UUIDV7_ERR_TIME_RANGE;

  if (effective_ms > s->last_ms) {
    /* Real ms-tick: redraw both rand_b and the 12-bit counter from
     * the CSPRNG (R4.3 -- new ms ⇒ new tail). Build into stack temps
     * so |*s| stays untouched on RNG failure. */
    uint8_t new_rand_b[8];
    chronoid_uuidv7_err_t e =
        chronoid_uuidv7_sequence_draw_random (new_rand_b, sizeof new_rand_b);
    if (e != CHRONOID_UUIDV7_OK)
      return e;

    uint16_t new_counter;
    e = chronoid_uuidv7_sequence_draw_counter (&new_counter);
    if (e != CHRONOID_UUIDV7_OK)
      return e;

    s->last_ms = effective_ms;
    s->counter = new_counter;
    memcpy (s->rand_b, new_rand_b, sizeof new_rand_b);
  } else {
    /* Same observed ms (or clamped to it): increment the counter.
     * If it would overflow 0xFFF, RFC option (a) -- bump the
     * timestamp and reseed the counter from the CSPRNG. The
     * synthesised ms is conceptually a new tick so the rand_b
     * tail is also redrawn (otherwise an attacker observing two
     * IDs spanning the rollover learns nothing new about rand_b's
     * entropy; redrawing is cheap and matches "new ms ⇒ new tail"
     * semantics).
     *
     * Loop on overflow: in pathological cases the synthesised
     * counter could itself collide if every redraw happens to land
     * outside [0, 0xFFE], but masking to 12 bits keeps it in
     * [0, 0xFFF] and the next call simply increments from there.
     * No unbounded loop here: at most one bump per _next call. */
    uint32_t bumped = (uint32_t) s->counter + 1u;
    if (bumped > UUIDV7_COUNTER_MASK) {
      /* Counter rollover -- promote to the next ms. Guard against
       * walking past the 48-bit range. */
      if (s->last_ms >= UUIDV7_MAX_UNIX_MS_INTERNAL)
        return CHRONOID_UUIDV7_ERR_TIME_RANGE;

      uint8_t new_rand_b[8];
      chronoid_uuidv7_err_t e =
          chronoid_uuidv7_sequence_draw_random (new_rand_b, sizeof new_rand_b);
      if (e != CHRONOID_UUIDV7_OK)
        return e;

      uint16_t new_counter;
      e = chronoid_uuidv7_sequence_draw_counter (&new_counter);
      if (e != CHRONOID_UUIDV7_OK)
        return e;

      s->last_ms = s->last_ms + 1;
      s->counter = new_counter;
      memcpy (s->rand_b, new_rand_b, sizeof new_rand_b);
    } else {
      /* Plain counter increment within the same ms. rand_b stays --
       * the per-emit randomness is the counter, not the tail.
       * RFC 9562 §6.2 method 1 is explicit about this: the rand_b
       * field is drawn fresh on every ms-tick, not on every emit. */
      s->counter = (uint16_t) bumped;
      /* s->last_ms unchanged */
    }
  }

  chronoid_uuidv7_sequence_emit (out, s->last_ms, s->counter, s->rand_b);
  return CHRONOID_UUIDV7_OK;
}

void
chronoid_uuidv7_sequence_bounds (const chronoid_uuidv7_sequence_t *s,
    chronoid_uuidv7_t *min_out, chronoid_uuidv7_t *max_out)
{
  /* The next chronoid_uuidv7_sequence_next call within the same observed
   * ms emits with counter = (s->counter + 1) and an unobserved
   * rand_b. Mirror chronoid_ksuid_sequence_bounds' shape: predict the
   * deterministic component (counter increment) and span rand_b across
   * its full 62-bit range.
   *
   * Counter saturation: if s->counter is already 0xFFF, the next emit
   * is an overflow path that bumps the timestamp -- still emits at
   * (last_ms + 1, fresh counter, fresh rand_b) which is
   * lexicographically greater than anything in the same-ms band. The
   * "max" sentinel here clamps the in-band envelope at counter=0xFFF
   * to match the documented contract: bounds describe the CURRENT
   * ms's emit space, not the post-rollover space. */
  uint32_t lo_counter = (uint32_t) s->counter + 1u;
  if (lo_counter > UUIDV7_COUNTER_MASK)
    lo_counter = UUIDV7_COUNTER_MASK;

  uint8_t rand_b_min[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t rand_b_max[8] = {
    /* Top 2 bits of byte 0 are forced to the 0b10 variant by emit;
     * the 6 bottom bits + the remaining 7 bytes can max out at 0xFF.
     * emit() OR's the variant in, so passing rand_b[0]=0xFF here
     * still resolves to 0xBF in the wire byte 8 (0x80 | 0x3F). */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };

  chronoid_uuidv7_sequence_emit (min_out, s->last_ms,
      (uint16_t) lo_counter, rand_b_min);
  chronoid_uuidv7_sequence_emit (max_out, s->last_ms,
      (uint16_t) UUIDV7_COUNTER_MASK, rand_b_max);
}
