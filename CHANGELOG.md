# Changelog

All notable changes to libchronoid are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
1.0.0 is the first stable ABI commitment; from this point forward a
SONAME bump (`libchronoid.so.0` → `libchronoid.so.1`) accompanies
every binary-incompatible change, and the major version bumps with it.

## [Unreleased]

### Fixed (build)

- `meson.build` SSSE3 hex-encode kernel and AVX2 batch kernel
  discriminator switched from `cc.get_argument_syntax() == 'msvc'` to
  `cc.get_id() == 'msvc'`. The previous predicate was true for both
  real MSVC `cl.exe` (which gets free SSSE3 intrinsics off the SSE2
  baseline and needs no flag) AND for `clang-cl` (which inherits
  Clang's strict per-feature target gate and therefore must compile
  the SSSE3 TU with explicit `-mssse3`). On `windows-latest +
  CC=clang-cl` the SSSE3 kernel was hitting `error: always_inline
  function '_mm_shuffle_epi8' requires target feature 'ssse3', but
  would be inlined into function 'chronoid_hex_encode_lower_ssse3'
  that is compiled without support for 'ssse3'`. The fix routes
  clang-cl through the gcc/clang branch where
  `cc.has_argument('-mssse3')` succeeds because clang-cl forwards
  `-m...` flags to its underlying clang driver. The same rewrite is
  applied to the AVX2 block for symmetry; real `cl.exe` keeps its
  `/arch:AVX2` path unchanged. The two other
  `cc.get_argument_syntax() == 'msvc'` checks in `meson.build` (for
  `/experimental:c11atomics` and for `-W…` warning suppression) are
  about flag syntax, not target-feature gating, and remain
  unchanged. No ABI, public-API, or installed-header impact. Closes #12.

## [1.0.0] — 2026-05-02

The first stable release. KSUID (segmentio wire-compatible) and
UUIDv7 (RFC 9562) surfaces are now committed: the ABI is locked at
`libchronoid.so.0`, every public type / constant / function is
covered by tests in the upstream suite, and SemVer applies in full
going forward (additions bump the minor, removals or signature
changes require a SONAME bump and a new major).

This is purely a stabilisation cut — no new public API, no
behavioural change, no SIMD-lane changes versus 0.10.1. Distros,
language bindings, and downstream consumers can pin against
`libchronoid >= 1.0.0` and rely on the documented contract.

### Added (project hygiene)

- `CODE_OF_CONDUCT.md` — Contributor Covenant 2.1 with maintainer
  contact substituted in.
- `CONTRIBUTING.md` — codifies the conventions already implicit in
  git history and CI: meson + ninja build, `gst-indent` +
  `clang-tidy` lint gate, atomic commits with the lowercase `type:`
  prefix and `(issue #N, commit X/Y)` series tagging, the
  additive-during-pre-1.0 ABI contract that closes with this
  release, the SPDX header policy split between LGPL-only and
  dual-licensed files, and the 3-phase PR gate (lint → build/test
  matrix → sanitizers) including the scalar-fallback lanes added in
  0.10.1.
- `SECURITY.md` — GitHub Private Vulnerability Reporting as the
  preferred channel with maintainer email as fallback. Documents
  the supported-version window, best-effort response targets, and an
  in-scope / out-of-scope split for parser memory safety, CSPRNG
  seeding/reseed/fork contract, KSUID and UUIDv7 wire-format
  compliance, and the documented monotonicity invariants.

### Fixed (tests)

- `tests/test_cli.sh` no longer depends on `xxd`. The hex round-trip
  vector at test 17 now uses `od -An -tx1 | tr -d ' \n'` (coreutils,
  essential everywhere) instead of `xxd -p | tr -d '\n'` (vim
  package, not in build chroots by default). Surfaced when the
  Debian/Ubuntu PPA build chroot lacked `xxd` and the silent
  pipe-failure (no `pipefail` in POSIX `set -eu`) produced an empty
  string for the comparison.

### Footprint

