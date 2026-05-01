/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * AVX2 4-wide chronoid_uuidv7_string_batch kernel.
 *
 * Strategy (Critic R6.1 -- the simpler path for the first AVX2 cut):
 *
 *   Outer loop processes 4 UUIDs per iteration. For each pair of
 *   UUIDs (32 input bytes -> 64 hex chars) we run the same nibble
 *   extract + PSHUFB-style ASCII LUT used by hex_ssse3.c, but lifted
 *   into a 256-bit register so a single VPSHUFB lookup converts 32
 *   nibbles to 32 ASCII chars in one shot. PSHUFB on YMM acts
 *   per-128-bit lane, which is exactly what we want here: the LUT
 *   is broadcast to both lanes and each input byte is mapped via
 *   its own lane's copy of the table.
 *
 *   The 64 hex chars produced by one VPSHUFB pair (after interleave)
 *   correspond to two consecutive UUIDs' contiguous hex
 *   representations. We then scalar-splice the four hyphens per
 *   UUID into the destination (8/13/18/23 within each 36-byte
 *   block).
 *
 * We pick "two UUIDs per AVX2 pass, two passes per outer iteration"
 * over "four UUIDs per AVX2 pass" because:
 *
 *   - Four 16-byte UUIDs = 64 input bytes = TWO YMM loads anyway,
 *     so the inner pshufb work would still be paired.
 *   - Doing two UUIDs per YMM keeps the punpckl/punpckh interleave
 *     pattern identical to the SSSE3 single-UUID kernel, so the
 *     correctness story carries over directly. A four-UUID-per-YMM
 *     in-lane permute variant is a follow-up optimisation; this
 *     commit deliberately ships the obviously-correct shape.
 *
 * Tail (n % 4): falls through to chronoid_uuidv7_string_batch_scalar
 * after the bulk loop. _mm256_zeroupper () is issued before the
 * scalar tail call AND before function return; both transitions
 * may land in non-VEX code (Intel SDM Vol 1 sec 14.1.2 -- a stale
 * upper-ymm causes a transition penalty when subsequent
 * SSE-encoded instructions execute on the same lane).
 *
 * __builtin_cpu_init / __builtin_cpu_supports stays in the
 * dispatcher (hex_batch.c) per Critic R6.3; this TU is only
 * compiled+linked when meson detects -mavx2 is available, and is
 * only entered through the resolved function pointer after the
 * runtime CPUID check has succeeded.
 */
#if defined(__x86_64__) || defined(_M_X64)
#  include <immintrin.h>
#  include <stddef.h>
#  include <stdint.h>
#  include <string.h>

#  include <chronoid/uuidv7.h>
#  include <chronoid/uuidv7/hex_batch.h>

/* Two-UUID pass: read 32 bytes (= two consecutive UUIDs), produce 64
 * contiguous lowercase hex chars in |out64| (32 chars per UUID, no
 * hyphens yet). The body is a lifted YMM version of hex_ssse3.c's
 * 16-byte path -- per-128-bit-lane PSHUFB does the nibble->ASCII LUT
 * for both UUIDs in one shot. */
