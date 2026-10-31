// win_localize_tests.cpp: where the Windows client finds localization.txt
// (win32/win_localize.cpp, #982). Retail reads it from the working directory
// only; an installed client started elsewhere then showed its startup prompts
// as raw WIN_* keys. Win_InitLocalizationFrom also looks under the command
// line's "+set fs_basepath" and in the exe's folder. The checks run from an
// empty working directory against a localization.txt in a temporary folder.

#include <win32/win_localize.h>

#include <qcommon/qcommon.h>
#include <universal/q_shared.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#define WEAK __attribute__((weak))

// The engine boundary: --gc-sections drops what no check reaches. The parser
// and va run as on the main thread.
namespace
{
va_info_t g_vaInfo{};
}
WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsRenderThread() { return false; }
WEAK bool Sys_IsDatabaseThread() { return false; }
WEAK void *Sys_GetValue(int) { return &g_vaInfo; }
WEAK void FS_FileClose(FILE *stream) { std::fclose(stream); }
WEAK void Com_Error(errorParm_t, const char *, ...) { std::abort(); }
WEAK void ProfLoad_BeginTrackedValue(MapProfileTrackedValue) {}
WEAK void ProfLoad_EndTrackedValue(MapProfileTrackedValue) {}
WEAK int SEH_GetLanguageIndexForName(const char *name, int *index)
{
    *index = std::strcmp(name, "russian") == 0 ? 7 : 0;
    return 1;
}

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

std::string BasePath(const char *commandLine)
{
    char path[MAX_OSPATH] = "unset";
    return Win_CommandLineBasePath(commandLine, path, sizeof(path)) ? path : "<none>";
}

void TestCommandLineBasePath()
{
    Check(BasePath("+set fs_basepath C:\\cod4 +set name kc") == "C:\\cod4", "fs_basepath from +set");
    Check(BasePath("+set fs_homepath h +seta fs_basepath \"C:\\Program Files (x86)\\cod4\" +connect x")
              == "C:\\Program Files (x86)\\cod4",
        "a quoted fs_basepath from +seta");
    Check(BasePath("+set fs_basepath a +SET fs_basepath b") == "b", "the last fs_basepath wins");
    // Dvar_Set_f joins the value tokens with single spaces, as
    // Dvar_GetCombinedString does, so an unquoted path keeps its spaces.
    Check(BasePath("+set fs_basepath C:\\Program Files\\KisakCOD  x86 +set name kc")
              == "C:\\Program Files\\KisakCOD x86",
        "an unquoted fs_basepath keeps its spaces");
    Check(BasePath("+set fs_basepath \"C:\\Program Files\\cod4\" +set name kc") == "C:\\Program Files\\cod4",
        "a quoted fs_basepath with spaces");
    // Without a value Dvar_Set_f does nothing, so an earlier value stands.
    Check(BasePath("+set fs_basepath C:\\cod4 +set fs_basepath") == "C:\\cod4", "a valueless last fs_basepath");
    Check(BasePath("+set fs_basepath C:\\cod4 +set fs_basepath  +set name kc") == "C:\\cod4",
        "a valueless fs_basepath before another +set");
    Check(BasePath("+set fs_basepath") == "<none>", "only a valueless fs_basepath sets none");
    Check(BasePath("+set FS_BASEPATH a") == "<none>", "the dvar name matches exactly");
    Check(BasePath("+set name fs_basepath") == "<none>", "only the dvar name counts");
    Check(BasePath("") == "<none>", "an empty command line sets none");
}

void TestFindsLocalization(const std::filesystem::path &install)
{
    const std::string base = install.string();
    const std::string commandLine = "+set fs_homepath home +set fs_basepath \"" + base + "\" +connect 10.0.0.1";

    Check(Win_InitLocalizationFrom(commandLine.c_str(), nullptr) == 7, "fs_basepath's localization.txt sets the language");
    Check(std::strcmp(Win_GetLanguage(), "russian") == 0, "the language is the file's");
    Check(std::strcmp(Win_LocalizeRef("WIN_COMPUTER_CHANGE_BODY"), "Hardware changed") == 0,
        "a prompt key reads its text from fs_basepath's file");

    Check(Win_InitLocalizationFrom("+set name kc", base.c_str()) == 7, "the exe folder's localization.txt is found");
    Check(std::strcmp(Win_LocalizeRef("WIN_COMPUTER_CHANGE_TITLE"), "Hardware") == 0,
        "a prompt key reads its text from the exe folder's file");

    // Neither: English zones, and keys stand for their text (#829).
    Check(Win_InitLocalizationFrom("+set fs_basepath /nonexistent", nullptr) == 0, "no file falls back");
    Check(std::strcmp(Win_GetLanguage(), "english") == 0, "no file assumes English");
    Check(std::strcmp(Win_LocalizeRef("WIN_COMPUTER_CHANGE_BODY"), "WIN_COMPUTER_CHANGE_BODY") == 0,
        "no file returns the key");
}
} // namespace

int main()
{
    namespace fs = std::filesystem;
    // A folder this run creates (create_directory fails if it exists), named
    // for the process and the clock, holds fixed names.
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path()
        / ("kc-win-localize-" + std::to_string(getpid()) + "-" + std::to_string(ticks));
    if (!fs::create_directory(root))
    {
        std::fprintf(stderr, "FAIL the test folder already exists\n");
        return 1;
    }
    const fs::path install = root / "cod4";
    const fs::path elsewhere = root / "elsewhere";
    fs::create_directory(install);
    fs::create_directory(elsewhere);
    std::ofstream(install / "localization.txt")
        << "russian\nWIN_COMPUTER_CHANGE_TITLE\n\"Hardware\"\nWIN_COMPUTER_CHANGE_BODY\n\"Hardware changed\"\n";
    fs::current_path(elsewhere); // no localization.txt here, as for an installed exe

    TestCommandLineBasePath();
    TestFindsLocalization(install);

    fs::current_path(fs::temp_directory_path());
    fs::remove_all(root);
    if (g_failures)
        return 1;
    std::puts("win localize: ok");
    return 0;
}
