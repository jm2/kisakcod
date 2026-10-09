// devgui_menu_tests.cpp: the developer GUI's menu table (devgui/devgui.cpp)
// at 64-bit. Free menus are chained through their label bytes, and a handle
// is a menu's index in devguiGlob.menus. Both were x86-only: the chain stored
// 32-bit pointers that a 64-bit read took as garbage, and the handle math
// used DevMenuItem's x86 size (40, not 48). The client crashed in
// DevGui_CreateMenu while registering its sound-curve graphs.
//
// The engine boundary is weak: --gc-sections drops what the checks never
// reach. Com_Error throws, so a test can expect it.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>

#include <devgui/devgui.h>
#include <qcommon/qcommon.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

struct ComError
{
};

dvar_s g_dvar{};

DevMenuItem *Item(std::uint16_t handle)
{
    return DevGui_GetMenu(handle)->menus;
}

std::string Label(int index)
{
    return "menu" + std::to_string(index);
}
} // namespace

// Engine boundary.
WEAK void KISAK_CDECL Com_Error(errorParm_t, const char *, ...)
{
    throw ComError{};
}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    ++g_failures;
}
WEAK const dvar_s *KISAK_CDECL Dvar_RegisterColor(const char *, float, float, float, float, uint16_t, const char *)
{
    return &g_dvar;
}
WEAK const dvar_s *KISAK_CDECL Dvar_RegisterFloat(const char *, float, DvarLimits, uint16_t, const char *)
{
    return &g_dvar;
}
WEAK const dvar_s *KISAK_CDECL Dvar_RegisterInt(const char *, int, DvarLimits, uint16_t, const char *)
{
    return &g_dvar;
}
WEAK const dvar_s *KISAK_CDECL Dvar_RegisterBool(const char *, bool, uint16_t, const char *)
{
    return &g_dvar;
}

WEAK uint32_t KISAK_CDECL DevGui_GetScreenWidth()
{
    return 640;
}
WEAK uint32_t KISAK_CDECL DevGui_GetScreenHeight()
{
    return 480;
}
WEAK void KISAK_CDECL DevGui_InputInit() {}
WEAK void KISAK_CDECL FreeString(const char *) {}
WEAK int I_stricmp(const char *a, const char *b)
{
    return strcasecmp(a, b);
}

int main()
{
    DevGui_Init();

    // The free chain hands out every menu in order, and a handle and its
    // menu map back to each other.
    constexpr int kMenus = static_cast<int>(sizeof(devguiGlob.menus) / sizeof(devguiGlob.menus[0]));
    for (int index = 0; index < kMenus; ++index)
    {
        const std::uint16_t handle = DevGui_CreateMenu(0, Label(index).c_str(), static_cast<__int16>(index));
        CHECK(handle == index + 1);
        CHECK(Item(handle) == &devguiGlob.menus[index]);
        CHECK(DevGui_GetMenuHandle(&devguiGlob.menus[index]) == handle);
        CHECK(Label(index) == devguiGlob.menus[index].label);
    }
    CHECK(DevGui_FindMenu(0, Label(kMenus - 1).c_str()) == kMenus);

    // A full table fails closed.
    bool full = false;
    try
    {
        DevGui_CreateMenu(0, "overflow", 0);
    }
    catch (const ComError &)
    {
        full = true;
    }
    CHECK(full);

    // Freed menus return to the chain and are handed out again.
    const std::uint16_t last = DevGui_FindMenu(0, Label(kMenus - 2).c_str());
    DevGui_FreeMenu_r(last); // the top-level list is sorted: frees the last two
    const std::uint16_t again = DevGui_CreateMenu(0, "again", 0);
    const std::uint16_t twice = DevGui_CreateMenu(0, "twice", 0);
    CHECK(again >= kMenus - 1 && again <= kMenus && twice >= kMenus - 1 && twice <= kMenus && again != twice);
    CHECK(!std::strcmp(Item(again)->label, "again") && !std::strcmp(Item(twice)->label, "twice"));

    if (g_failures == 0)
        std::printf("devgui menus: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