static inline void
chronoid_uuidv7_hex32_avx2 (char out64[64], const uint8_t *in32)
{
  /* The lowercase hex LUT, broadcast to both 128-bit lanes of a YMM.
   * VPSHUFB on YMM is per-128-bit-lane, so each lane gets its own
   * copy of the 16-entry table. */
  static const uint8_t kHexLowerLut[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };

  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  __m256i v = _mm256_loadu_si256 ((const __m256i *) in32);
  /* Broadcast the 128-bit LUT to both YMM lanes. _mm_loadu_si128 +
   * _mm256_broadcastsi128_si256 is the canonical idiom. */
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  __m128i lut128 = _mm_loadu_si128 ((const __m128i *) kHexLowerLut);
  __m256i lut = _mm256_broadcastsi128_si256 (lut128);

  __m256i mask_low = _mm256_set1_epi8 (0x0F);
  /* AVX2 has no _mm256_srli_epi8; shift epi16 then AND, matching the
   * trick in hex_ssse3.c. The high four bits of each byte's high-
   * nibble lane become 0 after the AND, which is exactly what
   * VPSHUFB wants for an in-LUT index (0..15). */
  __m256i hi_nibbles = _mm256_and_si256 (_mm256_srli_epi16 (v, 4), mask_low);
  __m256i lo_nibbles = _mm256_and_si256 (v, mask_low);

  __m256i ascii_hi = _mm256_shuffle_epi8 (lut, hi_nibbles);
  __m256i ascii_lo = _mm256_shuffle_epi8 (lut, lo_nibbles);

  /* Per-128-bit-lane interleave. _mm256_unpacklo_epi8 on two YMMs
   * acts as parallel _mm_unpacklo_epi8 in each 128-bit lane, so the
   * low half of each UUID's hex output lands in the low 16 bytes of
   * its lane, and the high half lands in the high 16 bytes. The
   * concatenation in memory order is then:
   *   [UUID0 hex chars 0..31, UUID1 hex chars 0..31].
   * That's the hyphen-free 64-char layout we want. */
  __m256i lo_half = _mm256_unpacklo_epi8 (ascii_hi, ascii_lo);
  __m256i hi_half = _mm256_unpackhi_epi8 (ascii_hi, ascii_lo);

  /* Per-lane store layout: lo_half[lane i] holds UUID(i) chars 0..15,
   * hi_half[lane i] holds UUID(i) chars 16..31. With AVX2's
   * lane-major register layout, a straight pair of 16-byte stores
   * in (lo_half-lo, hi_half-lo, lo_half-hi, hi_half-hi) order
   * produces the contiguous 64-char output. */
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (out64 + 0),
      _mm256_castsi256_si128 (lo_half));
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (out64 + 16),
      _mm256_castsi256_si128 (hi_half));
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (out64 + 32),
      _mm256_extracti128_si256 (lo_half, 1));
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (out64 + 48),
      _mm256_extracti128_si256 (hi_half, 1));
}

/* Splice the 32 contiguous hex chars in |hex32| into the 36-byte
 * canonical 8-4-4-4-12 layout at |out36|. Mirrors the splice in
 * hex_ssse3.c. Source offsets are 2 * byte-index. */
static inline void
chronoid_uuidv7_dash_splice (char *out36, const char *hex32)
{
  memcpy (out36 + 0, hex32 + 0, 8);
  out36[8] = '-';
  memcpy (out36 + 9, hex32 + 8, 4);
  out36[13] = '-';
  memcpy (out36 + 14, hex32 + 12, 4);
  out36[18] = '-';
  memcpy (out36 + 19, hex32 + 16, 4);
  out36[23] = '-';
  memcpy (out36 + 24, hex32 + 20, 12);
}

void
chronoid_uuidv7_string_batch_avx2 (const chronoid_uuidv7_t *ids,
    char *out_36n, size_t n)
{
  size_t bulk = n & ~(size_t) 3;

  for (size_t base = 0; base < bulk; base += 4) {
    /* Two AVX2 passes per outer iter, each handling two UUIDs.
     * 64 bytes input -> 64 hex chars per pass -> dash-splice into
     * two 36-byte output blocks. The temp lives on the stack so
     * the splice is a register-resident memcpy. */
    char hex[128];
    chronoid_uuidv7_hex32_avx2 (hex + 0, (const uint8_t *) &ids[base + 0]);
    chronoid_uuidv7_hex32_avx2 (hex + 64, (const uint8_t *) &ids[base + 2]);

    chronoid_uuidv7_dash_splice (out_36n + (base + 0)
        * CHRONOID_UUIDV7_STRING_LEN, hex + 0);
    chronoid_uuidv7_dash_splice (out_36n + (base + 1)
        * CHRONOID_UUIDV7_STRING_LEN, hex + 32);
    chronoid_uuidv7_dash_splice (out_36n + (base + 2)
        * CHRONOID_UUIDV7_STRING_LEN, hex + 64);
    chronoid_uuidv7_dash_splice (out_36n + (base + 3)
        * CHRONOID_UUIDV7_STRING_LEN, hex + 96);
  }

  /* Emit zeroupper before the scalar tail call AND before
   * returning; both transitions can land us in non-VEX code. */
  _mm256_zeroupper ();

  if (bulk < n)
    chronoid_uuidv7_string_batch_scalar (ids + bulk,
        out_36n + bulk * CHRONOID_UUIDV7_STRING_LEN, n - bulk);
}

#endif /* x86_64 */
