#ifndef KISAK_MSVC_CRT_COMPAT_H
#define KISAK_MSVC_CRT_COMPAT_H

// MSVC CRT / winnt spellings the decompiled engine calls directly
// (PLATFORM_POSIX.md's "MSVC CRT names" list plus _BitScanReverse, #218).
// Like msvc_printf_shim.h, MSVC keeps its own CRT untouched: everything
// below is guarded out for _MSC_VER.
//
// Include this header BEFORE the TU's C standard headers: the glibc
// basename() rename only works while <string.h> is unparsed, so
// universal/q_shared.h includes it ahead of its own system includes.
#if !defined(_MSC_VER)

// ---- basename vs glibc (first: the rename must win the include race) ----
// glibc's <string.h> declares a C++ basename() overload under _GNU_SOURCE
// that collides with the engine's file-scope `basename` buffer in
// qcommon/files.cpp; MSVC has no basename() at all. Rename the libc
// declaration away while <string.h> is first parsed, then drop the macro:
// the engine identifier keeps its exact name and linkage everywhere.
#define basename __kisak_glibc_basename_hidden
#include <string.h>
#undef basename

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

// ---- ARRAYSIZE (winnt.h) ----
// Defined by winnt.h, i.e. only in TUs that include <windows.h>; every other
// TU fails on the ~11 ARRAYSIZE call sites without this. Same expansion as
// the local fallbacks in buildnumber.cpp and com_sndalias_load_obj.cpp.
#ifndef ARRAYSIZE
#define ARRAYSIZE(A) (sizeof(A) / sizeof((A)[0]))
#endif

// mingw (_WIN32) already ships the CRT spellings below from its own headers;
// redefining them would collide. They are missing outright on POSIX.
#if !defined(_WIN32)

// MSVC: char *_strlwr(char *) — in-place lowercase, returns the buffer. The
// engine's I_strlwr is the same ASCII-only walk every call site wants.
static inline char *_strlwr(char *s)
{
    for (char *p = s; *p; ++p)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    }
    return s;
}

// MSVC: int _isnan(double) — nonzero iff NaN. NaN is the only value that
// compares unequal to itself, so this needs no <math.h> state.
static inline int _isnan(double x)
{
    return x != x;
}

// MSVC: __time64_t _time64(__time64_t *) — seconds since the epoch, stored
// to the pointer when non-null and returned either way. The engine types
// those through __int64 (Com_RealTime, Com_OpenLogFile, RB_LogInit), which
// is `long long` on every compiler in play.
static inline long long _time64(long long *dest)
{
    const long long now = (long long)time(NULL);
    if (dest)
        *dest = now;
    return now;
}

// MSVC: struct tm *_localtime64(const __time64_t *) — the companion every
// _time64 call site immediately pairs with. time_t is `long` on LP64 and a
// distinct type from `long long`, so convert through a local.
static inline struct tm *_localtime64(const long long *t)
{
    const time_t tt = (time_t)*t;
    return localtime(&tt);
}

// MSVC: #define _TRUNCATE ((size_t)-1) — the *_snprintf_s count that means
// "fill the buffer, terminator included" (BG_AnimParseError).
#define _TRUNCATE ((size_t)-1)

// MSVC: int _vsnprintf_s(char *, size_t, size_t, const char *, va_list).
// The only engine caller passes _TRUNCATE, but keep the MSVC return contract
// — truncation reports -1 — for the reason msvc_printf_shim.h documents.
static inline int KISAK_vsnprintf_s_trunc(
    char *const buffer, const size_t sizeOfBuffer, const size_t count,
    const char *const format, va_list args)
{
    if (buffer == NULL || format == NULL || sizeOfBuffer == 0)
        return -1;
    size_t limit = (count == _TRUNCATE) ? sizeOfBuffer : count;
    if (limit > sizeOfBuffer)
        limit = sizeOfBuffer;
    const int written = vsnprintf(buffer, limit, format, args);
    if (written < 0 || (size_t)written >= limit)
        return -1;
    return written;
}

#define _vsnprintf_s KISAK_vsnprintf_s_trunc

// MSVC: unsigned char _BitScanReverse(unsigned long *Index, unsigned long
// Mask) — the bit position of the most significant set bit (bit 0 least
// significant), or 0 with *Index untouched for a zero Mask. clang only
// implicitly declares it and g++ does not know it (#218). The template takes
// whatever pointer the call site holds (DWORD is `unsigned long` in the
// Windows SDK, `unsigned int` in the census d3d stub) and stores exactly 32
// bits, matching MSVC's DWORD-sized write.
template <typename TIndex>
static inline unsigned char _BitScanReverse(TIndex *index, unsigned long mask)
{
    const unsigned int bits = (unsigned int)mask;
    if (bits == 0)
        return 0;
    unsigned int store = 31u - (unsigned int)__builtin_clz(bits);
    memcpy(index, &store, sizeof(store));
    return 1;
}

#endif // !defined(_WIN32)

#endif // !defined(_MSC_VER)

#endif // KISAK_MSVC_CRT_COMPAT_H