A 1.0.0 release build on x86_64, post-`strip --strip-unneeded`:

| Artifact              | Bytes  | Δ vs 0.10.1 |
| :-------------------- | -----: | ----------: |
| libchronoid.so.1.0.0  | 39 072 |           0 |
| libchronoid.a         | 53 862 |      -1 384 |
| chronoid-gen (CLI)    | 31 136 |           0 |

The shared library and CLI binaries are byte-identical to the
0.10.1 release build (same SIMD lanes compiled in, same hardening
flags, same strip pass). The static archive shrinks ~1.4 KB from
ranlib symbol-table differences between toolchain runs and is not
attributable to any source-level change in this release.

## [0.10.1] — 2026-05-01

A pre-1.0 hygiene release: a contract-tightening for the
`chronoid-gen -f payload` projection on UUIDv7, a thread-safety
docstring fix for `chronoid_set_rand`, and CI lanes that exercise
the scalar fallback path. No public-API additions, no SONAME bump,
no behaviour change for callers who were not relying on the rejected
`-f payload` UUIDv7 projection.

### Changed (BREAKING — pre-1.0 contract tightening)

- `chronoid-gen -f payload` is now KSUID-only. Combining `-f payload`
  with `--format=uuidv7` (generation mode) or with a 36-char
  UUIDv7 positional (parse mode) exits non-zero with a stderr
  message pointing at `-f raw` (16-byte image) or `-f inspect`
  (rand_a / rand_b broken out). Rationale: UUIDv7 has no payload
  field per RFC 9562 §5.7 — randomness lives in `rand_a` (12 bits)
  and `rand_b` (62 bits) with version/variant nibbles overlaid, so
  any single "payload" slice is a category error. The previous
  10-byte slice shipped in 0.10.0 was structurally invertible but
  semantically misnamed; failing closed before 1.0.0 keeps the
  released-API contract honest. The `-f payload` projection for
  KSUID is unchanged (16 bytes). Closes #3.

### Changed (docs-only — no behaviour change)

- Clarify the thread-safety contract for `chronoid_set_rand` in
  `chronoid/ksuid.h`: the override is held in two independent atomic
  pointers (one for `fn`, one for `ctx`), not a single atomic pair,
  so a draw racing a swap can observe a crossed `(old_fn, new_ctx)`
  pairing. Callers must quiesce all in-flight
  `chronoid_ksuid_new` / `chronoid_uuidv7_new` calls before swapping
  the override or freeing `ctx`. Cross-referenced from the UUIDv7
  generation prose in `chronoid/uuidv7.h`. No ABI change. Closes #5.

### Added (build / CI)

- New `-Dssse3` meson `feature` option (default `auto`) gating the
  SSSE3 hex-encode kernel for `chronoid_uuidv7_format` on x86_64.
  Default behaviour is byte-identical to 0.10.0 on every supported
  platform; `-Dssse3=disabled` lets packagers and CI exercise the
  scalar fallback in `chronoid/uuidv7/hex.c`. The option gate wraps
  both the MSVC and GCC/Clang branches so the disable actually takes
  effect on Windows.
- Three new blocking PR-gate jobs (`scalar-fallback / no-avx2`,
  `/ no-ssse3`, `/ force-scalar`) in `.github/workflows/ci-pr.yml`,
  mirrored non-blocking with GCC + Clang in `.github/workflows/ci-main.yml`.
  Catches scalar-arm regressions that the SIMD-vs-scalar parity
  tests would otherwise mask on every existing CI lane (where the
  parity test reduces to scalar-vs-scalar because the SIMD kernel is
  compiled in). Closes #6.

### Footprint

A release build on x86_64 (post-`strip --strip-unneeded`):

| Artifact              | Bytes  |
| :-------------------- | -----: |
| libchronoid.so.0.10.1 | 39 072 |
| libchronoid.a         | 55 246 |
| chronoid-gen (CLI)    | 31 136 |

Byte-identical to 0.10.0 on x86_64 GCC; the `-f payload` rejection
landed without a measurable size delta after strip.

