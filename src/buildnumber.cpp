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
const char *__cdecl getSourceCommit()
{
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