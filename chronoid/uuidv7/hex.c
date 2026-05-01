/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Scalar hex encode/decode for UUIDv7's canonical 8-4-4-4-12 textual
 * representation. The 256-byte LUT is built at compile time so the
 * decode hot path stays branchless on the per-character dispatch
 * (one indexed load + one compare-against-sentinel) and so a future
 * SIMD validation kernel can reuse identical character semantics.
 *
 * No upstream lineage; the layout and alphabet are RFC 9562 §4.
 */
#include <chronoid/uuidv7/hex.h>
#include <chronoid/uuidv7/hex_simd.h>

#include <string.h>

/* Lowercase alphabet for encode. The NUL terminator at index 16 is
 * intentionally kept (no fixed-size [16] declaration) so gcc's
 * -Wunterminated-string-initialization stays happy; the loop only
 * ever indexes 0..15. */
static const char kHexLower[]
    = "0123456789abcdef";

/* ASCII -> hex nibble value, with 0xFF as the invalid sentinel.
 * Locale-independent: the table is keyed on raw byte values, not on
 * isxdigit() / tolower(). Indices outside [0-9A-Fa-f] -- including
 * '-', '\0', whitespace, and every byte >= 0x80 -- map to 0xFF. */
static const uint8_t kHexValue[256] = {
  /* 0x00 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x08 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x10 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x18 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x20 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x28 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* '0'..'9' = 0..9 */
  /* 0x30 */ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  /* 0x38 */ 0x08, 0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x40 */ 0xff,
  /* 'A'..'F' = 10..15 */
  0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  /* 0x47 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x4f */ 0xff,
  /* 0x50 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x58 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x60 */ 0xff,
  /* 'a'..'f' = 10..15 */
  0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  /* 0x67 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x6f */ 0xff,
  /* 0x70 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x78 */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  /* 0x80..0xff: all invalid */
  [128] = 0xff,[129] = 0xff,[130] = 0xff,[131] = 0xff,[132] = 0xff,[133] = 0xff,
  [134] = 0xff,[135] = 0xff,[136] = 0xff,[137] = 0xff,[138] = 0xff,[139] = 0xff,
  [140] = 0xff,[141] = 0xff,[142] = 0xff,[143] = 0xff,[144] = 0xff,[145] = 0xff,
  [146] = 0xff,[147] = 0xff,[148] = 0xff,[149] = 0xff,[150] = 0xff,[151] = 0xff,
  [152] = 0xff,[153] = 0xff,[154] = 0xff,[155] = 0xff,[156] = 0xff,[157] = 0xff,
  [158] = 0xff,[159] = 0xff,[160] = 0xff,[161] = 0xff,[162] = 0xff,[163] = 0xff,
  [164] = 0xff,[165] = 0xff,[166] = 0xff,[167] = 0xff,[168] = 0xff,[169] = 0xff,
  [170] = 0xff,[171] = 0xff,[172] = 0xff,[173] = 0xff,[174] = 0xff,[175] = 0xff,
  [176] = 0xff,[177] = 0xff,[178] = 0xff,[179] = 0xff,[180] = 0xff,[181] = 0xff,
  [182] = 0xff,[183] = 0xff,[184] = 0xff,[185] = 0xff,[186] = 0xff,[187] = 0xff,
  [188] = 0xff,[189] = 0xff,[190] = 0xff,[191] = 0xff,[192] = 0xff,[193] = 0xff,
  [194] = 0xff,[195] = 0xff,[196] = 0xff,[197] = 0xff,[198] = 0xff,[199] = 0xff,
  [200] = 0xff,[201] = 0xff,[202] = 0xff,[203] = 0xff,[204] = 0xff,[205] = 0xff,
  [206] = 0xff,[207] = 0xff,[208] = 0xff,[209] = 0xff,[210] = 0xff,[211] = 0xff,
  [212] = 0xff,[213] = 0xff,[214] = 0xff,[215] = 0xff,[216] = 0xff,[217] = 0xff,
  [218] = 0xff,[219] = 0xff,[220] = 0xff,[221] = 0xff,[222] = 0xff,[223] = 0xff,
  [224] = 0xff,[225] = 0xff,[226] = 0xff,[227] = 0xff,[228] = 0xff,[229] = 0xff,
  [230] = 0xff,[231] = 0xff,[232] = 0xff,[233] = 0xff,[234] = 0xff,[235] = 0xff,
  [236] = 0xff,[237] = 0xff,[238] = 0xff,[239] = 0xff,[240] = 0xff,[241] = 0xff,
  [242] = 0xff,[243] = 0xff,[244] = 0xff,[245] = 0xff,[246] = 0xff,[247] = 0xff,
  [248] = 0xff,[249] = 0xff,[250] = 0xff,[251] = 0xff,[252] = 0xff,[253] = 0xff,
  [254] = 0xff,[255] = 0xff,
};

