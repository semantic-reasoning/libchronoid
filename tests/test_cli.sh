#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Integration test for the ksuid-gen binary. Drives the CLI directly
# and verifies the round-trip generation + parse contract that the
# user-facing requirement asks for.

set -eu

KSUID_GEN="${1:?usage: test_cli.sh <path/to/ksuid-gen>}"

# 1. Default invocation emits exactly one 27-character base62 line.
out=$("$KSUID_GEN")
if [ "${#out}" -ne 27 ]; then
  echo "expected 27-char default output, got ${#out}: $out" >&2
  exit 1
fi
case "$out" in
  *[!0-9A-Za-z]*)
    echo "default output contains non-base62 char: $out" >&2
    exit 1
    ;;
esac

# 2. -n 4 emits 4 distinct 27-char lines.
out=$("$KSUID_GEN" -n 4)
n_lines=$(printf '%s\n' "$out" | wc -l)
if [ "$n_lines" -ne 4 ]; then
  echo "expected 4 lines from -n 4, got $n_lines:" >&2
  printf '%s\n' "$out" >&2
  exit 1
fi
n_uniq=$(printf '%s\n' "$out" | sort -u | wc -l)
if [ "$n_uniq" -ne 4 ]; then
  echo "expected 4 distinct KSUIDs, got $n_uniq" >&2
  exit 1
fi

# 3. Round trip: parse the previously-generated KSUIDs back through
#    the CLI and confirm we get the same string.
sample=$(printf '%s\n' "$out" | head -1)
back=$("$KSUID_GEN" "$sample")
if [ "$sample" != "$back" ]; then
  echo "round-trip mismatch: $sample vs $back" >&2
  exit 1
fi

# 4. Inspect format includes the canonical labels.
inspect=$("$KSUID_GEN" -f inspect "$sample")
for label in "REPRESENTATION:" "  String:" "     Raw:" \
             "COMPONENTS:" "       Time:" "  Timestamp:" \
             "    Payload:"; do
  case "$inspect" in
    *"$label"*) ;;
    *)
      echo "inspect output missing '$label':" >&2
      printf '%s\n' "$inspect" >&2
      exit 1
      ;;
  esac
done

# 5. Verbose mode prefixes each line with the KSUID and ": ".
verbose=$("$KSUID_GEN" -v -f timestamp "$sample")
case "$verbose" in
  "$sample: "*) ;;
  *)
    echo "expected -v output to start with '$sample: ', got: $verbose" >&2
    exit 1
    ;;
esac

# 6. Known golden vector parses to the documented timestamp.
expected_ts=107608047
actual_ts=$("$KSUID_GEN" -f timestamp 0ujtsYcgvSTl8PAuAdqWYSMnLOv)
if [ "$actual_ts" != "$expected_ts" ]; then
  echo "expected timestamp $expected_ts for sample, got $actual_ts" >&2
  exit 1
fi

# 7. -f raw emits exactly 20 bytes (binary).
n_raw=$("$KSUID_GEN" -f raw 0ujtsYcgvSTl8PAuAdqWYSMnLOv | wc -c)
if [ "$n_raw" -ne 20 ]; then
  echo "expected 20 bytes from -f raw, got $n_raw" >&2
  exit 1
fi

# 8. -f payload emits exactly 16 bytes.
n_pl=$("$KSUID_GEN" -f payload 0ujtsYcgvSTl8PAuAdqWYSMnLOv | wc -c)
if [ "$n_pl" -ne 16 ]; then
  echo "expected 16 bytes from -f payload, got $n_pl" >&2
  exit 1
fi

# 9. Bogus input rejected with non-zero exit.
if "$KSUID_GEN" -f string toolong-toolong-toolong-toolong 2>/dev/null; then
  echo "expected non-zero exit for bad-length input" >&2
  exit 1
fi

# 10. Bogus format rejected.
if "$KSUID_GEN" -f wat 2>/dev/null; then
  echo "expected non-zero exit for unknown format" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# UUIDv7 path -- exercises the --format=uuidv7 long flag and positional
# auto-detect by length (27 = KSUID, 36 = UUIDv7).
# ---------------------------------------------------------------------------

