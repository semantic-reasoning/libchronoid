# Changelog

All notable changes to libchronoid are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html) once
1.0.0 ships. Pre-1.0 versions are additive but make no ABI promise.

## [Unreleased]

### Planned for 1.0.0
- UUIDv7 (RFC 9562) generation, parsing, formatting, monotonic
  sequence (RFC 9562 §6.2 method 1).
- Hex codec (scalar + SSSE3 + AVX2) shared with future hex-formatted
  formats.
- Unified `chronoid-gen` CLI subcommands for KSUID and UUIDv7.

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
- **KSUID public API preserved.** `ksuid_t`, `ksuid_format`,
  `ksuid_parse`, `ksuid_compare`, `ksuid_new`, `ksuid_string_batch`,
  `ksuid_sequence_*`, `KSUID_BYTES`, `KSUID_STRING_LEN`,
  `KSUID_PAYLOAD_LEN`, `KSUID_EPOCH_SECONDS`, `KSUID_NIL`, `KSUID_MAX`,
  `KSUID_OK`, `KSUID_ERR_*`, `KSUID_NIL_INIT`, `KSUID_MAX_INIT` all
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
- `ksuid_string_batch` bulk encoder with runtime CPUID dispatch:
  - x86_64 + AVX2: 8-wide AVX2 kernel using a Granlund-Möller
    floor-reciprocal multiply divide-by-62.
  - All other hosts: scalar `ksuid_format` loop.
- SSE2 / NEON acceleration of base62 input validation.
- Monotonic `ksuid_sequence_t` with 16-bit suffix counter (≤65 536
  ordered KSUIDs per seed).
- DSE-resistant wipe via `explicit_bzero` / `SecureZeroMemory` /
  `memset_s` / volatile-store fallback.
- Thread-exit wipe registration via `__cxa_thread_atexit_impl`
  (glibc 2.18+, MUSL 1.2+, libc++abi) or `FlsAlloc` (Windows).
- `chronoid-gen` (formerly `ksuid-gen`) CLI for round-trip generation
  and inspection.

[Unreleased]: https://github.com/semantic-reasoning/libchronoid/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/semantic-reasoning/libchronoid/releases/tag/v0.9.0