/* Bytes that map to each of the four hyphen-bearing character offsets,
 * in input-string order. 0..3 -> bytes 0..3, 4..5 -> bytes 4..5, etc.
 * Encoded layout (RFC 9562 §4):
 *   chars 0..7   = bytes 0..3   (8 hex digits)
 *   char  8      = '-'
 *   chars 9..12  = bytes 4..5   (4 hex digits)
 *   char  13     = '-'
 *   chars 14..17 = bytes 6..7   (4 hex digits)
 *   char  18     = '-'
 *   chars 19..22 = bytes 8..9   (4 hex digits)
 *   char  23     = '-'
 *   chars 24..35 = bytes 10..15 (12 hex digits) */

void
chronoid_hex_encode_lower_scalar (char out[36], const uint8_t in[16])
{
  /* Map byte index -> character offset of its high nibble. The
   * hyphens at 8, 13, 18, 23 partition the 16 bytes into groups of
   * 4-2-2-2-6 bytes. Linear walk so the compiler unrolls it cleanly.
   * Static table avoids branching on hyphen positions inside the
   * loop. */
  static const uint8_t kCharOff[16] = {
    0, 2, 4, 6,                 /* group 1: chars 0..7   */
    9, 11,                      /* group 2: chars 9..12  */
    14, 16,                     /* group 3: chars 14..17 */
    19, 21,                     /* group 4: chars 19..22 */
    24, 26, 28, 30, 32, 34,     /* group 5: chars 24..35 */
  };

  for (size_t i = 0; i < 16; ++i) {
    uint8_t b = in[i];
    size_t off = kCharOff[i];
    out[off] = kHexLower[(b >> 4) & 0x0F];
    out[off + 1] = kHexLower[b & 0x0F];
  }
  out[8] = '-';
  out[13] = '-';
  out[18] = '-';
  out[23] = '-';
}

/* Public-internal entry point. Resolves at compile time to the SSSE3
 * kernel on x86_64 and to the scalar reference everywhere else; see
 * chronoid/uuidv7/hex_simd.h. Decode (chronoid_hex_decode below) is
 * scalar-only -- there is no SIMD validation kernel yet. */
void
chronoid_hex_encode_lower (char out[36], const uint8_t in[16])
{
  CHRONOID_HEX_ENCODE16_LOWER (out, in);
}

int
chronoid_hex_decode (uint8_t out[16], const char *s, size_t len)
{
  if (len != 36)
    return -1;

  /* Hyphen positions are fixed by RFC 9562: the bytes there MUST be
   * literal '-'. Reject up front so the per-byte loop only deals
   * with hex digits. */
  if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
    return -1;

  /* Symmetric inverse of kCharOff in encode -- the high-nibble char
   * offset for each output byte. Keeping the table identical to the
   * encode side makes drift impossible. */
  static const uint8_t kCharOff[16] = {
    0, 2, 4, 6,
    9, 11,
    14, 16,
    19, 21,
    24, 26, 28, 30, 32, 34,
  };

  /* Decode into a stack-local temp. The loop accumulates an OR mask
   * of every nibble lookup so a single post-loop branch decides
   * success vs. failure -- no early exit, so we never partially
   * mutate |out|. (The memcpy at the end is the only writer.) */
  uint8_t tmp[16];
  uint32_t bad = 0;
  for (size_t i = 0; i < 16; ++i) {
    size_t off = kCharOff[i];
    uint8_t hi = kHexValue[(unsigned char) s[off]];
    uint8_t lo = kHexValue[(unsigned char) s[off + 1]];
    bad |= hi;
    bad |= lo;
    tmp[i] = (uint8_t) ((hi << 4) | lo);
  }
  /* Any 0xff lookup leaves the high bit of |bad| set in its low byte
   * (0xff has bit 7 set). The accumulator stays clean (top bit zero)
   * iff every nibble was a valid hex digit (max value 0x0F). */
  if ((bad & 0xF0) != 0)
    return -1;

  memcpy (out, tmp, 16);
  return 0;
}
