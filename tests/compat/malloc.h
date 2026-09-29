/* Host shim for <malloc.h>, for libcs that do not ship that name.
 *
 * deps/ode/common.h -- reached from universal/com_math.cpp through
 * xanim/dobj.h -> ode/ode.h -- includes <malloc.h> and <memory.h>
 * unconditionally to get alloca and the malloc family. Those are
 * glibc/MSVC spellings; macOS and the BSDs keep the same declarations in
 * <stdlib.h>/<alloca.h> and <string.h>, and <malloc.h> is simply absent
 * there, so the include is fatal on those hosts.
 *
 * tests/cmake/misc.cmake puts this directory on the include path only when
 * a configure check finds no <malloc.h> at all, so glibc and MSVC keep
 * their real headers and never resolve to this file. Build-only: no retail
 * translation unit has tests/compat on its include line. Every libc this
 * is reached from provides <alloca.h>; glibc's own <malloc.h> would have
 * been found instead of this file.
 */
#pragma once

#include <stdlib.h>
#include <alloca.h>
