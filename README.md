# libchronoid

A C11 toolkit for time-ordered identifiers. Ships two sibling formats
under one library: a pure-C port of
[`segmentio/ksuid`](https://github.com/segmentio/ksuid) (20-byte
KSUID, 27-char base62, segmentio wire-compatible) and a UUIDv7
implementation per
[RFC 9562](https://datatracker.ietf.org/doc/html/rfc9562) (16-byte,
36-char canonical hyphenated hex, 48-bit ms timestamp + 12-bit
sub-ms counter monotonic sequence). Both formats share one CSPRNG,
one SIMD dispatch lane, and one set of wipe/fork-aware safety
primitives.

## Status

**1.0.0 — stable.** First committed ABI. KSUID (segmentio
wire-compatible) and UUIDv7 (RFC 9562) surfaces are both
feature-complete, tested, and locked at `libchronoid.so.0`. SemVer
applies in full from 1.0.0 forward: additions bump the minor;
removals or signature changes require a SONAME bump
(`libchronoid.so.0` → `libchronoid.so.1`) and a new major version.
Distros, language bindings, and downstream consumers can pin
against `libchronoid >= 1.0.0` and rely on the documented contract.

## Provenance

libchronoid is the successor to
[`libksuid` 1.0.0](https://github.com/semantic-reasoning/libksuid)
and inherits its full git history, tests, SIMD kernels, and wire-format
guarantees for KSUID. The rename reflects an enlarged scope: a
single-format port grows into a multi-format toolkit. The KSUID public
API (types, constants, functions) is unchanged byte-for-byte at the
ABI level; only header path (`<chronoid/ksuid.h>` instead of
`<libksuid/ksuid.h>`) and library/SONAME (`libchronoid.so.0` instead
of `libksuid.so.1`) move.

## Goals

- **Pure C11.** No pthread; thread safety via `_Thread_local` storage and
  `<stdatomic.h>` only.
- **meson + ninja** build. Both static and shared libraries plus a
  `chronoid-gen` CLI for round-trip generation and parsing.
- **Wire-compatible** with upstream `segmentio/ksuid` — same 20-byte
  binary layout, same 27-character base62 string encoding, same epoch
  (1400000000 = 2014-05-13 16:53:20 UTC), same ordering invariants.
- **Honest SIMD**: the base62 long-division core is sequential and is
  not vectorizable; SSE2/NEON paths accelerate KSUID input validation
  (16-byte packed range tests, sentinel-fill on miss). UUIDv7 hex
  encoding has an SSSE3 single-UUID kernel and a 4-wide AVX2 bulk
  kernel; both share the same lazy-init dispatcher trampoline as the
  KSUID bulk path.
- **Cross-platform RNG**:
  - Linux: `getrandom(2)` with `/dev/urandom` fallback
  - macOS: `getentropy(3)` (10.12+) with `/dev/urandom` fallback
  - Windows: `BCryptGenRandom` (linked via `bcrypt.lib`)
  - Per-thread ChaCha20 CSPRNG keyed from the OS source, reseeded
    every 1 MiB / hour / fork / clock-skew event.
- **Small footprint**: no heap allocations on the hot path; no
  third-party runtime dependencies. Stripped library is ~18 KB.

## Licensing

libchronoid is distributed under the **GNU Lesser General Public License,
version 3 or later** (see [`LICENSE`](LICENSE)). It is a derivative
work that ports algorithms and binary formats from `segmentio/ksuid`,
which is distributed under the **MIT License** (see
[`LICENSE.MIT`](LICENSE.MIT) for the upstream text). The combined
attribution requirements are described in [`NOTICE`](NOTICE).

Source files derived from upstream Go code carry the SPDX header

```
SPDX-License-Identifier: LGPL-3.0-or-later AND MIT
```

and a pointer back to the upstream source they were ported from.

## Building

```sh
meson setup build
meson compile -C build
meson test    -C build
```

For a release build with NDEBUG and stripped binaries:

```sh
meson setup build-release --buildtype=release
meson compile -C build-release
strip --strip-unneeded build-release/libchronoid.so.*
```

## Footprint

A release build on x86_64 produces (post-`strip --strip-unneeded`,
1.1.0 with UUIDv7 and the shared hex codec linked in):

| Artifact              | Bytes  |
| :-------------------- | -----: |
| libchronoid.so.1.1.0  | 39 072 |
| libchronoid.a         | 53 862 |
| chronoid-gen (CLI)    | 31 136 |

The bulk-encode AVX2 kernel from `chronoid/ksuid/encode_avx2.c` accounts for
roughly 8 KB of the shared-library size, and the UUIDv7 hex AVX2
kernel from `chronoid/uuidv7/hex_avx2.c` adds a similar amount;
non-AVX2 hosts compile and link both kernels but never call into
them, so the CPUID-gated dispatch adds no runtime cost to those
targets.

The shared library has zero runtime dependencies beyond the C library
and, on Windows, `bcrypt.lib`.

## CLI

```sh
$ chronoid-gen
3D3tYHbvwtnqJnHlVV0SnfLhsIl

$ chronoid-gen -n 3
3D3tYDod37FCb5o2znp85EOqeO3
3D3tYG2kSSEmqfHFkSFXeSYyLAa
3D3tYGRMmQOon3PeymhLvYc1yu5

$ chronoid-gen -f inspect 0ujtsYcgvSTl8PAuAdqWYSMnLOv

REPRESENTATION:

  String: 0ujtsYcgvSTl8PAuAdqWYSMnLOv
     Raw: 0669F7EFB5A1CD34B5F99D1154FB6853345C9735

COMPONENTS:

       Time: 2017-10-10 04:00:47 +0000 UTC
  Timestamp: 107608047
    Payload: B5A1CD34B5F99D1154FB6853345C9735
```

UUIDv7 generation and round-trip:

```sh
$ chronoid-gen --format=uuidv7
019de2b2-7c56-7e59-98be-2ed3cffbd12e

$ chronoid-gen --format=uuidv7 -n 3
019de2b2-7c8d-7c2c-9d55-7a1d3a4f6e02
019de2b2-7c8d-7c2d-9c01-bb4e2f1a8c7d
019de2b2-7c8d-7c2e-9b03-4c8f9d2e1a05

$ chronoid-gen -f inspect 017f22e2-79b0-7cc3-98c4-dc0c0c07398f

REPRESENTATION:

  String: 017f22e2-79b0-7cc3-98c4-dc0c0c07398f
     Raw: 017F22E279B07CC398C4DC0C0C07398F

COMPONENTS:

       Time: 2022-02-22 19:22:22.000 +0000 UTC
  Timestamp: 1645557742000
    Version: 7
    Variant: 0b10 (RFC 4122 / 9562)
     rand_a: 0xCC3
     rand_b: 98C4DC0C0C07398F
```

A positional argument is auto-detected by length: 27 characters →
KSUID, 36 characters → UUIDv7. `--format=ksuid|uuidv7` selects the
ID format on the generation path (default `ksuid`, preserving the
pre-UUIDv7 invocation `chronoid-gen` with no flags). `--format` and
positional length must agree in parse mode; a mismatch is a hard
error rather than a silent coercion.

`-f raw` emits the unencoded byte image of the ID -- 20 bytes for
KSUID, 16 bytes for UUIDv7 -- suitable for `> file.bin`.

`-f payload` is KSUID-only. KSUID has a 16-byte payload field
(`chronoid_ksuid_payload`); UUIDv7 has no equivalent — RFC 9562 §5.7
splits the random bits across `rand_a` (12 bits) and `rand_b` (62
bits) with the version and variant nibbles overlaid in the same
bytes. Combining `-f payload` with `--format=uuidv7` (or with a
36-char positional that auto-detects as UUIDv7) is a hard error.
For UUIDv7 use `-f raw` (the 16-byte image) or `-f inspect` (which
prints `rand_a` and `rand_b` separately).

The full flag set is `--format={ksuid,uuidv7}`, `-n N`,
`-f {string,inspect,time,timestamp,payload,raw}`, `-v`, `-h`. See
`chronoid-gen -h` for details.

The Go upstream's `-f template` is intentionally not supported -- a
faithful re-implementation of Go's `text/template` grammar in C is
out of scope, and a "mostly compatible" template engine is worse than
no engine at all.

## Bulk encode

When formatting many KSUIDs at once (database snapshots, log batches,
network bulk responses), use the bulk variant rather than calling
`chronoid_ksuid_format` in a loop:

```c
chronoid_ksuid_t ids[1024];
char    out[1024 * CHRONOID_KSUID_STRING_LEN];   /* no NUL terminators */

chronoid_ksuid_string_batch (ids, out, 1024);
/* ids[i] is now at out[i * CHRONOID_KSUID_STRING_LEN .. (i + 1) * CHRONOID_KSUID_STRING_LEN - 1] */
```

The function is thread-safe for disjoint output buffers and `n == 0`
is a no-op. Output is byte-identical to a `chronoid_ksuid_format` loop --
only the throughput differs.

The implementation dispatches at first call to a kernel selected
from CPU features via an atomic function pointer (libsodium-style
trampoline; race-free without locks or allocation):

- **x86_64 + AVX2**: an 8-wide AVX2 kernel that processes eight
  KSUIDs per outer iteration via a Granlund-Möller floor-reciprocal
  multiply divide-by-62 ([libksuid#13](https://github.com/semantic-reasoning/libksuid/issues/13)).
  The magic constant is auto-generated by `tools/derive-magic.py`,
  pinned in `chronoid/ksuid/divisor_magic.h`, and verified against
  `__uint128_t` integer division on every CI run.
- **Other hosts** (non-AVX2 x86_64, aarch64, arm, ...): a per-ID
  scalar loop equivalent to calling `chronoid_ksuid_format` N times.

Output is byte-identical across kernels; the differential parity
test in `tests/test_string_batch.c` cross-checks the AVX2 kernel
against the scalar reference over ≥ 2²⁰ pseudo-random KSUIDs and a
lane-swap detection corpus.

Setting the environment variable `CHRONOID_FORCE_SCALAR=1` pins the
dispatcher to the scalar path at first call (runtime kill switch
without rebuilding the library). The same env var governs both
`chronoid_ksuid_string_batch` and `chronoid_uuidv7_string_batch`.

## UUIDv7

A 16-byte UUIDv7 per [RFC 9562](https://datatracker.ietf.org/doc/html/rfc9562)
(May 2024). Lex-sortable by binary representation; the leading 48
bits are an unsigned big-endian Unix millisecond timestamp, so two
UUIDv7s minted in different ms compare in time order via plain
`memcmp` over their byte form.

### Wire format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+---------------------------------------------------------------+
|                       unix_ts_ms (48b)                        |
|                                                               |
+-------------------------------+-------------------------------+
|         unix_ts_ms            |  ver(0x7) |     rand_a (12b)  |
+-------------------------------+-------------------------------+
| var(0b10) |                rand_b (62 bits)                   |
+---------------------------------------------------------------+
|                          rand_b                               |
+---------------------------------------------------------------+
```

- bytes 0..5 — 48-bit Unix millisecond timestamp, big-endian.
- byte 6 high nibble = `0x7` (version), low nibble = top 4 bits of
  `rand_a`.
- byte 7 = bottom 8 bits of `rand_a` (12 bits total).
- byte 8 top 2 bits = `0b10` (RFC 9562 variant), bottom 6 bits of
  byte 8 plus bytes 9..15 = `rand_b` (62 bits total).

### String form

36-character canonical hyphenated lowercase hex
(`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`, group lengths 8-4-4-4-12).
Parsing is case-insensitive (`'A'..'F'` and `'a'..'f'` produce the
same nibble); formatting always emits lowercase. The output is NOT
NUL-terminated -- size buffers to `CHRONOID_UUIDV7_STRING_LEN + 1`
and append `'\0'` when a C string is needed.

### API example

```c
#include <chronoid/uuidv7.h>

chronoid_uuidv7_t id;
if (chronoid_uuidv7_new (&id) != CHRONOID_UUIDV7_OK) { /* handle */ }

char s[CHRONOID_UUIDV7_STRING_LEN + 1];
chronoid_uuidv7_format (&id, s);
s[CHRONOID_UUIDV7_STRING_LEN] = '\0';
/* s is "xxxxxxxx-xxxx-7xxx-yxxx-xxxxxxxxxxxx" with y in {8,9,a,b}. */
```

`chronoid_uuidv7_new` reads the wall clock and draws fresh random
bytes from the per-thread CSPRNG. For monotonic runs within the
same process, use the sequence type below.

### Bulk encode

`chronoid_uuidv7_string_batch` mirrors `chronoid_ksuid_string_batch`:
it formats `n` UUIDv7s into a contiguous output buffer of
`n * CHRONOID_UUIDV7_STRING_LEN` bytes (no NUL terminators). On
x86_64 + AVX2 the dispatcher resolves to a 4-wide AVX2 hex kernel
(4 UUIDs per outer iteration); on other hosts it resolves to a
per-UUID scalar loop. Output is byte-identical across kernels and
is cross-checked by a differential parity test over ≥ 2²⁰
pseudo-random UUIDv7s. `CHRONOID_FORCE_SCALAR=1` pins the
dispatcher to the scalar path.

### Monotonic sequence

`chronoid_uuidv7_sequence_t` implements RFC 9562 §6.2 method 1: a
12-bit sub-ms counter occupying the `rand_a` field acts as a
monotonic tiebreaker within a single millisecond, and
`chronoid_uuidv7_sequence_next` redraws the 62-bit `rand_b` tail on
every real ms-tick. On counter overflow within a single ms the
sequence bumps the embedded timestamp by 1 ms and reseeds the
counter (RFC option (a)) -- it never returns "exhausted" and never
stalls. If the system clock moves backwards (NTP step, VM resume)
the sequence clamps to its last emitted ms so monotonicity is
preserved.

The sequence type is **NOT thread-safe**: one
`chronoid_uuidv7_sequence_t` instance per thread. Concurrent calls
from multiple threads on the same instance are undefined behaviour;
multiple threads should each own their own.

### CSPRNG

Random bytes for both `chronoid_uuidv7_new` and the sequence redraw
come from the per-thread ChaCha20 CSPRNG keyed from the OS entropy
source (the same CSPRNG that backs `chronoid_ksuid_new`). The
`chronoid_set_rand` override registered for KSUID generation
governs UUIDv7 random draws as well -- one global hookup serves
both formats.

## Layout

Public headers (`chronoid/chronoid.h`, `chronoid/ksuid.h`,
`chronoid/uuidv7.h`) live at the top of `chronoid/`; format-specific
implementation TUs live in per-format subdirectories so the tree
advertises the scope at the source level. Shared infrastructure
(CSPRNG, wipe, byte-order helpers) sits at the top of `chronoid/`
because it serves every format.

For convenience, `<chronoid/chronoid.h>` is an umbrella header that
pulls in every public surface of libchronoid. Downstream consumers
that want both formats can write a single include:

```c
#include <chronoid/chronoid.h>          /* KSUID + UUIDv7 + version */
```

For TUs that only need one format, prefer the narrower form to keep
compile units lean:

```c
#include <chronoid/ksuid.h>             /* public KSUID API */
#include <chronoid/uuidv7.h>            /* public UUIDv7 API */
#include <chronoid/ksuid/base62.h>      /* internal KSUID helper */
```

After install the public headers land at
`${prefix}/include/chronoid/{chronoid,ksuid,uuidv7}.h`, so downstream
consumers use the exact same include lines that the in-tree sources
do.

```
chronoid/
├── chronoid.h                public umbrella — <chronoid/chronoid.h>
├── ksuid.h                   public — <chronoid/ksuid.h>
├── uuidv7.h                  public — <chronoid/uuidv7.h>
├── chronoid_version.h.in
├── byteorder.h, chacha20.{c,h}, rand.h, rand_os.c, rand_tls.c, wipe.h
│                             shared infra (CSPRNG / wipe / endian)
├── ksuid/                    KSUID implementation TUs
│   ├── ksuid.c, sequence.c
│   ├── base62.{c,h}, base62_simd.h, base62_{sse2,neon}.c
│   ├── compare_simd.h, compare_{scalar,sse2,neon}.c
│   ├── encode_batch.{c,h}, encode_avx2.c
│   └── divisor_magic.h
└── uuidv7/                   UUIDv7 implementation TUs
    ├── uuidv7.c, uuidv7_sequence.c
    ├── hex.{c,h}, hex_simd.h, hex_ssse3.c, hex_avx2.c
    └── hex_batch.{c,h}
examples/                     example consumers; chronoid-gen CLI
tests/                        unit + integration tests
tools/                        build tooling (derive-magic.py, gst-indent)
hooks/                        git hooks (pre-commit code-style check)
```

## Acknowledgements

The KSUID specification, base62 alphabet, encoding scheme, and reference
test vectors all originate from
[`segmentio/ksuid`](https://github.com/segmentio/ksuid) (MIT License,
Copyright (c) 2017 Segment.io). This project would not exist without
that prior art.

The UUIDv7 wire format and monotonicity guidance come from
[RFC 9562](https://datatracker.ietf.org/doc/html/rfc9562) (May 2024,
P. Leach et al.). The §5.7 byte layout (timestamp, version, variant,
`rand_a`, `rand_b` fields) is pinned by `tests/test_uuidv7_rfc_layout.c`
against the RFC §6.13 published example
`017F22E2-79B0-7CC3-98C4-DC0C0C07398F`, so any future refactor of
the encode/decode paths that drifts from the RFC fails the test
suite before shipping.

The KSUID half of this codebase was first published as
[`libksuid` 1.0.0](https://github.com/semantic-reasoning/libksuid)
(LGPL-3.0-or-later AND MIT) before being absorbed into libchronoid;
the original repository is preserved as a read-only archive.
