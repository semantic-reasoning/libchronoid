/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Operating system entropy source for libksuid. Uses, in order of
 * preference:
 *
 *   1. getrandom(2) -- Linux >=3.17, FreeBSD >=12, glibc >=2.25,
 *                       MUSL >=1.1.20. Always cryptographic.
 *   2. getentropy(3) -- macOS, OpenBSD. Capped at 256 bytes per call,
 *                       loops below to fill larger buffers.
 *   3. BCryptGenRandom -- Windows; not yet implemented in this file
 *                          (Windows is not a v1 target).
 *   4. /dev/urandom -- legacy Linux / portable fallback.
 *
 * On any failure the function returns -1 and the caller must surface
 * the error rather than degrading to a non-cryptographic source --
 * silently producing predictable bytes from an ID generator is a far
 * worse outcome than a clean error.
 */
#include "rand.h"

#include <errno.h>

/* Pick exactly one preferred fast path based on what meson detected. */
#if defined(KSUID_HAVE_GETRANDOM)
#  include <sys/random.h>
#  define KSUID_HAVE_FAST_PATH 1
#elif defined(KSUID_HAVE_GETENTROPY)
#  include <unistd.h>
#  define KSUID_HAVE_FAST_PATH 1
#endif

/* /dev/urandom fallback is always available on POSIX-ish hosts. */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(KSUID_HAVE_GETRANDOM)
static int
ksuid_random_via_getrandom (uint8_t *buf, size_t n)
{
  size_t off = 0;
  while (off < n) {
    ssize_t r = getrandom (buf + off, n - off, 0);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t) r;
  }
  return 0;
}
#endif

#if defined(KSUID_HAVE_GETENTROPY)
static int
ksuid_random_via_getentropy (uint8_t *buf, size_t n)
{
  /* getentropy() is documented to fail for n > 256. */
  size_t off = 0;
  while (off < n) {
    size_t chunk = (n - off > 256) ? 256 : (n - off);
    if (getentropy (buf + off, chunk) != 0)
      return -1;
    off += chunk;
  }
  return 0;
}
#endif

static int
ksuid_random_via_urandom (uint8_t *buf, size_t n)
{
  int fd;
  do {
    fd = open ("/dev/urandom", O_RDONLY | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0)
    return -1;
  size_t off = 0;
  int rc = 0;
  while (off < n) {
    ssize_t r = read (fd, buf + off, n - off);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      rc = -1;
      break;
    }
    if (r == 0) {
      /* /dev/urandom does not return EOF in normal operation; treat
       * this as a hard failure. */
      rc = -1;
      break;
    }
    off += (size_t) r;
  }
  close (fd);
  return rc;
}

int
ksuid_os_random_bytes (uint8_t *buf, size_t n)
{
  if (n == 0)
    return 0;

#if defined(KSUID_HAVE_GETRANDOM)
  if (ksuid_random_via_getrandom (buf, n) == 0)
    return 0;
#endif

#if defined(KSUID_HAVE_GETENTROPY)
  if (ksuid_random_via_getentropy (buf, n) == 0)
    return 0;
#endif

  return ksuid_random_via_urandom (buf, n);
}
