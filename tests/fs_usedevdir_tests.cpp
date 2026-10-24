// fs_usedevdir_tests.cpp: the production FS_Startup (universal/com_files.cpp)
// with the MP client's defines. The Steam 1.8 executable searches raw,
// raw_shared, devraw and devraw_shared under each root only with
// fs_usedevdir set (bool, default 0, INIT); players and the game directories
// are searched either way. Since 1.4 it also loads only official iw_* IWDs
// (and the localized_* packs) from fs_basepath/main.
//
// The engine boundary is weak; --gc-sections drops the rest of com_files.cpp.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_filesystem.h>
#include <qcommon/sys_local.h>
#include <qcommon/threads.h>
#include <qcommon/unzip.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/q_shared.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// The non-localized directories FS_Startup put on the search path, by name.
std::vector<std::string> SearchedDirs()
{
    std::vector<std::string> dirs;
    for (const searchpath_s *search = fs_searchpaths; search; search = search->next)
    {
        if (search->dir && !search->bLocalized)
            dirs.emplace_back(search->dir->gamedir);
    }
    return dirs;
}

bool Has(const std::vector<std::string> &dirs, const char *dir)
{
    for (const std::string &d : dirs)
    {
        if (d == dir)
            return true;
    }
    return false;
}

void ClearSearchPaths()
{
    while (fs_searchpaths)
    {
        searchpath_s *const next = fs_searchpaths->next;
        std::free(fs_searchpaths->dir);
        std::free(fs_searchpaths);
        fs_searchpaths = next;
    }
}
} // namespace

void __cdecl FS_Startup(char *gameName); // com_files.cpp, not in a header

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "assert %s:%d\n", filename, line);
    std::abort();
}
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error: %s\n", fmt);
    std::abort();
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
std::vector<std::string> g_warnings;
WEAK void Com_PrintWarning(int, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    g_warnings.emplace_back(text);
}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void *Z_Malloc(int size, const char *, int) { return std::calloc(1, size); }
WEAK void Z_Free(void *ptr, int) { std::free(ptr); }

// The rest of the engine boundary: a startup with no iwds and no files.
#define UNREACHED std::abort()
WEAK void __cdecl Cmd_AddCommandInternal(const char *, void(__cdecl *)(), cmd_function_s *) {}
WEAK int Cmd_Argc() { return 0; }
WEAK const char *Cmd_Argv(int) { return ""; }
WEAK void Com_Memset(void *dest, int value, size_t count) { std::memset(dest, value, count); }
WEAK bool __cdecl Com_LogFileOpen() { return false; }
WEAK const char *CopyString(const char *in) { return strdup(in ? in : ""); }
WEAK void __cdecl FreeString(const char *str) { std::free(const_cast<char *>(str)); }
WEAK void __cdecl Dvar_AddCommands() {}
WEAK void KISAK_CDECL Sys_LockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_LockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *) {}
const dvar_t *useFastFile; // registered in main, as com_init does
va_info_t g_vaInfo; // value 1: the one thread's va() buffers
WEAK void *__cdecl Sys_GetValue(int valueIndex) { return valueIndex == 1 ? &g_vaInfo : nullptr; }
WEAK char *__cdecl Sys_Cwd() { return const_cast<char *>("/kisak-test/cwd"); }
WEAK const char *__cdecl Sys_DefaultCDPath() { return ""; }
WEAK bool KISAK_CDECL Sys_FileSystemTrustRoot(const char *) { return true; }
// With g_listIwds set, every root's main holds a stock IWD, a localized pack
// and a custom one; otherwise no directory holds an IWD.
bool g_listIwds = false;
std::vector<std::string> g_opened; // the IWDs FS_LoadZipFile tried to open
WEAK char **__cdecl Sys_ListFiles(const char *directory, const char *, const char *, int *numfiles, int)
{
    static char names[3][32];
    static char *list[4];
    const std::string dir = directory;
    if (!g_listIwds || dir.size() < 5 || dir.compare(dir.size() - 5, 5, "/main") != 0)
    {
        *numfiles = 0;
        return nullptr;
    }
    std::snprintf(names[0], sizeof(names[0]), "iw_00.iwd");
    std::snprintf(names[1], sizeof(names[1]), "localized_english_iw00.iwd");
    std::snprintf(names[2], sizeof(names[2]), "zz_custom.iwd");
    list[0] = nullptr; // the hunk FS_FreeFileList releases
    for (int i = 0; i < 3; ++i)
        list[i + 1] = names[i];
    *numfiles = 3;
    return &list[1];
}
WEAK SysFileSystemListStatus KISAK_CDECL Sys_FileSystemListDirectory(const char *, std::size_t,
                                                                    std::vector<SysFileSystemDirectoryEntry> *)
{
    return SysFileSystemListStatus::Error; // no language folders
}
WEAK void __cdecl Sys_Mkdir(const char *) { UNREACHED; }
WEAK int __cdecl SEH_GetCurrentLanguage() { return 0; }
WEAK int __cdecl SEH_GetLanguageIndexForName(const char *, int *index)
{
    *index = 0;
    return 1;
}
WEAK const char *__cdecl SEH_GetLanguageName(uint32_t) { return "english"; }
WEAK char *__cdecl SEH_SafeTranslateString(char *reference) { return reference; }
WEAK int __cdecl Kisak_rand() { return 0; }
WEAK bool __cdecl Vec4Compare(const float *, const float *) { UNREACHED; }
WEAK int __cdecl Com_BlockChecksumKey32(const uint8_t *, uint32_t, uint32_t) { UNREACHED; }
WEAK char __cdecl Com_FilterPath(const char *, const char *, int) { UNREACHED; }
WEAK int __cdecl FS_FileGetFileSize(FILE *) { UNREACHED; }
WEAK FILE *__cdecl FS_FileOpenReadBinary(const char *) { return nullptr; } // no CD key file
WEAK FILE *__cdecl FS_FileOpenWriteBinary(const char *) { UNREACHED; }
WEAK uint32_t __cdecl FS_FileRead(void *, uint32_t, FILE *) { UNREACHED; }
WEAK uint32_t __cdecl FS_FileWrite(const void *, uint32_t, FILE *) { UNREACHED; }
WEAK char *__cdecl Hunk_CopyString(HunkUser *, const char *) { UNREACHED; }
WEAK void *Hunk_UserAlloc(HunkUser *, uint32_t, int) { UNREACHED; }
WEAK HunkUser *__cdecl Hunk_UserCreate(int, const char *, bool, bool, int) { UNREACHED; }
WEAK void __cdecl Hunk_UserDestroy(HunkUser *) {} // the file lists are static
WEAK unzFile unzOpen(const char *path)
{
    g_opened.emplace_back(path);
    return nullptr; // nothing loads; the attempt is the evidence
}
WEAK int unzClose(unzFile) { UNREACHED; }
WEAK int unzCloseCurrentFile(unzFile) { UNREACHED; }
WEAK int unzOpenCurrentFile(unzFile) { UNREACHED; }
WEAK int unzGetGlobalInfo(unzFile, unz_global_info *) { UNREACHED; }
WEAK int unzGoToFirstFile(unzFile) { UNREACHED; }
WEAK int unzGoToNextFile(unzFile) { UNREACHED; }
WEAK int unzGetCurrentFileInfoPosition(unzFile, unsigned long *) { UNREACHED; }
WEAK int unzSetCurrentFileInfoPosition(unzFile, unsigned long) { UNREACHED; }
WEAK int unzGetCurrentFileInfo(unzFile, unz_file_info *, char *, unsigned long, void *, unsigned long, char *,
                               unsigned long) { UNREACHED; }