# RFC 9562 Appendix A.6 example: the canonical UUIDv7 used as a
# documentation vector. Its embedded 48-bit timestamp decodes to
# 1645557742000 ms (2022-02-22 19:22:22 UTC).
RFC_UUIDV7=017f22e2-79b0-7cc3-98c4-dc0c0c07398f
RFC_UUIDV7_TS=1645557742000

# 11. --format=uuidv7 emits a 36-char canonical hyphenated UUIDv7.
out=$("$KSUID_GEN" --format=uuidv7)
if [ "${#out}" -ne 36 ]; then
  echo "expected 36-char uuidv7 output, got ${#out}: $out" >&2
  exit 1
fi
case "$out" in
  ????????-????-7???-????-????????????) ;;
  *)
    echo "uuidv7 output not in canonical 8-4-4-4-12 form / wrong version: $out" >&2
    exit 1
    ;;
esac

# 12. -n 3 --format=uuidv7 emits 3 distinct 36-char lines.
out=$("$KSUID_GEN" -n 3 --format=uuidv7)
n_lines=$(printf '%s\n' "$out" | wc -l)
if [ "$n_lines" -ne 3 ]; then
  echo "expected 3 lines from -n 3 --format=uuidv7, got $n_lines:" >&2
  printf '%s\n' "$out" >&2
  exit 1
fi
n_uniq=$(printf '%s\n' "$out" | sort -u | wc -l)
if [ "$n_uniq" -ne 3 ]; then
  echo "expected 3 distinct UUIDv7s, got $n_uniq" >&2
  exit 1
fi
while IFS= read -r line; do
  # Strip trailing CR. Windows MSVC's chronoid-gen.exe writes its
  # text output through stdio in text mode, which converts the
  # newline terminator to "\r\n". `read -r` consumes the LF but
  # leaves the CR in place, inflating ${#line} by 1 on Windows.
  # Stripping a possible trailing CR makes the length check
  # platform-independent.
  line="${line%$'\r'}"
  if [ "${#line}" -ne 36 ]; then
    echo "uuidv7 generation produced a non-36-char line: $line" >&2
    exit 1
  fi
done <<EOF
$out
EOF

# 13. Positional auto-detect: a 36-char hyphenated arg parses as UUIDv7
#     and round-trips back to itself.
back=$("$KSUID_GEN" "$RFC_UUIDV7")
if [ "$back" != "$RFC_UUIDV7" ]; then
  echo "uuidv7 round-trip mismatch: $RFC_UUIDV7 vs $back" >&2
  exit 1
fi

# 14. -f inspect on a UUIDv7 surfaces the new Version / Variant /
#     Timestamp lines while preserving the existing String:/Raw: labels
#     that earlier KSUID assertions rely on.
inspect=$("$KSUID_GEN" -f inspect "$RFC_UUIDV7")
for label in "REPRESENTATION:" "  String:" "     Raw:" \
             "COMPONENTS:" "       Time:" "  Timestamp: $RFC_UUIDV7_TS" \
             "    Version: 7" "    Variant: 0b10"; do
  case "$inspect" in
    *"$label"*) ;;
    *)
      echo "uuidv7 inspect output missing '$label':" >&2
      printf '%s\n' "$inspect" >&2
      exit 1
      ;;
  esac
done

# 15. -f time on a UUIDv7 prints UTC; pin the date prefix so we don't
#     depend on the local TZ. The RFC example's ms timestamp lands at
#     2022-02-22 19:22:22 UTC.
t=$("$KSUID_GEN" -f time "$RFC_UUIDV7")
case "$t" in
  "2022-02-22 19:22:22"*"+0000 UTC") ;;
  *)
    echo "uuidv7 -f time output unexpected: $t" >&2
    exit 1
    ;;
esac

# 16. -f timestamp on a UUIDv7 prints the integer ms.
ts=$("$KSUID_GEN" -f timestamp "$RFC_UUIDV7")
if [ "$ts" != "$RFC_UUIDV7_TS" ]; then
  echo "expected uuidv7 timestamp $RFC_UUIDV7_TS, got $ts" >&2
  exit 1
fi

