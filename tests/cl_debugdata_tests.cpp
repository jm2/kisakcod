// cl_debugdata_tests.cpp: CL_UpdateDebugServerData (production
// client/cl_debugdata.cpp), compiled with the client's defines. It copies the
// server debug strings and lines into their buffers. Before the server sends
// any, both lists are empty and their arrays are still null; memcpy must not
// be called with them (UBSan: null passed to a nonnull argument).

#include <client_mp/client_mp.h>
#include <gfx_d3d/r_debug.h>

#include <cstdio>
#include <cstring>

clientStatic_t cls;

void __cdecl CL_UpdateDebugServerData();

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

trDebugString_t g_strings[2];
trDebugString_t g_stringsBuffer[2];
trDebugLine_t g_lines[1];
trDebugLine_t g_linesBuffer[1];

void TestEmptyListsCopyNothing()
{
    std::memset(&cls.debug, 0, sizeof(cls.debug));
    cls.rendererStarted = 1;
    CL_UpdateDebugServerData();
    Check(cls.debug.svStringsBuffer.num == 0 && cls.debug.svLinesBuffer.num == 0, "empty lists stay empty");
}

void TestListsAreCopied()
{
    std::memset(&cls.debug, 0, sizeof(cls.debug));
    cls.rendererStarted = 1;
    std::strcpy(g_strings[1].text, "second");
    g_lines[0].depthTest = 7;
    cls.debug.svStrings.strings = g_strings;
    cls.debug.svStrings.num = 2;
    cls.debug.svStringsBuffer.strings = g_stringsBuffer;
    cls.debug.svLines.lines = g_lines;
    cls.debug.svLines.num = 1;
    cls.debug.svLinesBuffer.lines = g_linesBuffer;
    CL_UpdateDebugServerData();
    Check(cls.debug.svStringsBuffer.num == 2 && std::strcmp(g_stringsBuffer[1].text, "second") == 0,
        "strings are copied");
    Check(cls.debug.svLinesBuffer.num == 1 && g_linesBuffer[0].depthTest == 7, "lines are copied");
}
}

int main()
{
    TestEmptyListsCopyNothing();
    TestListsAreCopied();
    if (g_failures)
        return 1;
    std::puts("cl_debugdata: ok");
    return 0;
}