## [0.10.0] — 2026-05-01

### Added — UUIDv7 support

UUIDv7 (RFC 9562) generation, parsing, formatting, monotonic sequence,
and bulk encoding -- a sibling format to KSUID under one library,
sharing one CSPRNG, one SIMD dispatch lane, and one set of
wipe/fork-aware safety primitives.

#### Public API additions (`<chronoid/uuidv7.h>`)

- `chronoid_uuidv7_t` -- 16-byte struct with
  `_Static_assert sizeof == CHRONOID_UUIDV7_BYTES` so the
  aggregate-initializer macros stay in lockstep with the struct
  layout.
- `chronoid_uuidv7_err_t` -- error enum:
  `CHRONOID_UUIDV7_OK`, `CHRONOID_UUIDV7_ERR_SIZE`,
  `CHRONOID_UUIDV7_ERR_STR_SIZE`, `CHRONOID_UUIDV7_ERR_STR_VALUE`,
  `CHRONOID_UUIDV7_ERR_RNG`, `CHRONOID_UUIDV7_ERR_TIME_RANGE`.
  Slots `-4` and `-6` left unallocated to keep enum positions
  aligned with `chronoid_ksuid_err_t` for future cross-format
  helpers.
- Constants: `CHRONOID_UUIDV7_BYTES = 16`,
  `CHRONOID_UUIDV7_STRING_LEN = 36`, `CHRONOID_UUIDV7_NIL`,
  `CHRONOID_UUIDV7_MAX`, `CHRONOID_UUIDV7_NIL_INIT`,
  `CHRONOID_UUIDV7_MAX_INIT` (aggregate-initializer macros for
  static-storage / Windows-DLL contexts where the extern symbols
  are not constant expressions).
- Predicates / ordering: `chronoid_uuidv7_is_nil`,
  `chronoid_uuidv7_compare` (memcmp semantics over the 16-byte
  representation).
- Construction: `chronoid_uuidv7_from_bytes`,
  `chronoid_uuidv7_from_parts`, `chronoid_uuidv7_new`,
  `chronoid_uuidv7_new_with_time`.
- Field accessors: `chronoid_uuidv7_unix_ms`,
  `chronoid_uuidv7_version`, `chronoid_uuidv7_variant`.
- Codec: `chronoid_uuidv7_format`, `chronoid_uuidv7_parse`,
  `chronoid_uuidv7_string_batch`.
- Sequence: `chronoid_uuidv7_sequence_t`,
  `chronoid_uuidv7_sequence_init`,
  `chronoid_uuidv7_sequence_next`,
  `chronoid_uuidv7_sequence_bounds`.

#### Implementation

- 4-wide AVX2 bulk kernel for `chronoid_uuidv7_string_batch` with
  runtime CPUID dispatch via a lazy-init atomic-function-pointer
  trampoline (libsodium-style, race-free, no locks, no allocation).
  The `CHRONOID_FORCE_SCALAR` env var (shared with
  `chronoid_ksuid_string_batch`) pins the dispatcher to the scalar
  path at first call.
- SSSE3 single-UUID hex encode kernel selected at compile time on
  x86_64.
- RFC 9562 §6.2 method 1 monotonic sequence: 12-bit sub-ms counter
  in the `rand_a` field, timestamp-ahead bump on counter overflow
  (RFC option (a)), clock-backward clamp via `max(wall_ms, last_ms)`,
  CSPRNG-randomized counter init so consecutive sequences sharing
  the same start ms are not predictable from each other's tails.
  Never returns "exhausted"; never stalls.
- Shared `chronoid_set_rand` override now governs both KSUID and
  UUIDv7 random draws via the internal helper
  `chronoid_internal_fill_random` -- one global CSPRNG hookup
  serves both formats.
- `CHRONOID_TESTING`-gated `chronoid_set_time_source_for_testing`
  hook for deterministic monotonicity tests; not exposed in
  release builds.

#### CLI (`chronoid-gen`)