int main()
{
    char gameName[] = "main";
    Dvar_Init();
    useFastFile = Dvar_RegisterBool("useFastFile", true, DVAR_INIT, "Enables loading data from fast files");
    Dvar_SetFromStringByName("fs_basepath", "/kisak-test/base"); // what "+set" does before init
    Dvar_SetFromStringByName("fs_homepath", "/kisak-test/home");

    // 1.8's default: no development directories under any root.
    FS_Startup(gameName);
    std::vector<std::string> dirs = SearchedDirs();
    for (const char *dev : {"raw", "raw_shared", "devraw", "devraw_shared"})
        CHECK(!Has(dirs, dev));
    CHECK(Has(dirs, "players"));
    CHECK(Has(dirs, "main"));
    CHECK(Dvar_GetBool("fs_usedevdir") == false);
    ClearSearchPaths();

    // With fs_usedevdir set at startup, they are searched again.
    Dvar_SetFromStringByName("fs_usedevdir", "1");
    FS_Startup(gameName);
    dirs = SearchedDirs();
    for (const char *dev : {"raw", "raw_shared", "devraw", "devraw_shared"})
        CHECK(Has(dirs, dev));
    CHECK(Has(dirs, "players"));
    CHECK(Has(dirs, "main"));
    ClearSearchPaths();

    // Only official IWDs load from fs_basepath/main; a custom one is skipped
    // with 1.8's warning. Other roots' main directories are not filtered.
    g_listIwds = true;
    FS_Startup(gameName);
    const auto opened = [](const char *path) {
        for (const std::string &p : g_opened)
        {
            if (p == path)
                return true;
        }
        return false;
    };
    CHECK(opened("/kisak-test/base/main/iw_00.iwd"));
    CHECK(opened("/kisak-test/base/main/localized_english_iw00.iwd"));
    CHECK(!opened("/kisak-test/base/main/zz_custom.iwd"));
    CHECK(opened("/kisak-test/home/main/zz_custom.iwd"));
    bool warned = false;
    for (const std::string &w : g_warnings)
        warned = warned || w == "WARNING: Invalid IWD zz_custom.iwd in \\main.\n";
    CHECK(warned);
    ClearSearchPaths();

    if (g_failures == 0)
        std::printf("fs usedevdir: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
