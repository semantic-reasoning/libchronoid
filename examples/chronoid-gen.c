/* SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
 *
 * chronoid-gen -- the libchronoid demo CLI. Generates and inspects
 * KSUIDs and UUIDv7s; intentionally feature-aligned with upstream
 * segmentio/ksuid's `cmd/ksuid` (cmd/ksuid/main.go) MINUS the Go
 * template format which cannot be sensibly reimplemented without
 * dragging Go's text/template grammar in.
 *
 *   chronoid-gen                          -- emit one new KSUID
 *   chronoid-gen --format=uuidv7          -- emit one new UUIDv7
 *   chronoid-gen -n N                     -- emit N IDs of the chosen format
 *   chronoid-gen -f FORMAT                -- one of {string, inspect, time,
 *                                          timestamp, payload, raw}
 *   chronoid-gen [args...]                -- treat args as KSUID/UUIDv7 strings
 *                                          to parse and format. The format
 *                                          is auto-detected per arg by string
 *                                          length: 27 = KSUID, 36 = UUIDv7.
 *   chronoid-gen -v                       -- prefix each line with "<id>: "
 *
 * The --format=ksuid|uuidv7 long flag selects the ID format for the
 * generation path. It is a separate flag from -f (output projection)
 * to avoid collision; --format defaults to ksuid for backward UX.
 * In parse mode, positional length wins over --format; a mismatch
 * between --format and positional length is an error.
 */
#include <chronoid/ksuid.h>
#include <chronoid/uuidv7.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
  FMT_STRING,
  FMT_INSPECT,
  FMT_TIME,
  FMT_TIMESTAMP,
  FMT_PAYLOAD,
  FMT_RAW,
};

enum
{
  IDFMT_KSUID,
  IDFMT_UUIDV7,
};

static int
parse_format (const char *s)
{
  if (strcmp (s, "string") == 0)
    return FMT_STRING;
  if (strcmp (s, "inspect") == 0)
    return FMT_INSPECT;
  if (strcmp (s, "time") == 0)
    return FMT_TIME;
  if (strcmp (s, "timestamp") == 0)
    return FMT_TIMESTAMP;
  if (strcmp (s, "payload") == 0)
    return FMT_PAYLOAD;
  if (strcmp (s, "raw") == 0)
    return FMT_RAW;
  return -1;
}

static int
parse_idformat (const char *s)
{
  if (strcmp (s, "ksuid") == 0)
    return IDFMT_KSUID;
  if (strcmp (s, "uuidv7") == 0)
    return IDFMT_UUIDV7;
  return -1;
}

static void
fputs_hex_upper (const uint8_t *bytes, size_t n, FILE *f)
{
  static const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) {
    fputc (digits[(bytes[i] >> 4) & 0xf], f);
    fputc (digits[bytes[i] & 0xf], f);
  }
}

static void
fputs_time_local (int64_t unix_seconds, FILE *f)
{
  time_t t = (time_t) unix_seconds;
  struct tm tm;
#if defined(_WIN32)
  if (localtime_s (&tm, &t) != 0) {
    fprintf (f, "%" PRId64, unix_seconds);
    return;
  }
#else
  if (localtime_r (&t, &tm) == NULL) {
    fprintf (f, "%" PRId64, unix_seconds);
    return;
  }
#endif
  /* Mirror Go time.Time.String() shape: "2006-01-02 15:04:05 -0700 MST" */
  char buf[64];
  if (strftime (buf, sizeof buf, "%Y-%m-%d %H:%M:%S %z %Z", &tm) == 0)
    snprintf (buf, sizeof buf, "%" PRId64, unix_seconds);
  fputs (buf, f);
}

/* UUIDv7 timestamp is millisecond-resolution; render it as ISO-8601 UTC
 * with explicit ms component and a "+0000 UTC" tail to keep the visual
 * shape close to fputs_time_local while making the UTC offset
 * unambiguous. The UUIDv7 RFC defines unix_ms relative to Unix epoch
 * UTC, so rendering in UTC avoids the timezone-dependent jitter that
 * would make round-trip CLI tests fragile. */