- New `--format=ksuid|uuidv7` long flag selecting the ID format on
  the generation path. Default `ksuid` so the pre-UUIDv7 invocation
  `chronoid-gen` with no flags still emits a KSUID
  (backward-compatible UX).
- Parse-mode positional auto-detect by length: 27 chars → KSUID,
  36 chars → UUIDv7. `--format` and positional length must agree;
  a mismatch is a hard error rather than a silent coercion.
- All `-f` projections (`string`, `inspect`, `time`, `timestamp`,
  `payload`, `raw`) supported for both formats. UUIDv7 inspect adds
  `Version:`, `Variant:`, `rand_a:`, `rand_b:` lines while keeping
  the `String:` / `Raw:` / `Time:` / `Timestamp:` labels that the
  KSUID assertions in `tests/test_cli.sh` already depend on.
- `-f raw` emits the unencoded byte image -- 20 bytes for KSUID,
  16 bytes for UUIDv7. UUIDv7 `-f payload` emits a 10-byte slice
  (12-bit `rand_a` packed into 2 bytes plus 8 bytes of `rand_b`)
  so scripted callers get a stable byte-counted rendering.

#### Tests

- 7 new test executables: `test_uuidv7_smoke`,
  `test_uuidv7_parts`, `test_uuidv7_parse_format`,
  `test_uuidv7_sequence`, `test_uuidv7_new`,
  `test_uuidv7_string_batch`, `test_uuidv7_rfc_layout`.
- AVX2-vs-scalar bulk-encode parity loop ≥ 2²⁰ iterations against
  a deterministic xorshift64\* PRNG corpus.
- RFC 9562 §5.7 byte-layout pin including the §6.13 published
  example `017F22E2-79B0-7CC3-98C4-DC0C0C07398F`. Any future
  refactor of the encode/decode paths that drifts from the RFC
  fails this test before shipping.
- Windows-DLL `__declspec(dllimport)` `_INIT` constexpr regression
  (`tests/test_init_shared.c`) extended to cover the UUIDv7
  sentinels alongside the KSUID ones.

#### Internal renames (predecessors of the UUIDv7 work)

- Public KSUID API renamed `ksuid_*` → `chronoid_ksuid_*` and
  `KSUID_*` → `CHRONOID_KSUID_*` for library-namespace consistency
  (commit 1).
- KSUID implementation TUs moved into `chronoid/ksuid/`
  subdirectory; the public umbrella header stays at
  `chronoid/ksuid.h` (commit 2).
- Internal CSPRNG / wipe / byteorder symbols renamed `ksuid_*` →
  `chronoid_*` (commit 3). Shared infrastructure now serves both
  formats under one namespace.

#### Umbrella header

- New `<chronoid/chronoid.h>` umbrella that includes both
  `<chronoid/ksuid.h>` and `<chronoid/uuidv7.h>` plus the generated
  `<chronoid/chronoid_version.h>`. Callers that want the full library
  surface in one include now have a stable single-header entry point;
  the per-format headers continue to be the authoritative declarations.

#### CI / portability fixes

