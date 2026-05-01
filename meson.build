project('libchronoid', 'c',
  version : '0.99.0',
  license : 'LGPL-3.0-or-later AND MIT',
  meson_version : '>=1.1.0',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'b_ndebug=if-release',
    'default_library=both',
  ],
)

cc = meson.get_compiler('c')
pkg = import('pkgconfig')

# Hardening / portability flags applied to all our TUs. Compiler-
# specific defaults split between gcc/clang and MSVC: visibility
# control + POSIX feature macros only mean anything on the GCC-style
# command line, and MSVC needs an explicit opt-in to C11 atomics
# until they leave experimental status.
common_args = ['-DCHRONOID_BUILDING=1']
if cc.get_argument_syntax() == 'msvc'
  # MSVC's <stdatomic.h> is gated behind /experimental:c11atomics
  # until C11 atomics ship as fully supported (Microsoft Learn:
  # /experimental:c11atomics). Without it the header emits
  # #error: "C atomic support is not enabled".
  common_args += ['/experimental:c11atomics']
else
  common_args += [
    '-D_DEFAULT_SOURCE',
    '-D_POSIX_C_SOURCE=200809L',
    '-fvisibility=hidden',
  ]
endif

# OS entropy source detection. Per-host: at most one of these is
# preferred at runtime; the others either are not present (Windows has
# neither getrandom nor getentropy) or are kept compiled-in as a
# secondary fallback (urandom on POSIX).
if cc.has_function('getrandom', prefix : '#include <sys/random.h>')
  common_args += '-DCHRONOID_HAVE_GETRANDOM=1'
endif
if cc.has_function('getentropy', prefix : '#include <unistd.h>')
  common_args += '-DCHRONOID_HAVE_GETENTROPY=1'
endif

# DSE-resistant wipe primitive (issue #2). The explicit_bzero
# prototype lives in different headers on different platforms:
# modern glibc (2.25+), macOS 14.4+, and OpenBSD declare it in
# <string.h>; FreeBSD, NetBSD, and some older glibc / MUSL builds
# put it in <strings.h>. Probe <string.h> first (the common case)
# and fall back to <strings.h>.
#
# Pass _DEFAULT_SOURCE explicitly because meson does not inherit
# add_project_arguments() into has_function() probes, and glibc
# gates the prototype behind that feature macro.
#
# memset_s is the C11 Annex K third option; rarely shipped.
# Windows uses SecureZeroMemory unconditionally (the macro lives
# in <windows.h> and cc.has_function lies on macros, so detect by
# host system instead).
wipe_backend = 'volatile-fallback'
if cc.has_function('explicit_bzero',
    prefix : '#include <string.h>',
    args   : ['-D_DEFAULT_SOURCE'])
  common_args += '-DCHRONOID_HAVE_EXPLICIT_BZERO_STRING_H=1'
  wipe_backend = 'explicit_bzero (<string.h>)'
elif cc.has_function('explicit_bzero',
    prefix : '#include <strings.h>',
    args   : ['-D_DEFAULT_SOURCE'])
  common_args += '-DCHRONOID_HAVE_EXPLICIT_BZERO_STRINGS_H=1'
  wipe_backend = 'explicit_bzero (<strings.h>)'
elif host_machine.system() == 'windows'
  wipe_backend = 'SecureZeroMemory'
elif cc.has_function('memset_s',
    prefix : '#define __STDC_WANT_LIB_EXT1__ 1\n#include <string.h>')
  common_args += '-DCHRONOID_HAVE_MEMSET_S=1'
  # Annex K's memset_s prototype is gated behind __STDC_WANT_LIB_EXT1__
  # on every libc that provides it (glibc, Apple libc, MUSL...).
  # Defining the opt-in macro project-wide -- not just inside wipe.h
  # -- guarantees it is set before <string.h> is first included by
  # ANY translation unit, since wipe.h cannot rewind a prior include
  # that already pulled <string.h> in without the opt-in.
  common_args += '-D__STDC_WANT_LIB_EXT1__=1'
  wipe_backend = 'memset_s (C11 Annex K)'
