// r_warn_newline_tests.cpp: the per-frame renderer warnings of the production
// gfx_d3d/r_warn.cpp. R_WarnOncePerFrame prints s_warnFormat[warnType] with
// Com_PrintWarning(8, "%s", message), so each line must carry its own newline,
// as retail's table does (#988); without one the warning runs into the next
// console line.

#include <gfx_d3d/r_debug.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_init.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_time.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

r_globals_t rg;

namespace
{
char g_printed[2048];
int g_prints;
uint32_t g_msec;
dvar_s g_repeatDelay;
}

void Com_PrintWarning(int, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    // flawfinder: ignore (test-only capture stub forwards the production TU format)
    std::vsnprintf(g_printed, sizeof(g_printed), fmt, va);
    va_end(va);
    ++g_prints;
}
std::uint32_t KISAK_CDECL Sys_Milliseconds() { return g_msec; }
const dvar_s *__cdecl Dvar_RegisterFloat(const char *, float, DvarLimits, uint16_t, const char *) { return &g_repeatDelay; }
void MyAssertHandler(const char *, int, int, const char *, ...) {}

namespace
{
// Warnings whose text differs from retail beyond the newline; #988 leaves them.
bool TextDiffersFromRetail(int type)
{
    switch (type)
    {
    case R_WARN_NONEMISSIVE_FX_MATERIAL:
    case R_WARN_NONLIGHTMAP_MARK_MATERIAL:
    case R_WARN_MISSING_DECL_NONDEBUG:
    case R_WARN_MAX_SCENE_BRUSH_REFS:
    case R_WARN_FOGABLE_2DTEXT:
    case R_WARN_FOGABLE_2DGLYPH:
        return true;
    default:
        return false;
    }
}
}

int main()
{
    g_repeatDelay.current.value = 0.0f;
    r_warningRepeatDelay = &g_repeatDelay;

    int failures = 0;
    for (int type = 0; type < R_WARN_COUNT; ++type)
    {
        if (TextDiffersFromRetail(type))
            continue;
        ++rg.frontEndFrameCount;
        g_msec += 16;
        g_prints = 0;
        g_printed[0] = '\0';
        // Every remaining format takes no argument, %s, %i or %i %i.
        if (std::strstr(s_warnFormat[type], "%s"))
            R_WarnOncePerFrame(static_cast<GfxWarningType>(type), "name");
        else
            R_WarnOncePerFrame(static_cast<GfxWarningType>(type), 7, 8);

        // vsnprintf NUL-terminates within the buffer.
        const std::string_view printed(g_printed);
        if (g_prints != 1 || printed.size() < 2 || printed.back() != '\n' || printed.find('\n') != printed.size() - 1)
        {
            std::fprintf(stderr, "FAIL warning %d: %d prints, \"%s\" does not end in exactly one newline\n", type, g_prints, g_printed);
            ++failures;
        }
    }
    if (failures)
        return 1;
    std::puts("r_warn newline: ok");
    return 0;
}
