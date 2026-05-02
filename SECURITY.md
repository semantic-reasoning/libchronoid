# Security policy

## Reporting a vulnerability

**Please do not file public GitHub issues for security reports.** Public
issues become indexable within minutes and remove the maintainer's
ability to coordinate a fix before disclosure.

Use one of the private channels below instead.

### Preferred — GitHub Private Vulnerability Reporting

Open a private advisory at:

> <https://github.com/semantic-reasoning/libchronoid/security/advisories/new>

GitHub PVR keeps the report visible only to repository maintainers and
the people you explicitly add, and it doubles as the workspace where
the fix, the CVE assignment, and the public advisory get prepared.
This is the recommended path for almost every report.

### Fallback — direct email

If GitHub PVR is unavailable to you (account constraints, sandboxed
disclosure pipeline, etc.), email:

> Justin Kim — `justin.joy.9to5@gmail.com`

Subject line `libchronoid security:` plus a one-line summary. PGP is
not currently offered; if the contents are sensitive enough that
plaintext SMTP worries you, prefer the GitHub PVR channel above.

### What to include

- libchronoid version (release tag or commit SHA).
- OS, architecture, compiler and version.
- Build configuration (debug / release, sanitizers on/off, any
  non-default meson options such as `-Davx2=disabled` or
  `-Dssse3=disabled`).
- A minimal reproducer: a short `.c` file plus the build/run commands
  is the gold standard. For RNG / SIMD reports, results both with and
  without `CHRONOID_FORCE_SCALAR=1` materially help triage.
- Your assessment of impact — what an attacker can do, under what
  preconditions.
- Whether you intend to request a CVE and a preferred disclosure
  timeline.

## Response timeline

These are targets, not contractual SLAs — libchronoid is maintained
by a single person and best-effort applies.

| Stage                                | Target              |
| :----------------------------------- | :------------------ |
| Acknowledgement of receipt           | within 3 business days |
| Initial triage and severity estimate | within 7 business days |
| Coordinated disclosure window        | up to 90 days from acknowledgement, sooner if a fix is ready |

If a report is in scope and reproducible, the maintainer will work
with the reporter on a fix, a coordinated public disclosure date, and
credit in the published advisory (opt-in).

If you do not get an acknowledgement within the target window, escalate
by re-sending via the other channel (GitHub PVR ↔ email) — mail
deliverability is the most likely cause of silence.

## Supported versions

Pre-1.0 support is narrow. Only the **most recent 0.10.x patch
release** receives security fixes; older 0.10.x and any 0.9.x release
are end-of-life. The development branch (`main`, currently 0.99.x)
receives fixes as they land.

| Version line          | Status                  | Security fixes |
| :-------------------- | :---------------------- | :------------- |
| `main` (0.99.x dev)   | active development      | yes            |
| 0.10.1                | latest stable           | yes            |
| 0.10.0                | superseded by 0.10.1    | no             |
| 0.9.x                 | superseded              | no             |
| `libksuid` 1.0.0      | archived predecessor    | no — see <https://github.com/semantic-reasoning/libksuid> |

This table tightens once 1.0.0 ships; until then, "stay on the latest
patch" is the entire support story.

## Scope

In scope:

- **Memory safety** in any public API: out-of-bounds read/write,
  use-after-free, uninitialized-read, integer overflow leading to
  undersized allocation or buffer overflow. The parsers
  (`chronoid_ksuid_parse`, `chronoid_uuidv7_parse`) and the bulk
  encoders are particular focus areas.
- **CSPRNG correctness**: failures to seed from the OS source, failures
  to reseed after `fork(2)` or a clock-skew event, or any condition
  that causes two distinct draws to share state. The reseed contract
  is documented in the README and in `chronoid/csprng.c`.
- **Wire-format violations**: a generated KSUID that fails segmentio
  round-trip, or a generated UUIDv7 that fails RFC 9562 §5.7 layout
  pinning, with security-relevant impact (e.g. a downstream consumer
  treats the malformed ID as authoritative).
- **Monotonicity violations** that could be turned into a guess /
  forge / replay primitive against a downstream system that relies on
  the ordering invariants documented for `chronoid_ksuid_new` and
  `chronoid_uuidv7_new`.
- **Build-system issues** that produce silently insecure binaries on a
  supported platform — for example, a meson option whose effect is
  documented as "disable SIMD lane" but which actually disables a
  hardening pass.

Out of scope:

- Misuse of the documented thread-safety contract for
  `chronoid_set_rand`. The override is held in two independent atomic
  pointers; callers must quiesce in-flight `chronoid_*_new` calls
  before swapping it. A race that violates the documented contract is
  a caller bug, not a library bug.
- Vulnerabilities in third-party RNG sources (`getrandom(2)`,
  `getentropy(3)`, `BCryptGenRandom`) themselves. Report those to the
  OS vendor; libchronoid will coordinate downstream once an upstream
  advisory exists.
- Side-channel attacks (timing, cache, power) against the base62
  long-division core or the hex codec. The library does not currently
  claim constant-time properties for these paths; reports here are
  welcome as feature requests on the public issue tracker rather than
  as security advisories.
- Reports against unsupported version lines (see "Supported
  versions"). The fix lands on the supported branch only.
- Distributions where a packager has disabled hardening flags
  (`-D_FORTIFY_SOURCE`, stack protector, etc.) below the upstream
  defaults — the report belongs to the packager.

When in doubt, file the report anyway under GitHub PVR. It is far
better to have a borderline-scope report and triage it down than to
miss a real one because the reporter was not sure.

## Disclosure history

No security advisories have been published for libchronoid as of
2026-05-02. Once advisories exist, they will be linked here and from
[`CHANGELOG.md`](CHANGELOG.md).