endif
summary({'wipe backend' : wipe_backend}, section : 'libchronoid')

# Issue #4 thread-exit wipe registration. Probe via cc.links() with
# an explicit prototype rather than cc.has_function() because
# __cxa_thread_atexit_impl is a private libc symbol that no public
# header declares -- has_function would either spuriously fail
# (no prototype) or spuriously pass (linker found a different
# weak alias). Linking with the explicit prototype is the only
# honest signal that the actual ABI matches what we're going to
# call.
thread_exit_backend = 'documented residue (no automatic wipe)'
cxa_thread_atexit_test = '''
extern int __cxa_thread_atexit_impl(void (*fn)(void *), void *arg, void *dso);
extern void *__dso_handle;
static void wipe(void *p) { (void)p; }
int main(void) {
  return __cxa_thread_atexit_impl(wipe, (void *)0, &__dso_handle);
}
'''
if cc.links(cxa_thread_atexit_test,
    name : '__cxa_thread_atexit_impl + __dso_handle')
  common_args += '-DCHRONOID_HAVE_CXA_THREAD_ATEXIT_IMPL=1'
  thread_exit_backend = '__cxa_thread_atexit_impl'
elif host_machine.system() == 'windows'
  common_args += '-DCHRONOID_HAVE_FLS=1'
  thread_exit_backend = 'FlsAlloc (Windows)'
endif
summary({'thread-exit wipe' : thread_exit_backend}, section : 'libchronoid')

# Windows: the supported entropy source is BCryptGenRandom. The whole
# /dev/urandom + getrandom + getentropy chain doesn't apply, and
# rand_os.c switches to a Bcrypt-only path when CHRONOID_HAVE_BCRYPT is
# defined.
extra_link_deps = []
if host_machine.system() == 'windows'
  bcrypt_dep = cc.find_library('bcrypt', required : true)
  extra_link_deps += bcrypt_dep
  common_args += '-DCHRONOID_HAVE_BCRYPT=1'
endif
if cc.get_argument_syntax() != 'msvc'
  foreach a : ['-Wshadow', '-Wstrict-prototypes', '-Wpointer-arith',
               '-Wcast-align', '-Wconversion', '-Wmissing-prototypes']
    if cc.has_argument(a)
      common_args += a
    endif
  endforeach
endif

# libsoup-style layout: the project root is on the include path so
# every #include uses the <chronoid/foo.h> form, which works
# uniformly for in-tree builds, downstream consumers, and the
# generated install tree (${prefix}/include/chronoid/ksuid.h).
inc = include_directories('.')
public_headers = files('chronoid/chronoid.h',
                       'chronoid/ksuid.h',
                       'chronoid/uuidv7.h')

# Generate chronoid/chronoid_version.h alongside the source tree's
# chronoid/. The configure_file lives in chronoid/meson.build so
# its output lands at ${MESON_BUILD_ROOT}/chronoid/chronoid_version.h
# and resolves through |inc| as <chronoid/chronoid_version.h>.
subdir('chronoid')

core_sources = files(
  'chronoid/ksuid/ksuid.c',
  'chronoid/ksuid/base62.c',
  'chronoid/ksuid/sequence.c',
  'chronoid/rand_os.c',
  'chronoid/rand_tls.c',
  'chronoid/chacha20.c',
  'chronoid/ksuid/compare_scalar.c',
  'chronoid/ksuid/encode_batch.c',
  'chronoid/uuidv7/uuidv7.c',
  'chronoid/uuidv7/uuidv7_sequence.c',
  'chronoid/uuidv7/hex.c',
  'chronoid/uuidv7/hex_batch.c',
)

