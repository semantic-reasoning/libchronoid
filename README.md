# libchronoid

A C11 toolkit for time-ordered identifiers. Today it ships a pure-C
port of [`segmentio/ksuid`](https://github.com/segmentio/ksuid)
(20-byte KSUID, 27-char base62, segmentio wire-compatible). UUIDv7
([RFC 9562](https://datatracker.ietf.org/doc/html/rfc9562)) lands on
the road to 1.0 and is the reason this project exists as something
broader than a single-format port. Both formats share one CSPRNG, one
SIMD dispatch lane, and one set of wipe/fork-aware safety primitives.

## Status

**0.9.0 — pre-release.** Held at 0.9.0 until UUIDv7 ships and parity
tests stabilize, at which point the project cuts **1.0.0** and freezes
the SONAME at `libchronoid.so.1`. During the 0.9.x window the ABI is
**additive only** (no removals, no signature changes); the SONAME is
`libchronoid.so.0` and downstream consumers should expect to rebuild
when 1.0 lands. The KSUID API surface inherited from libksuid 1.0.0 is
already feature-complete and tested.

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
  not vectorizable; SSE2/NEON paths accelerate input validation
  (16-byte packed range tests, sentinel-fill on miss).
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

A release build on x86_64 produces (post-`strip --strip-unneeded`):

| Artifact            | Bytes  |
| :------------------ | -----: |
| libchronoid.so.0.9.0   | 26 752 |
| libchronoid.a          | 35 212 |
| chronoid-gen (CLI)     | 22 920 |

(Numbers carried over from libksuid 1.0.0; the rebrand commit changed
no compiled bytes. They will move when UUIDv7 + hex codec land.)

The bulk-encode AVX2 kernel from `chronoid/ksuid/encode_avx2.c` accounts for
roughly 8 KB of the shared-library size; non-AVX2 hosts compile and
link the kernel but never call into it, so the CPUID-gated dispatch
adds no runtime cost to those targets.

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

The full flag set is `-n N`, `-f {string,inspect,time,timestamp,payload,raw}`, `-v`, `-h`. See `chronoid-gen -h` for details.

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
without rebuilding the library).

## Layout

Public headers (`chronoid/ksuid.h`, and later `chronoid/uuidv7.h`)
live at the top of `chronoid/`; format-specific implementation TUs
live in per-format subdirectories so the tree advertises the scope
expansion at the source level. Shared infrastructure (CSPRNG, wipe,
byte-order helpers) sits at the top of `chronoid/` because it serves
every format.

```c
#include <chronoid/ksuid.h>             /* public KSUID API */
#include <chronoid/ksuid/base62.h>      /* internal KSUID helper */
```

After install the public header lands at
`${prefix}/include/chronoid/ksuid.h`, so downstream consumers use the
exact same include line that the in-tree sources do.

```
chronoid/
├── ksuid.h                   public — <chronoid/ksuid.h>
├── chronoid_version.h.in
├── byteorder.h, chacha20.{c,h}, rand.h, rand_os.c, rand_tls.c, wipe.h
│                             shared infra (CSPRNG / wipe / endian)
└── ksuid/                    KSUID implementation TUs
    ├── ksuid.c, sequence.c
    ├── base62.{c,h}, base62_simd.h, base62_{sse2,neon}.c
    ├── compare_simd.h, compare_{scalar,sse2,neon}.c
    ├── encode_batch.{c,h}, encode_avx2.c
    └── divisor_magic.h
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
P. Leach et al.).

The KSUID half of this codebase was first published as
[`libksuid` 1.0.0](https://github.com/semantic-reasoning/libksuid)
(LGPL-3.0-or-later AND MIT) before being absorbed into libchronoid;
the original repository is preserved as a read-only archive.