static void
fputs_time_utc_ms (int64_t unix_ms, FILE *f)
{
  /* Split into seconds + ms tail. C floor-divides for non-negative
   * inputs, which matches what we want; UUIDv7 unix_ms is always
   * non-negative (48-bit unsigned interpretation). */
  int64_t secs = unix_ms / 1000;
  int ms = (int) (unix_ms % 1000);
  if (ms < 0) {
    /* Defensive: pin negative ms (impossible for valid UUIDv7) into the
     * lower bucket so the printed string stays well-formed. */
    ms += 1000;
    secs -= 1;
  }
  time_t t = (time_t) secs;
  struct tm tm;
#if defined(_WIN32)
  if (gmtime_s (&tm, &t) != 0) {
    fprintf (f, "%" PRId64, unix_ms);
    return;
  }
#else
  if (gmtime_r (&t, &tm) == NULL) {
    fprintf (f, "%" PRId64, unix_ms);
    return;
  }
#endif
  char buf[64];
  if (strftime (buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm) == 0) {
    fprintf (f, "%" PRId64, unix_ms);
    return;
  }
  fprintf (f, "%s.%03d +0000 UTC", buf, ms);
}

/* ------------------------------------------------------------------ */
/* KSUID print paths -- unchanged from the pre-UUIDv7 CLI.            */
/* ------------------------------------------------------------------ */

static void
print_string (const chronoid_ksuid_t *id)
{
  char s[CHRONOID_KSUID_STRING_LEN + 1];
  chronoid_ksuid_format (id, s);
  s[CHRONOID_KSUID_STRING_LEN] = '\0';
  puts (s);
}

static void
print_inspect (const chronoid_ksuid_t *id)
{
  char s[CHRONOID_KSUID_STRING_LEN + 1];
  chronoid_ksuid_format (id, s);
  s[CHRONOID_KSUID_STRING_LEN] = '\0';
  /* Format string is byte-for-byte equivalent to upstream
   * cmd/ksuid/main.go:86-98 modulo the leading newline. */
  printf ("\n" "REPRESENTATION:\n" "\n" "  String: %s\n" "     Raw: ", s);
  fputs_hex_upper (id->b, CHRONOID_KSUID_BYTES, stdout);
  printf ("\n" "\n" "COMPONENTS:\n" "\n" "       Time: ");
  fputs_time_local (chronoid_ksuid_time_unix (id), stdout);
  printf ("\n"
      "  Timestamp: %" PRIu32 "\n" "    Payload: ",
      chronoid_ksuid_timestamp (id));
  fputs_hex_upper (chronoid_ksuid_payload (id), CHRONOID_KSUID_PAYLOAD_LEN,
      stdout);
  fputs ("\n\n", stdout);
}

static void
print_time (const chronoid_ksuid_t *id)
{
  fputs_time_local (chronoid_ksuid_time_unix (id), stdout);
  fputc ('\n', stdout);
}

static void
print_timestamp (const chronoid_ksuid_t *id)
{
  printf ("%" PRIu32 "\n", chronoid_ksuid_timestamp (id));
}

static void
print_payload (const chronoid_ksuid_t *id)
{
  fwrite (chronoid_ksuid_payload (id), 1, CHRONOID_KSUID_PAYLOAD_LEN, stdout);
}

static void
print_raw (const chronoid_ksuid_t *id)
{
  fwrite (id->b, 1, CHRONOID_KSUID_BYTES, stdout);
}

static void
print_one_ksuid (int format, const chronoid_ksuid_t *id, int verbose)
{
  if (verbose) {
    char s[CHRONOID_KSUID_STRING_LEN + 1];
    chronoid_ksuid_format (id, s);
    s[CHRONOID_KSUID_STRING_LEN] = '\0';
    fputs (s, stdout);
    fputs (": ", stdout);
  }
  switch (format) {
    case FMT_STRING:
      print_string (id);
      break;
    case FMT_INSPECT:
      print_inspect (id);
      break;
    case FMT_TIME:
      print_time (id);
      break;
    case FMT_TIMESTAMP:
      print_timestamp (id);
      break;
    case FMT_PAYLOAD:
      print_payload (id);
      break;
    case FMT_RAW:
      print_raw (id);
      break;
    default:
      /* parse_format() validates the input; no other value reaches
       * print_one_ksuid. Branch exists to satisfy
       * bugprone-switch-missing-default-case. */
      break;
  }
}

