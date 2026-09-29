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
//
// Scope (NOW.md row 23, split 4a): ARRAYSIZE, _isnan, _BitScanReverse,
// _time64, _TRUNCATE. The _vsnprintf_s and _localtime64 companions are
// included because _TRUNCATE and _time64 are unusable without them — every
// call site pairs the named item with its companion on the next line.
// Split 4b (_strlwr, basename) and 4c (IsValidSeed, BigShort) are separate
// beads.
#if !defined(_MSC_VER)

// ---- basename vs glibc (first: the rename must win the include race) ----
// glibc's <string.h> declares a C++ basename() overload under _GNU_SOURCE
// that collides with the engine's file-scope `basename` buffer in
// qcommon/files.cpp; MSVC has no basename() at all. Rename the libc
// declaration away while <string.h> is first parsed, then drop the macro:
// the engine identifier keeps its exact name and linkage everywhere.
#define basename kisak_glibc_basename_hidden
#include <string.h>
#undef basename

#include <stddef.h>
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
// is `long long` on every compiler in play. C11 timespec_get is the
// year-2038-safe wall-clock spelling this shim is emulating the 64-bit
// API of: no 32-bit epoch truncation anywhere on the path.
static inline long long _time64(long long *dest)
{
    // cppcheck-suppress y2038-unsafe-call -- Every POSIX target this header
    // compiles for (lin64/a64) is LP64 with 64-bit time_t, so timespec/
    // timespec_get carry no 32-bit epoch; the shim exists to provide MSVC's
    // 64-bit __time64_t contract (issue #218) and truncates nothing.
    struct timespec ts;
    // cppcheck-suppress y2038-unsafe-call -- 64-bit time_t on lin64/a64
    // (LP64): this wall-clock read cannot wrap in 2038. The error sentinel
    // below is MSVC's 32-bit-era return contract, not a width limit.
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC)
        ts.tv_sec = (time_t)-1;
    const long long now = (long long)ts.tv_sec;
    if (dest)
        *dest = now;
    return now;
}

// MSVC: struct tm *_localtime64(const __time64_t *) — the companion the
// _time64 call sites in qcommon/common.cpp, universal/com_shared.cpp and
// gfx_d3d/rb_logfile.cpp immediately pair with. time_t is `long` on LP64
// and a distinct type from `long long`, so convert through a local.
// localtime_r fills a caller-owned buffer and returns it: same NULL-on-
// failure and per-thread scratch storage as MSVC's _localtime64.
//
// The scratch buffer is TU-scope thread_local storage, not a function-local
// static (Codacy local-static finding; the TU-scope hoist idiom
// net_chan_capture_tick_tests.cpp documents): MSVC's _localtime64 returns a
// pointer to per-thread scratch that outlives the call, so the storage must
// too. Per-TU copies match the static inline function's own linkage.
static thread_local struct tm kisak_localtime64_tm_buf;
static inline struct tm *_localtime64(const long long *t)
{
    const time_t tt = (time_t)*t;
    // cppcheck-suppress y2038-unsafe-call -- Every POSIX target this header
    // compiles for (lin64/a64) is LP64 with 64-bit time_t, so this reentrant
    // conversion cannot wrap in 2038; it is the POSIX spelling of MSVC's
    // 64-bit _localtime64 contract (issue #218).
    return localtime_r(&tt, &kisak_localtime64_tm_buf);
}

// MSVC: #define _TRUNCATE ((size_t)-1) — the *_snprintf_s count that means
// "fill the buffer, terminator included" (BG_AnimParseError).
#define _TRUNCATE ((size_t)-1)

// MSVC: int _vsnprintf_s(char *, size_t, size_t, const char *, va_list).
// The only engine caller passes _TRUNCATE, but keep the MSVC return contract
// — truncation reports -1 — for the reason msvc_printf_shim.h documents.
// The count arithmetic is the *_s part of the contract; the format
// forwarding itself goes through that file's documented printf-family
// wrapper, which already implements exactly the truncation contract below.
#include "msvc_printf_shim.h"
#include <stdarg.h>

static inline int KISAK_vsnprintf_s_trunc(
    char *const buffer, const size_t sizeOfBuffer, const size_t count,
    const char *const format, va_list args)
{
    if (buffer == NULL || format == NULL || sizeOfBuffer == 0)
        return -1;
    size_t limit = (count == _TRUNCATE) ? sizeOfBuffer : count;
    if (limit > sizeOfBuffer)
        limit = sizeOfBuffer;
    return KISAK_vsnprintf_trunc(buffer, limit, format, args);
}

#define _vsnprintf_s KISAK_vsnprintf_s_trunc

// MSVC: unsigned char _BitScanReverse(unsigned long *Index, unsigned long
// Mask) — the bit position of the most significant set bit (bit 0 least
// significant). clang only implicitly declares it and g++ does not know it
// (#218).
//
// MSVC contract (what is actually guaranteed): return 1 and write the bit
// position to *Index when Mask is nonzero; return 0 when Mask is zero. On
// a zero mask *Index is UNDEFINED — Microsoft documents only the return
// value. The intrinsic lowers to x86 `bsr`, which leaves the destination
// undefined for a zero source (Intel SDM Vol. 2B). Every engine call site
// reads the index only on success, so the undefined state is never
// observed in production. The POSIX shim is stricter and stores nothing on
// failure; that extra guarantee is NOT part of the MSVC contract and is
// pinned as shim-only behavior in tests/msvc_crt_compat_tests.cpp.
//
// The template takes whatever pointer the call site holds (DWORD is
// `unsigned long` in the Windows SDK, `unsigned int` in the census d3d
// stub) and stores exactly 32 bits, matching MSVC's DWORD-sized write.
template <typename TIndex>
static inline unsigned char _BitScanReverse(TIndex *index, unsigned long mask)
{
    const unsigned int bits = (unsigned int)mask;
    if (bits == 0)
        return 0;
    const unsigned int store = 31u - (unsigned int)__builtin_clz(bits);
    // MSVC writes DWORD width (32 bits) through Index even where the
    // caller's pointer is wider (msg_bits_mp.cpp casts an int*), so the copy
    // extent is sizeof(store) and never sizeof(*index) — the destination can
    // always hold it because every call site holds at least a DWORD. Copy
    // those bytes one at a time through unsigned char: the same aliasing-safe
    // write memcpy performs, with the bound spelled out instead of delegated.
    unsigned char *const dst = reinterpret_cast<unsigned char *>(index);
    const unsigned char *const src =
        reinterpret_cast<const unsigned char *>(&store);
    for (size_t i = 0; i < sizeof(store); ++i)
        dst[i] = src[i];
    return 1;
}

#endif // !defined(_WIN32)

#endif // !defined(_MSC_VER)

#endif // KISAK_MSVC_CRT_COMPAT_H