# SIMD / NEON acceleration. SSE2 is part of the x86_64 ABI baseline
# (mandatory since the AMD64 spec) and likewise NEON for aarch64,
# so we compile the per-arch kernel TU directly into core_sources
# without runtime feature detection. ARMv7 NEON is opt-in via
# -mfpu=neon. Other hosts fall back to the scalar translate path
# that lives unconditionally in base62.c.
host_cpu = host_machine.cpu_family()
if get_option('simd') != 'none'
  if host_cpu == 'x86_64'
    core_sources += files(
      'chronoid/ksuid/base62_sse2.c',
      'chronoid/ksuid/compare_sse2.c',
    )
    common_args += '-DCHRONOID_HAVE_SSE2=1'
  elif host_cpu in ['aarch64', 'arm64']
    core_sources += files(
      'chronoid/ksuid/base62_neon.c',
      'chronoid/ksuid/compare_neon.c',
    )
    common_args += '-DCHRONOID_HAVE_NEON=1'
  elif host_cpu == 'arm' and cc.has_argument('-mfpu=neon')
    core_sources += files(
      'chronoid/ksuid/base62_neon.c',
      'chronoid/ksuid/compare_neon.c',
    )
    common_args += '-DCHRONOID_HAVE_NEON=1'
  endif
endif

# SSSE3 hex encode kernel for chronoid_uuidv7_format. Compiled in its
# own static library with -mssse3 because SSSE3 is NOT in the AMD64
# psABI v1 baseline (only SSE2 is); per-file flag isolation mirrors
# how the AVX2 kernel below is wired. SSSE3 is universally available
# on every CPU shipped since 2006 (Intel Core 2, AMD K10), so we treat
# it as compile-time-guaranteed on x86_64 with no runtime CPUID gate.
have_hex_ssse3 = false
ssse3_arg = ''
# The -Dssse3 feature option (value: 'auto' by default) wraps BOTH the
# MSVC and GCC/Clang branches so a `-Dssse3=disabled` flag actually
# disables the kernel everywhere, including on Windows where SSSE3
# intrinsics are normally on by default with no flag. Without this
# outer gate the non-SSSE3 CI lane silently keeps the SSSE3 kernel on
# MSVC builds.
if get_option('simd') != 'none' and host_cpu == 'x86_64' \
    and get_option('ssse3').allowed()
  if cc.get_argument_syntax() == 'msvc'
    # MSVC has no /arch:SSSE3 switch; SSE2 is its lowest baseline and
    # SSSE3 intrinsics are unconditionally available when SSE2 is
    # enabled (which is default on x86_64). No flag needed.
    have_hex_ssse3 = true
  else
    candidate = '-mssse3'
    if cc.has_argument(candidate)
      have_hex_ssse3 = true
      ssse3_arg = candidate
    endif
  endif
  if have_hex_ssse3
    common_args += '-DCHRONOID_HAVE_HEX_SSSE3=1'
  endif
endif

# AVX2 8-wide ksuid_string_batch kernel. Compiled in its own static
# library with -mavx2 (or /arch:AVX2 on MSVC) so the rest of libchronoid
# keeps the SSE2 baseline ABI; the kernel is selected at runtime via
# CPUID inside the dispatcher (chronoid/ksuid/encode_batch.c). Non-x86_64
# hosts skip this entirely; non-AVX2 x86_64 hosts compile the TU but
# never call into it.
have_avx2_batch = false
avx2_arg = ''
if get_option('simd') != 'none' and host_cpu == 'x86_64'
  if get_option('avx2_batch').allowed()
    if cc.get_argument_syntax() == 'msvc'
      candidate = '/arch:AVX2'
    else
      candidate = '-mavx2'
    endif
    if cc.has_argument(candidate)
      have_avx2_batch = true
      avx2_arg = candidate
      common_args += '-DCHRONOID_HAVE_AVX2_BATCH=1'
    endif
  endif
endif

# AVX2 4-wide hex encode kernel for chronoid_uuidv7_string_batch. Uses
# the same -Davx2_batch detection / -mavx2 flag plumbing as the KSUID
# AVX2 batch kernel above; the two kernels share the option and the
# CHRONOID_FORCE_SCALAR runtime env-var override, but each gets its own
# feature macro (CHRONOID_HAVE_AVX2_BATCH for KSUID,
# CHRONOID_HAVE_HEX_AVX2 for UUIDv7) so the dispatcher in
# chronoid/uuidv7/hex_batch.c can compile-out its AVX2 branch
# independently if a future host disables one without the other.
have_hex_avx2 = false
if have_avx2_batch
  have_hex_avx2 = true
  common_args += '-DCHRONOID_HAVE_HEX_AVX2=1'
