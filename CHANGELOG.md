# Changelog

All notable changes to libchronoid are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html) once
1.0.0 ships. Pre-1.0 versions are additive but make no ABI promise.

## [Unreleased]

### Added in 0.9.x — UUIDv7 support

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

## [0.9.0] — 2026-05-01

Initial libchronoid release. Held at 0.9.0 until UUIDv7 lands and the
1.0.0 line is cut.

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
- **Version policy.** The pre-release line stays at 0.9.0 until the
  UUIDv7 implementation, parity tests, and the unified CLI are stable
  enough to justify cutting 1.0.0 and freezing the SONAME at
  `libchronoid.so.1`.

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

[Unreleased]: https://github.com/semantic-reasoning/libchronoid/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/semantic-reasoning/libchronoid/releases/tag/v0.9.0