/* ------------------------------------------------------------------ */
/* UUIDv7 print paths.                                                */
/* ------------------------------------------------------------------ */

static void
print_string_uuidv7 (const chronoid_uuidv7_t *id)
{
  char s[CHRONOID_UUIDV7_STRING_LEN + 1];
  chronoid_uuidv7_format (id, s);
  s[CHRONOID_UUIDV7_STRING_LEN] = '\0';
  puts (s);
}

static void
print_inspect_uuidv7 (const chronoid_uuidv7_t *id)
{
  char s[CHRONOID_UUIDV7_STRING_LEN + 1];
  chronoid_uuidv7_format (id, s);
  s[CHRONOID_UUIDV7_STRING_LEN] = '\0';
  /* Mirror the KSUID inspect labels (String:, Raw:, Time:, Timestamp:)
   * so test_cli.sh's existing label assertions stay green when fed a
   * UUIDv7. The Version / Variant / rand_a / rand_b lines are
   * additive. The 12-bit rand_a is bytes 6 (low nibble) and 7 of the
   * binary form; rand_b is bytes 8..15 with the variant bits stripped
   * back to their full 8-bit width on display so callers can spot
   * non-conforming UUIDs. */
  uint16_t rand_a_12 =
      (uint16_t) (((uint16_t) (id->b[6] & 0x0f) << 8) | (uint16_t) id->b[7]);
  printf ("\n" "REPRESENTATION:\n" "\n" "  String: %s\n" "     Raw: ", s);
  fputs_hex_upper (id->b, CHRONOID_UUIDV7_BYTES, stdout);
  printf ("\n" "\n" "COMPONENTS:\n" "\n" "       Time: ");
  fputs_time_utc_ms (chronoid_uuidv7_unix_ms (id), stdout);
  printf ("\n"
      "  Timestamp: %" PRId64 "\n"
      "    Version: %u\n"
      "    Variant: 0b%u%u (RFC 4122 / 9562)\n"
      "     rand_a: 0x%03X\n"
      "     rand_b: ",
      chronoid_uuidv7_unix_ms (id),
      (unsigned) chronoid_uuidv7_version (id),
      ((unsigned) chronoid_uuidv7_variant (id) >> 1) & 1u,
      (unsigned) chronoid_uuidv7_variant (id) & 1u, (unsigned) rand_a_12);
  fputs_hex_upper (id->b + 8, 8, stdout);
  fputs ("\n\n", stdout);
}

static void
print_time_uuidv7 (const chronoid_uuidv7_t *id)
{
  fputs_time_utc_ms (chronoid_uuidv7_unix_ms (id), stdout);
  fputc ('\n', stdout);
}

static void
print_timestamp_uuidv7 (const chronoid_uuidv7_t *id)
{
  printf ("%" PRId64 "\n", chronoid_uuidv7_unix_ms (id));
}

/* UUIDv7 has no single "payload" field. We print the random tail
 * (rand_a's 12 bits packed into 2 bytes plus rand_b's 8 bytes) as
 * 10 raw bytes -- analogous to KSUID's binary -f payload output --
 * so scripted callers get a stable, byte-counted rendering. */
static void
print_payload_uuidv7 (const chronoid_uuidv7_t *id)
{
  uint8_t buf[10];
  /* rand_a low nibble of byte 6 + byte 7 = 12-bit value packed into
   * two bytes with the high nibble zeroed. */
  buf[0] = id->b[6] & 0x0f;
  buf[1] = id->b[7];
  /* rand_b -- the variant bits in the top of byte 8 are part of the
   * raw layout and are emitted as-is; this keeps the output a faithful
   * slice of the binary UUID for round-trip uses. */
  memcpy (buf + 2, id->b + 8, 8);
  fwrite (buf, 1, sizeof buf, stdout);
}

