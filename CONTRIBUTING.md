# Contributing to libchronoid

Thanks for your interest in libchronoid. This document covers what you
need to know to land a change: build, style, commit shape, and the
gates a pull request has to clear.

For the social contract see [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md);
for vulnerability reports see [`SECURITY.md`](SECURITY.md) — please do
not open public issues for security reports.

## Before you start

- **Open an issue first for non-trivial work.** A short design
  conversation up front is cheaper than a rejected PR. Trivial fixes
  (typos, doc nits, build-warning cleanups, obvious one-line bugs) are
  fine to send straight as a PR.
- **Check the pre-1.0 ABI contract.** During the 0.9.x / 0.10.x window
  the public ABI is **additive only**: no removals, no signature
  changes, no semantic changes to existing functions. Anything that
  breaks that contract has to wait for a SONAME bump and needs explicit
  maintainer sign-off in the issue.
- **License footing.** Contributions are accepted under the project
  license: `LGPL-3.0-or-later` for original files, and
  `LGPL-3.0-or-later AND MIT` for files derived from the upstream
  `segmentio/ksuid` Go sources. New files must carry the appropriate
  SPDX header (see `chronoid/ksuid/*.c` for the dual-license pattern
  and `chronoid/uuidv7/*.c` for the LGPL-only pattern). By submitting
  a PR you confirm you have the right to contribute the change under
  these terms.

## Build & test

The project builds with **meson + ninja**. C11, no pthread, no
third-party runtime dependencies.

```sh
meson setup build
meson compile -C build
meson test    -C build
```

A few useful variants:

```sh
# Release build (NDEBUG, optimized).
meson setup build-release --buildtype=release
meson compile -C build-release

# Sanitizers — match the CI Phase-3 lane.
meson setup build-san -Db_sanitize=address,undefined
meson compile -C build-san
meson test    -C build-san

# Force the scalar fallback at runtime (no rebuild).
CHRONOID_FORCE_SCALAR=1 meson test -C build

# Disable a SIMD lane at build time (catches scalar-only regressions).
meson setup build-noavx2 -Davx2=disabled
meson setup build-nossse3 -Dssse3=disabled
```

Add tests for the surface you touched. The bulk encoders carry
**differential parity tests** that cross-check SIMD against scalar
output over ≥ 2²⁰ pseudo-random IDs; if you add a new SIMD kernel,
extend that style rather than inventing your own check.

## Coding style

- **Formatter: `gst-indent`** (the GStreamer indent profile, vendored
  at `tools/gst-indent`). All `.c` / `.h` files in the source tree are
  expected to round-trip through it cleanly. The repo ships a
  pre-commit hook that enforces this on staged files — install it
  with:

  ```sh
  cp hooks/pre-commit.hook .git/hooks/pre-commit
  chmod +x .git/hooks/pre-commit
  ```

  Manual reformatting of a single file:

  ```sh
  ./tools/gst-indent path/to/file.c
  ```

- **`clang-tidy`** runs in CI under the project's `.clang-tidy` config.
  Warnings are gates, not advisories: a new warning fails the lint
  phase. Check locally with `meson compile -C build clang-tidy` (or
  invoke `clang-tidy` directly against the file you changed) before
  pushing.

- **C11 only.** No GNU extensions in public headers. No `pthread`.
  Thread-local state goes through `_Thread_local`, atomics through
  `<stdatomic.h>`. No heap allocations on the hot path.

- **No new runtime dependencies.** The shared library links only
  against the C library (and `bcrypt.lib` on Windows). Adding a
  dependency requires a separate maintainer-approved discussion.

## Commit shape

Commits in this repo are **atomic** — each commit builds clean, passes
the test suite on its own, and has a single clear scope. A multi-step
change goes in as a series of self-contained commits, not as one
mega-commit and not as work-in-progress patches.

The first line follows a lowercase-`type:` prefix:

```
<type>: <short imperative summary>
```

Types in active use: `feat`, `fix`, `docs`, `test`, `ci`, `build`,
`chore`, `style`, `cli`, `core`, `release`. Keep the summary under
~72 characters.

For a change that closes an issue, end the body with `Closes #N`. For
a multi-commit series tied to one issue, mark each commit with
`(issue #N, commit X/Y)` in the subject — see the 0.10.0 UUIDv7 series
or the 0.10.1 scalar-fallback series in `git log` for examples.

Example, single commit:

```
fix(test): drop tight upper bound in test_counter_overflow_bumps_timestamp

The 1-ms bound flaked under heavy CI load on macOS. Loosen to 50 ms;
the test still demonstrates that the timestamp advances on counter
saturation.

Closes #2
```

Example, three-commit series:

```
build: add -Dssse3 meson feature option (issue #6, commit 1/3)
ci:    add scalar fallback PR-gate lanes (issue #6, commit 2/3)
ci:    mirror scalar fallback lanes in ci-main.yml (issue #6, commit 3/3)
```

Prefer to **create new commits** rather than amend or force-push once a
PR is open — review history matters, and the reviewer should not have
to re-read a moving branch.

## Pull-request gates

Every PR runs three sequential CI phases (see
`.github/workflows/ci-pr.yml`). Earlier-phase failures short-circuit
later ones, so a busted formatter does not pay for the full sanitizer
matrix.

1. **Phase 1 — Lint.** `gst-indent` then `clang-tidy`. Hard gate.
2. **Phase 2 — Build & test matrix.** Linux GCC, Linux Clang, macOS
   Clang, Windows MSVC. `fail-fast: off` so you see every platform's
   verdict on a busted PR. Includes the scalar-fallback lanes
   (`/ no-avx2`, `/ no-ssse3`, `/ force-scalar`) that catch regressions
   the SIMD-vs-scalar parity test would otherwise mask.
3. **Phase 3 — Sanitizers.** Linux + macOS ASan + UBSan. Runs only
   after Phase 2 has cleared.

A PR is ready to merge when every required check is green and at
least one maintainer has approved.

## Filing a good issue

Useful bug reports include:

- libchronoid version (`git describe` or release tag).
- OS, compiler, and arch (`uname -a`, `cc --version`).
- Build mode (`debug` / `release`, sanitizers on/off, any non-default
  meson options like `-Davx2=disabled`).
- A minimal reproducer (a 30-line `.c` file is gold; a "doesn't work
  in production" report is not actionable).
- For SIMD or RNG suspicions, the output of
  `CHRONOID_FORCE_SCALAR=1` versus the default — if the bug
  disappears under the scalar fallback, that narrows the search by
  half.

For feature requests, start with the use case, not the proposed
implementation. The maintainer reserves the right to push back on
features that contradict the goals listed in the README (small
footprint, zero runtime deps, additive ABI).
