/* Host shim for <memory.h>, for libcs that do not ship that name.
 *
 * glibc provides <memory.h> as an alias for <string.h>, and
 * deps/ode/common.h includes it alongside <malloc.h> (see
 * tests/compat/malloc.h for the full rationale). macOS and the BSDs do not
 * ship the name at all. Same guard as malloc.h: this directory is on the
 * include path only when a configure check finds no <memory.h>, so glibc
 * and MSVC keep their real headers. Build-only.
 */
#pragma once

#include <string.h>
