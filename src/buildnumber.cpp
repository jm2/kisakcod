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