# 17. -f raw on a UUIDv7 emits exactly 16 raw bytes (binary), mirroring
#     KSUID's -f raw which emits 20 raw bytes. The byte image must
#     hex-decode to the RFC 9562 §6.13 example.
n_raw_v7=$("$KSUID_GEN" -f raw "$RFC_UUIDV7" | wc -c)
if [ "$n_raw_v7" -ne 16 ]; then
  echo "expected 16 bytes from uuidv7 -f raw, got $n_raw_v7" >&2
  exit 1
fi
raw_hex_v7=$("$KSUID_GEN" -f raw "$RFC_UUIDV7" | od -An -tx1 | tr -d ' \n')
if [ "$raw_hex_v7" != "017f22e279b07cc398c4dc0c0c07398f" ]; then
  echo "expected uuidv7 -f raw bytes 017f22e279b07cc398c4dc0c0c07398f, got $raw_hex_v7" >&2
  exit 1
fi

# 18. --format=ksuid combined with a 36-char positional is a hard error
#     (the parser refuses to silently coerce mismatched format/length).
if "$KSUID_GEN" --format=ksuid "$RFC_UUIDV7" >/dev/null 2>&1; then
  echo "expected non-zero exit for --format=ksuid mismatch on uuidv7 positional" >&2
  exit 1
fi

# 19. Unknown --format=... value rejected.
if "$KSUID_GEN" --format=wat >/dev/null 2>&1; then
  echo "expected non-zero exit for unknown --format value" >&2
  exit 1
fi

# 20. -f payload rejected for UUIDv7 in generation mode (no payload
#     field per RFC 9562; rejection must happen before any output is
#     written so scripted callers don't see a partial stream).
gen_pl_out=$("$KSUID_GEN" --format=uuidv7 -f payload 2>/dev/null || true)
if [ -n "$gen_pl_out" ]; then
  echo "expected empty stdout for -f payload + uuidv7 rejection, got: $gen_pl_out" >&2
  exit 1
fi
gen_pl_err=$("$KSUID_GEN" --format=uuidv7 -f payload 2>&1 >/dev/null || true)
case "$gen_pl_err" in
  *"-f payload is not supported for UUIDv7"*) ;;
  *)
    echo "expected literal '-f payload is not supported for UUIDv7' in stderr, got: $gen_pl_err" >&2
    exit 1
    ;;
esac
if "$KSUID_GEN" --format=uuidv7 -f payload >/dev/null 2>&1; then
  echo "expected non-zero exit for -f payload + --format=uuidv7" >&2
  exit 1
fi
# The verbose flag (-v) must not bypass the rejection: -v is parsed
# the same way as any other option, but the rejection happens up-front
# in main() before any per-id printing, so this exercise pins that
# code-flow guarantee.
if "$KSUID_GEN" -v --format=uuidv7 -f payload >/dev/null 2>&1; then
  echo "expected non-zero exit for -v --format=uuidv7 -f payload" >&2
  exit 1
fi

# 21. -f payload rejected for UUIDv7 in parse mode (auto-detected by
#     the 36-char positional length).
if "$KSUID_GEN" -f payload "$RFC_UUIDV7" >/dev/null 2>&1; then
  echo "expected non-zero exit for -f payload on uuidv7 positional" >&2
  exit 1
fi
parse_pl_err=$("$KSUID_GEN" -f payload "$RFC_UUIDV7" 2>&1 >/dev/null || true)
case "$parse_pl_err" in
  *"-f payload is not supported for UUIDv7"*) ;;
  *)
    echo "expected literal '-f payload is not supported for UUIDv7' in stderr (parse mode), got: $parse_pl_err" >&2
    exit 1
    ;;
esac

# 22. -h text advertises the KSUID-only restriction so the user-facing
#     contract is discoverable without reading the source.
help_text=$("$KSUID_GEN" -h 2>&1)
case "$help_text" in
  *"-f payload is KSUID-only"*) ;;
  *)
    echo "expected -h text to advertise '-f payload is KSUID-only', got:" >&2
    printf '%s\n' "$help_text" >&2
    exit 1
    ;;
esac

echo "chronoid-gen integration: all checks passed"