- `tests/test_uuidv7_sequence.c` no longer asserts a tight upper bound
  on `chronoid_uuidv7_sequence_next` counter-overflow timestamps; the
  loose check is robust to MALLOC_PERTURB_-induced scheduling jitter
  (closes #2).
- `tests/test_*` use `_putenv_s` on Windows MSVC; `setenv` is POSIX-only.
- Stale `libksuid` path references purged from `.github/workflows/*`.
- Source tree reformatted via `gst-indent` for the CI lint gate.
- `chronoid_uuidv7_sequence_next` counter draw silenced under
  clang-tidy `bugprone-narrowing-conversions`.

## [0.9.0] — 2026-05-01

Initial libchronoid release. The pre-release plateau covering the
libksuid → libchronoid rebrand and infrastructure groundwork for
UUIDv7. The 0.10.0 release that follows adds UUIDv7 (RFC 9562) on
top of this base.

### Project rename and scope expansion
- **Renamed from libksuid to libchronoid.** The KSUID half of the
  codebase is unchanged byte-for-byte at the algorithmic level; only
  the project name, header path, library name, and SONAME move.
  - Header path: `<libksuid/ksuid.h>` → `<chronoid/ksuid.h>`
  - Library: `libksuid.so.1` → `libchronoid.so.0`
  - pkg-config: `libksuid.pc` → `libchronoid.pc`
  - CLI: `ksuid-gen` → `chronoid-gen` (semantics unchanged)
  - Internal macros: `KSUID_BUILDING/PUBLIC/HAVE_*/TESTING/VERSION_*`
    and the env var `KSUID_FORCE_SCALAR` renamed to their `CHRONOID_*`
    equivalents.
- **KSUID public API preserved.** `chronoid_ksuid_t`, `chronoid_ksuid_format`,
  `chronoid_ksuid_parse`, `chronoid_ksuid_compare`, `chronoid_ksuid_new`, `chronoid_ksuid_string_batch`,
  `chronoid_ksuid_sequence_*`, `CHRONOID_KSUID_BYTES`, `CHRONOID_KSUID_STRING_LEN`,
  `CHRONOID_KSUID_PAYLOAD_LEN`, `CHRONOID_KSUID_EPOCH_SECONDS`, `CHRONOID_KSUID_NIL`, `CHRONOID_KSUID_MAX`,
  `CHRONOID_KSUID_OK`, `CHRONOID_KSUID_ERR_*`, `CHRONOID_KSUID_NIL_INIT`, `CHRONOID_KSUID_MAX_INIT` all
  retain their names and semantics — they describe the KSUID format,
  not the library, and stay stable across the rename.
- **Predecessor archived.** `semantic-reasoning/libksuid` 1.0.0 is
  preserved as a read-only archive on GitHub. New work, including
  UUIDv7, lands here.
- **License unchanged for inherited files.** TUs derived from
  segmentio/ksuid keep `SPDX: LGPL-3.0-or-later AND MIT`. New TUs
  introduced under libchronoid (UUIDv7 core, hex codec) will carry
  `SPDX: LGPL-3.0-or-later` only, with no MIT clause, since they have
  no Segment lineage.
- **Version policy.** 0.9.0 is the rebrand-only pre-release line.
  UUIDv7 lands in 0.10.0. 1.0.0 is cut once the open
  CI / docs / CLI cleanup items are closed, at which point the
  SONAME freezes at `libchronoid.so.1`.

### Inherited from libksuid 1.0.0
- KSUID generation, parsing, formatting, comparison.
- Per-thread ChaCha20 CSPRNG keyed from the OS entropy source
  (`getrandom` / `getentropy` / `BCryptGenRandom`), reseeded every
  1 MiB / 1 hour / fork / clock-skew event.
- `chronoid_ksuid_string_batch` bulk encoder with runtime CPUID dispatch:
  - x86_64 + AVX2: 8-wide AVX2 kernel using a Granlund-Möller
    floor-reciprocal multiply divide-by-62.
  - All other hosts: scalar `chronoid_ksuid_format` loop.
- SSE2 / NEON acceleration of base62 input validation.
- Monotonic `chronoid_ksuid_sequence_t` with 16-bit suffix counter (≤65 536
  ordered KSUIDs per seed).
- DSE-resistant wipe via `explicit_bzero` / `SecureZeroMemory` /
  `memset_s` / volatile-store fallback.
- Thread-exit wipe registration via `__cxa_thread_atexit_impl`
  (glibc 2.18+, MUSL 1.2+, libc++abi) or `FlsAlloc` (Windows).
- `chronoid-gen` (formerly `ksuid-gen`) CLI for round-trip generation
  and inspection.

[Unreleased]: https://github.com/semantic-reasoning/libchronoid/compare/v0.10.0...HEAD
[0.10.0]: https://github.com/semantic-reasoning/libchronoid/releases/tag/v0.10.0
[0.9.0]: https://github.com/semantic-reasoning/libchronoid/releases/tag/v0.9.0