static void
print_raw_uuidv7 (const chronoid_uuidv7_t *id)
{
  /* -f raw is the unencoded byte image of the ID, suitable for
   * `> file.bin`: 16 raw bytes (matches KSUID -f raw which writes 20
   * raw bytes). The hex-encoded uppercase form lives in the inspect
   * projection's "Raw:" line, not here. */
  fwrite (id->b, 1, CHRONOID_UUIDV7_BYTES, stdout);
}

static void
print_one_uuidv7 (int format, const chronoid_uuidv7_t *id, int verbose)
{
  if (verbose) {
    char s[CHRONOID_UUIDV7_STRING_LEN + 1];
    chronoid_uuidv7_format (id, s);
    s[CHRONOID_UUIDV7_STRING_LEN] = '\0';
    fputs (s, stdout);
    fputs (": ", stdout);
  }
  switch (format) {
    case FMT_STRING:
      print_string_uuidv7 (id);
      break;
    case FMT_INSPECT:
      print_inspect_uuidv7 (id);
      break;
    case FMT_TIME:
      print_time_uuidv7 (id);
      break;
    case FMT_TIMESTAMP:
      print_timestamp_uuidv7 (id);
      break;
    case FMT_PAYLOAD:
      print_payload_uuidv7 (id);
      break;
    case FMT_RAW:
      print_raw_uuidv7 (id);
      break;
    default:
      break;
  }
}

static void
usage (FILE *f, const char *argv0)
{
  fprintf (f,
      "usage: %s [-n N] [-f FORMAT] [--format=ID] [-v] [ID ...]\n"
      "  -n N        number of IDs to generate when no args given (default 1)\n"
      "  -f FORMAT   output projection: one of string, inspect, time,\n"
      "              timestamp, payload, raw (default: string)\n"
      "  --format=ID ID format for generation: one of ksuid, uuidv7\n"
      "              (default: ksuid)\n"
      "  -v          prefix each line with the ID and ': '\n"
      "  -h          show this help\n"
      "When ID arguments are supplied they are parsed and formatted; the -n\n"
      "flag is ignored. Positional args are auto-detected by length: 27 chars\n"
      "= KSUID, 36 chars = UUIDv7. A mismatch between --format and a\n"
      "positional argument's detected format is an error.\n", argv0);
}

