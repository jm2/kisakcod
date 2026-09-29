#ifndef KISAK_MSVC_CRT_COMPAT_H
#define KISAK_MSVC_CRT_COMPAT_H

// MSVC CRT / winnt spellings the decompiled engine calls directly
// (PLATFORM_POSIX.md's "MSVC CRT names" list). Like msvc_printf_shim.h, MSVC
// keeps its own CRT untouched: everything below is guarded out for _MSC_VER.
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
#define basename kisak_glibc_basename_hidden
#include <string.h>
#undef basename

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

#endif // !defined(_WIN32)

#endif // !defined(_MSC_VER)

#endif // KISAK_MSVC_CRT_COMPAT_H
