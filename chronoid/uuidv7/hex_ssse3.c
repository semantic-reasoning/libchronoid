/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * SSSE3 single-UUID hex encode kernel for UUIDv7's canonical
 * 8-4-4-4-12 textual form. Maps 16 input bytes to 32 lowercase ASCII
 * hex digits via PSHUFB on the 16-byte LUT "0123456789abcdef", then
 * splices in the four hyphens by scalar memcpy.
 *
 * Compiled into its own static_library with a per-file -mssse3 flag
 * (matching the AVX2 kernel pattern) because SSSE3 is x86-64-v2 and
 * NOT in the AMD64 psABI v1 baseline (which mandates only SSE2). The
 * library relies on near-universal SSSE3 availability since 2006
 * (Intel Core 2 / AMD K10) rather than gating on runtime CPUID; an
 * SSE2-only fallback rung can be added later if downstream targets
 * pre-Core-2 hardware. The scalar fallback in hex.c stays available
 * unconditionally and is exercised by the parity test in
 * tests/test_uuidv7_parse_format.c.
 *
 * Algorithm (RFC 9562 §4 layout):
 *   1. v = load 16 bytes
 *   2. hi = (v >> 4) & 0x0F   (16 nibbles, one per byte)
 *      lo = v & 0x0F
 *      Note: SSE2/3 has no _mm_srli_epi8, so we shift epi16 and mask.
 *   3. ascii_hi = pshufb(LUT, hi)     // each nibble -> ASCII char
 *      ascii_lo = pshufb(LUT, lo)
 *   4. interleave: [hi0,lo0,hi1,lo1,...,hi7,lo7]  (low half)
 *                  [hi8,lo8,...,hi15,lo15]        (high half)
 *      That's 32 contiguous lowercase hex chars (no hyphens).
 *   5. memcpy chunks into out[36] with hyphens at offsets 8,13,18,23.
 *
 * Critic R5.1 pin: nibble order is high-then-low so byte 0xAB encodes
 * as "ab", not "ba". The known-vector parity test
 * {0x00,...,0x0f} -> "00010203-0405-0607-0809-0a0b0c0d0e0f"
 * locks this in.
 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <string.h>
#include <stdint.h>
#include <tmmintrin.h>          /* SSSE3 (_mm_shuffle_epi8) */

#include <chronoid/uuidv7/hex_simd.h>

void
chronoid_hex_encode_lower_ssse3 (char out[36], const uint8_t in[16])
{
  /* The 16-byte lowercase hex LUT, fed straight to pshufb. */
  static const uint8_t kHexLowerLut[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };

  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  __m128i v   = _mm_loadu_si128 ((const __m128i *) in);
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  __m128i lut = _mm_loadu_si128 ((const __m128i *) kHexLowerLut);

  __m128i mask_low = _mm_set1_epi8 (0x0F);

  /* SSE has no per-byte shift right; shift epi16 by 4 then mask the
   * low nibble out of each byte. The high four bits of each byte's
   * high nibble lane become 0 after the AND, which is exactly what
   * pshufb wants for an in-LUT index (0..15). */
  __m128i hi_nibbles = _mm_and_si128 (_mm_srli_epi16 (v, 4), mask_low);
  __m128i lo_nibbles = _mm_and_si128 (v,                     mask_low);

  /* pshufb: for each lane index i in [0..15], output[i] = LUT[low4(idx[i])].
   * Our nibble vectors only have values 0..15 by construction so the
   * top-bit-zero -> select-from-LUT semantics line up perfectly. */
  __m128i ascii_hi = _mm_shuffle_epi8 (lut, hi_nibbles);
  __m128i ascii_lo = _mm_shuffle_epi8 (lut, lo_nibbles);

  /* Interleave: punpcklbw([hi],[lo]) yields
   *   [hi0,lo0, hi1,lo1, ..., hi7,lo7]      (chars for bytes 0..7)
   * punpckhbw([hi],[lo]) yields
   *   [hi8,lo8, ..., hi15,lo15]             (chars for bytes 8..15)
   * Together that's the 32-char hex string (no hyphens). */
  __m128i lo_half = _mm_unpacklo_epi8 (ascii_hi, ascii_lo);
  __m128i hi_half = _mm_unpackhi_epi8 (ascii_hi, ascii_lo);

  char tmp[32];
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (tmp + 0),  lo_half);
  /* NOLINTNEXTLINE(clang-diagnostic-cast-align) */
  _mm_storeu_si128 ((__m128i *) (tmp + 16), hi_half);

  /* Splice in the four hyphens. Layout (RFC 9562 §4):
   *   chars 0..7   = bytes 0..3   (8 hex digits)
   *   char  8      = '-'
   *   chars 9..12  = bytes 4..5   (4 hex digits)
   *   char  13     = '-'
   *   chars 14..17 = bytes 6..7   (4 hex digits)
   *   char  18     = '-'
   *   chars 19..22 = bytes 8..9   (4 hex digits)
   *   char  23     = '-'
   *   chars 24..35 = bytes 10..15 (12 hex digits)
   * Source offsets in tmp[] are 2x the byte index. */
  memcpy (out +  0, tmp +  0,  8);  /* bytes 0..3   -> chars 0..7   */
  out[8]  = '-';
  memcpy (out +  9, tmp +  8,  4);  /* bytes 4..5   -> chars 9..12  */
  out[13] = '-';
  memcpy (out + 14, tmp + 12,  4);  /* bytes 6..7   -> chars 14..17 */
  out[18] = '-';
  memcpy (out + 19, tmp + 16,  4);  /* bytes 8..9   -> chars 19..22 */
  out[23] = '-';
  memcpy (out + 24, tmp + 20, 12);  /* bytes 10..15 -> chars 24..35 */
}
#endif /* x86 */