endif

# Apply collected defines + warnings only after the per-arch SIMD
# section has had its chance to push CHRONOID_HAVE_SSE2 / _NEON /
# _AVX2_BATCH.
add_project_arguments(common_args, language : 'c')

hex_ssse3_lib_dep = []
if have_hex_ssse3
  # PIC required so the same objects can land in both the static and
  # shared variants of `both_libraries('chronoid', ...)` below.
  chronoid_hex_ssse3_lib = static_library('chronoid_hex_ssse3',
    'chronoid/uuidv7/hex_ssse3.c',
    include_directories : inc,
    c_args              : ssse3_arg != '' ? [ssse3_arg] : [],
    pic                 : true,
    install             : false,
  )
  hex_ssse3_lib_dep = [chronoid_hex_ssse3_lib]
endif

avx2_lib_dep = []
if have_avx2_batch
  # PIC required so the same objects can land in both the static and
  # shared variants of `both_libraries('chronoid', ...)` below.
  chronoid_avx2_lib = static_library('chronoid_avx2_batch',
    'chronoid/ksuid/encode_avx2.c',
    include_directories : inc,
    c_args              : [avx2_arg],
    pic                 : true,
    install             : false,
  )
  # link_whole forces every object from the AVX2 static lib into the
  # final library archive even though the call site (the trampoline
  # in encode_batch.c) only ever takes the address of the kernel
  # behind a runtime CPUID check; without link_whole the linker may
  # GC the symbol on shared-library link.
  avx2_lib_dep = [chronoid_avx2_lib]
endif

# UUIDv7 hex AVX2 kernel: separate static_library with -mavx2 so the
# rest of libchronoid keeps the SSE2 baseline ABI. Critic R6.2: this
# TU MUST NOT land in core_sources, or non-AVX2 hosts segfault on
# library load. link_whole below ties the resolved kernel symbol into
# the final shared object even though it's only referenced via the
# atomic function pointer in chronoid/uuidv7/hex_batch.c.
hex_avx2_lib_dep = []
if have_hex_avx2
  chronoid_hex_avx2_lib = static_library('chronoid_hex_avx2',
    'chronoid/uuidv7/hex_avx2.c',
    include_directories : inc,
    c_args              : [avx2_arg],
    pic                 : true,
    install             : false,
  )
  hex_avx2_lib_dep = [chronoid_hex_avx2_lib]
endif

chronoid_lib = both_libraries('chronoid', core_sources,
  include_directories : inc,
  dependencies        : extra_link_deps,
  link_whole          : hex_ssse3_lib_dep + avx2_lib_dep + hex_avx2_lib_dep,
  version             : meson.project_version(),
  # soversion tracks semver major: a major bump means a binary-
  # incompatible ABI break and the SONAME flips with it. 1.0 is the
  # first stable ABI commitment.
  soversion           : '0',
  install             : true,
)

install_headers(public_headers, subdir : 'chronoid')

# License + attribution artifacts MUST ship with every redistribution
# of libchronoid in source or binary form: LGPL-3.0-or-later requires the
# LGPL text, and the upstream MIT terms (LICENSE.MIT) require the
# permission notice "be included in all copies or substantial portions
# of the Software". NOTICE documents how the two combine.
license_dir = get_option('datadir') / 'doc' / 'libchronoid'
install_data(
  files('LICENSE', 'LICENSE.MIT', 'NOTICE'),
  install_dir : license_dir,
)

pkg.generate(chronoid_lib.get_shared_lib(),
  name        : 'libchronoid',
  description : 'C11 toolkit for time-ordered IDs (KSUID + future UUIDv7); successor to libksuid',
  version     : meson.project_version(),
  filebase    : 'libchronoid',
)

if get_option('cli')
  chronoid_gen = executable('chronoid-gen',
    'examples/chronoid-gen.c',
    include_directories : inc,
    link_with : chronoid_lib.get_static_lib(),
    install : true,
  )
endif

if get_option('tests')
  subdir('tests')
endif