int
main (int argc, char **argv)
{
  long count = 1;
  int format = FMT_STRING;
  int idformat = IDFMT_KSUID;
  int idformat_explicit = 0;    /* set if --format=... was passed         */
  int verbose = 0;

  /* Hand-rolled option parser. POSIX getopt(3) lives in <unistd.h>
   * which is not available on Windows MSVC, and the few flags here
   * are simple enough that pulling in a getopt shim would be more
   * code than parsing them inline. The accepted spellings are:
   *   -n N         (count, positive integer; -n N as separate tokens)
   *   -f FMT       (output projection)
   *   --format=ID  (id format; ksuid or uuidv7)
   *   -v           (verbose; switch)
   *   -h           (help; switch)
   * Combined short options (-vh) and attached values (-n4) are not
   * supported -- the existing tests/test_cli.sh exercises only the
   * separate-token spelling, matching upstream Go ksuid's CLI. The
   * --format flag is parsed before any positional validation per the
   * Critic's R8.1 hardening note. */
  int idx = 1;
  while (idx < argc) {
    const char *a = argv[idx];
    if (a[0] != '-' || a[1] == '\0') {
      /* Not a recognised option; treat as a positional ID. */
      break;
    }
    /* Long option: --format=VALUE (only spelling we accept; --format VAL
     * with a separate token is rejected to keep the parser regular). */
    if (a[1] == '-') {
      if (strncmp (a, "--format=", 9) == 0) {
        const char *val = a + 9;
        idformat = parse_idformat (val);
        if (idformat < 0) {
          fprintf (stderr, "unknown id format: %s\n", val);
          return 1;
        }
        idformat_explicit = 1;
        ++idx;
        continue;
      }
      fprintf (stderr, "unknown option: %s\n", a);
      usage (stderr, argv[0]);
      return 1;
    }
    /* Short option: must be exactly two characters. */
    if (a[2] != '\0') {
      fprintf (stderr, "unknown option: %s\n", a);
      usage (stderr, argv[0]);
      return 1;
    }
    char flag = a[1];
    if (flag == 'v') {
      verbose = 1;
      ++idx;
    } else if (flag == 'h') {
      usage (stdout, argv[0]);
      return 0;
    } else if (flag == 'n' || flag == 'f') {
      if (idx + 1 >= argc) {
        fprintf (stderr, "missing argument for -%c\n", flag);
        usage (stderr, argv[0]);
        return 1;
      }
      const char *val = argv[idx + 1];
      if (flag == 'n') {
        char *end;
        errno = 0;
        long v = strtol (val, &end, 10);
        if (errno != 0 || *end != '\0' || v <= 0) {
          fprintf (stderr, "invalid -n value: %s\n", val);
          return 1;
        }
        count = v;
      } else {
        format = parse_format (val);
        if (format < 0) {
          fprintf (stderr, "unknown format: %s\n", val);
          return 1;
        }
      }
      idx += 2;
    } else {
      fprintf (stderr, "unknown option: %s\n", a);
      usage (stderr, argv[0]);
      return 1;
    }
  }

  if (idx == argc) {
    /* Generation mode. */
    for (long i = 0; i < count; ++i) {
      if (idformat == IDFMT_UUIDV7) {
        chronoid_uuidv7_t id;
        chronoid_uuidv7_err_t e = chronoid_uuidv7_new (&id);
        if (e != CHRONOID_UUIDV7_OK) {
          fprintf (stderr, "chronoid_uuidv7_new failed (err %d)\n", (int) e);
          return 2;
        }
        print_one_uuidv7 (format, &id, verbose);
      } else {
        chronoid_ksuid_t id;
        chronoid_ksuid_err_t e = chronoid_ksuid_new (&id);
        if (e != CHRONOID_KSUID_OK) {
          fprintf (stderr, "chronoid_ksuid_new failed (err %d)\n", (int) e);
          return 2;
        }
        print_one_ksuid (format, &id, verbose);
      }
    }
  } else {
    /* Parse mode. Positional length wins when --format is not
     * explicit; if --format is explicit, mismatch is an error so the
     * user gets a loud signal instead of silently skewed output. */
    for (int i = idx; i < argc; ++i) {
      size_t len = strlen (argv[i]);
      int detected;
      if (len == CHRONOID_KSUID_STRING_LEN) {
        detected = IDFMT_KSUID;
      } else if (len == CHRONOID_UUIDV7_STRING_LEN) {
        detected = IDFMT_UUIDV7;
      } else {
        fprintf (stderr,
            "could not parse %s: expected 27 (KSUID) or 36 (UUIDv7) chars, got %zu\n",
            argv[i], len);
        return 1;
      }
      if (idformat_explicit && detected != idformat) {
        fprintf (stderr,
            "format mismatch: --format=%s but %s looks like a %s (%zu chars)\n",
            idformat == IDFMT_UUIDV7 ? "uuidv7" : "ksuid",
            argv[i], detected == IDFMT_UUIDV7 ? "UUIDv7" : "KSUID", len);
        return 1;
      }
      if (detected == IDFMT_UUIDV7) {
        chronoid_uuidv7_t id;
        chronoid_uuidv7_err_t e = chronoid_uuidv7_parse (&id, argv[i], len);
        if (e != CHRONOID_UUIDV7_OK) {
          fprintf (stderr, "could not parse %s (err %d)\n", argv[i], (int) e);
          return 1;
        }
        print_one_uuidv7 (format, &id, verbose);
      } else {
        chronoid_ksuid_t id;
        chronoid_ksuid_err_t e = chronoid_ksuid_parse (&id, argv[i], len);
        if (e != CHRONOID_KSUID_OK) {
          fprintf (stderr, "could not parse %s (err %d)\n", argv[i], (int) e);
          return 1;
        }
        print_one_ksuid (format, &id, verbose);
      }
    }
  }
  return 0;
}
