#ifndef KISAK_MSVC_PRINTF_SHIM_H
#define KISAK_MSVC_PRINTF_SHIM_H

// MSVC keeps its own CRT implementations untouched: the native
// _vsnprintf/_snprintf already carry the truncation contract below, so
// the shim only exists to reproduce it on POSIX hosts. Guarding the
// contents keeps every host exercising the implementation production
// actually uses — including tests/msvc_printf_shim_tests.cpp, which
// asserts the contract against the real CRT on Windows and against this
// shim on POSIX.
#if !defined(_MSC_VER)

#include <stdarg.h>
#include <stdio.h>

// KisakCOD port: the decompiled sources call the MSVC formatted-print
// spellings directly, and their callers were compiled against the MSVC
// return contract: _vsnprintf/_snprintf report truncation as -1. POSIX
// snprintf/vsnprintf instead return the full required length, so plain
// aliasing silently changed truncation semantics — Com_SaveDvarsToBuffer
// advances its buffer by the return value after only a written<0 check,
// so a translated truncation walked it past the allocation and
// underflowed the remaining count. Keep the POSIX implementations but
// restore the MSVC contract: any output that does not fit, terminator
// included, returns -1. The POSIX truncated-buffer terminator is
// retained, a strictly safer superset of MSVC (which may leave the
// truncated buffer unterminated).
//
// This file exists solely to host these two wrappers: their bodies call
// vsnprintf with a caller-supplied format string, which is the definition
// of a printf-family wrapper and unavoidably trips CWE-134-style
// "use a constant format" scanners (see .codacy.yaml). The truncation
// contract itself is runtime-tested by tests/msvc_printf_shim_tests.cpp
// on every host.
static inline int KISAK_vsnprintf_trunc(
    char *const buffer, const size_t count, const char *const format,
    va_list args)
{
    const int written = vsnprintf(buffer, count, format, args);
    if (written < 0 || (size_t)written >= count)
        return -1;
    return written;
}

static inline int KISAK_snprintf_trunc(
    char *const buffer, const size_t count, const char *const format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer, count, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= count)
        return -1;
    return written;
}

#define _vsnprintf KISAK_vsnprintf_trunc
#define _snprintf KISAK_snprintf_trunc

#endif // !defined(_MSC_VER)

#endif // KISAK_MSVC_PRINTF_SHIM_H
