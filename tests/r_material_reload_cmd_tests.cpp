// r_material_reload_cmd_tests.cpp: R_Cmd_ReloadMaterialTextures (production
// gfx_d3d/r_material.cpp) for a material that is not loaded. The material name
// comes from the command line, so it must reach Com_Printf as an argument;
// the decompiled command passed it inside the format string.

#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_init.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>

r_globals_t rg;

namespace
{
const char *g_printFormat = nullptr;
const char *g_printArg = nullptr;
const char kName[] = "decal_100%s";
const char kExpected[] = "ReloadMaterialTextures: Material '%s' is not currently loaded.\n";
char g_formatted[] = "(name formatted into the format)";
}

// Reached only for a loaded material, or by its hash lookup.
void __cdecl Image_Reload(GfxImage *)
{
}

void __cdecl RB_BindDefaultImages()
{
}

uint32_t __cdecl R_HashAssetName(const char *)
{
    return 0;
}

int __cdecl Cmd_Argc()
{
    return 2;
}

const char *__cdecl Cmd_Argv(int)
{
    return kName;
}

char *__cdecl va(const char *, ...)
{
    return g_formatted;
}

void QDECL Com_Printf(int, const char *fmt, ...)
{
    g_printFormat = fmt;
    va_list args;
    va_start(args, fmt);
    g_printArg = std::strcmp(fmt, kExpected) == 0 ? va_arg(args, const char *) : nullptr;
    va_end(args);
}

int main()
{
    R_Cmd_ReloadMaterialTextures();
    const bool ok = g_printFormat && std::strcmp(g_printFormat, kExpected) == 0 && g_printArg == kName;
    if (!ok)
    {
        std::fprintf(stderr, "FAIL the material name is not passed as a Com_Printf argument\n");
        return 1;
    }
    std::puts("reloadmaterialtextures: ok");
    return 0;
}
