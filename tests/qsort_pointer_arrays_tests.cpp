// qsort_pointer_arrays_tests.cpp: the console's enum autocomplete
// (ConDrawInput_AutoCompleteArg, client/cl_console.cpp) and the loose impact
// effect files (CG_RegisterImpactEffectsForDir, cgame/cg_effects_load_obj.cpp)
// sort arrays of string pointers. Both gave qsort a 4-byte element, so on
// 64-bit they sorted pointer halves and the comparator read spliced pointers;
// typing an r_mode value in the console crashed the client (#949). The
// production TUs at native width; the MP client's defines.
//
// The rest of the engine boundary is weak, and --gc-sections drops the code
// no check reaches.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <cgame/cg_local.h>
#include <client/client.h>
#include <client_mp/client_mp.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/q_parse.h>
#include <universal/q_shared.h>
#include <universal/surfaceflags.h>

#define WEAK __attribute__((weak))

void __cdecl CG_RegisterImpactEffectsForDir(char *dir, EffectFile *effectFile, char *listbuf);

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

std::vector<std::string> g_args;
std::vector<std::string> g_seen;
dvar_s g_hintBoxColor{};
// FS_GetFileList's packed, unsorted names.
const char g_files[] = "wood.csv\0Concrete.csv\0metal.csv\0asphalt.csv";
} // namespace

clientStatic_t cls;
field_t g_consoleField;

WEAK int __cdecl Cmd_Argc() { return static_cast<int>(g_args.size()); }
WEAK const char *__cdecl Cmd_Argv(int arg)
{
    return arg < static_cast<int>(g_args.size()) ? g_args[arg].c_str() : "";
}
WEAK void __cdecl MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void __cdecl Com_Printf(int, const char *, ...) {}
WEAK int __cdecl R_TextWidth(const char *text, int, Font_s *) { return static_cast<int>(std::string_view(text).size()); }
WEAK void __cdecl R_AddCmdDrawStretchPic(float, float, float, float, float, float, float, float, const float *, Material *) {}
WEAK void __cdecl R_AddCmdDrawText(const char *text, int, Font_s *, float, float, float, float, float, const float *, int)
{
    g_seen.push_back(text);
}
WEAK void __cdecl Vec4Scale(const float *v, float scale, float *result)
{
    for (int i = 0; i < 4; ++i)
        result[i] = v[i] * scale;
}
WEAK int __cdecl FS_GetFileList(const char *, const char *, FsListBehavior_e, char *listbuf, int)
{
    std::copy(std::begin(g_files), std::end(g_files), listbuf);
    return 4;
}
WEAK uint32_t __cdecl FS_FOpenFileByMode(char *qpath, int *f, fsMode_t)
{
    g_seen.push_back(qpath);
    *f = 0;
    return static_cast<uint32_t>(-1);
}
WEAK void *__cdecl Sys_GetValue(int)
{
    static va_info_t info{};
    return &info;
}
// Unreached: every file fails to open, so none is read or parsed.
WEAK uint32_t __cdecl FS_Read(uint8_t *, uint32_t, int) { return 0; }
WEAK void __cdecl FS_FCloseFile(int) {}
WEAK void *__cdecl Hunk_AllocateTempMemoryHigh(int, const char *) { return nullptr; }
WEAK void Hunk_ClearTempMemoryHigh() {}
WEAK uint32_t *__cdecl Hunk_AllocateTempMemory(int, const char *) { return nullptr; }
WEAK void Hunk_CheckTempMemoryHighClear() {}
WEAK void __cdecl Com_BeginParseSession(const char *) {}
WEAK void __cdecl Com_EndParseSession() {}
WEAK void __cdecl Com_SetCSV(int) {}
WEAK parseInfo_t *__cdecl Com_Parse(const char **) { return nullptr; }
WEAK parseInfo_t *__cdecl Com_ParseOnLine(const char **) { return nullptr; }
WEAK void __cdecl Com_SkipRestOfLine(const char **) {}
WEAK int __cdecl Com_SurfaceTypeFromName(const char *) { return 0; }
WEAK void Com_PrintError(int, const char *, ...) {}

namespace
{
// Typing "r_mode 1" lists the matching modes, sorted.
void TestAutoCompleteArgSorted()
{
    con_inputHintBoxColor = &g_hintBoxColor;
    I_strncpyz(g_consoleField.buffer, "r_mode 1", static_cast<int>(sizeof(g_consoleField.buffer)));
    g_args = {"r_mode", "1"};
    g_seen.clear();
    const char *modes[] = {"1920x1200", "1024x768", "1600x1200", "800x600", "1280x800"};
    ConDrawInput_AutoCompleteArg(modes, 5);
    const std::vector<std::string> want = {"1024x768", "1280x800", "1600x1200", "1920x1200"};
    CHECK(g_seen == want);
}

// The loose impact effect files load in case-insensitive name order.
void TestImpactFilesSorted()
{
    static char listbuf[0x10000];
    static EffectFile effectFile;
    g_seen.clear();
    CG_RegisterImpactEffectsForDir(const_cast<char *>("fx"), &effectFile, listbuf);
    const std::vector<std::string> want = {"fx/asphalt.csv", "fx/Concrete.csv", "fx/metal.csv", "fx/wood.csv"};
    CHECK(g_seen == want);
}
} // namespace

int main()
{
    TestAutoCompleteArgSorted();
    TestImpactFilesSorted();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("qsort pointer arrays: ok");
    return 0;
}
