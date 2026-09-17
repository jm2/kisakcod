#include "buildnumber.h"
#include <stdio.h>

// KisakCOD port: __cdecl spelling from the dependency-free compatibility
// leaf; this TU does not include the qcommon composition.
#include <universal/platform_compat.h>

char buildnumbuf[128];

/*
* Original Date: "Thu Oct 04 00:43:04 2007"
* Original Build: 13620
*/

// LWSS: shared between SP/MP for simplicity

// The immutable source revision this binary was built from. A checkout
// resolves HEAD; a `git archive` source tree without `.git` resolves the
// substituted src/source_identity.txt (see
// scripts/extern/resolve_source_identity.cmake). Referencing the generated
// KISAK_SOURCE_COMMIT macro here keeps the value in the compiled artifact, so
// a released binary still records its source identity after the build tree is
// discarded. It is an accessor, not display text, so existing output is
// unchanged.
//
// Referencing the macro is necessary but not sufficient. Release links enable
// dead-code elimination (`/OPT:REF` on MSVC, `--gc-sections` on ELF), and an
// accessor with no production caller is discarded together with its revision
// string, so a shipped binary records no identity at all. Retention is
// enforced here, beside the definition, so every target that compiles this
// translation unit carries the identity without a per-target linker option:
//   * MSVC: the `/include` linker directive forces the decorated accessor into
//     the image even when nothing references it.
//   * GCC/Clang: the `retain` attribute (GCC 11+, Clang 13+) marks the
//     accessor's section as a garbage-collection root. Toolchains predating
//     `retain` leave the attribute empty; they keep the identity only while no
//     section GC is enabled, exactly as before this change.
// The accessor is given C linkage so the linker symbol is stable across
// compilers and architectures, which is what the `/include` directive names.
#if !defined(_MSC_VER) && defined(__has_attribute)
#  if __has_attribute(retain)
#    define KISAK_SOURCE_IDENTITY_RETAIN __attribute__((retain))
#  endif
#endif
#ifndef KISAK_SOURCE_IDENTITY_RETAIN
#  define KISAK_SOURCE_IDENTITY_RETAIN
#endif
extern "C" KISAK_SOURCE_IDENTITY_RETAIN const char *__cdecl getSourceCommit()
{
#if defined(_MSC_VER)
#  if defined(_M_IX86)
#    pragma comment(linker, "/include:_getSourceCommit")
#  else
#    pragma comment(linker, "/include:getSourceCommit")
#  endif
#endif
	return KISAK_SOURCE_COMMIT;
}

char *__cdecl getBuildNumber()
{
#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

	snprintf(buildnumbuf, ARRAYSIZE(buildnumbuf), "%d %s %s", BUILD_NUMBER, __DATE__, __TIME__);
	return buildnumbuf;
}

int getBuildNumberAsInt()
{
	return BUILD_NUMBER;
}