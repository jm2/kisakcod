/* Host shim for <memory.h>, for libcs that do not ship that name.
 *
 * glibc provides <memory.h> as an alias for <string.h>, and
 * deps/ode/common.h includes it alongside <malloc.h> (see the comment in
 * tests/compat/malloc.h). macOS and the BSDs do not ship the name at all, so
 * the include is fatal there.
 *
 * tests/cmake/misc.cmake puts this directory on the include path only when a
 * configure check finds no <memory.h> at all, so glibc and MSVC keep their
 * real headers and never resolve to this file. Build-only: no retail
 * translation unit has tests/compat on its include line.
 */
#pragma once

#include <string.h>
