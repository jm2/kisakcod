// script_engine_harness.cpp: the engine boundary of the production script
// TUs (see script_engine_harness.hpp). Memory keeps the engine's contracts
// (contiguous temp allocations, zeroed, mapped hunk blocks); logging,
// profiling and file I/O are inert; scripts come from the fast-file RawFile
// lookup; Com_Error unwinds to the test like a drop.

#include "script_engine_harness.hpp"

#include <universal/q_parse.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

#include <sys/mman.h>

#include <database/database.h>
#include <qcommon/qcommon.h>
#include <script/scr_debugger.h>
#include <script/scr_evaluate.h>
#include <script/scr_main.h>
#include <script/scr_parsetree.h>
#include <script/scr_stringlist.h>
#include <script/scr_variable.h>
#include <script/scr_vm.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/profile.h>
#include <win32/win_net_debug.h>

namespace
{
// The fast-file RawFile for a <name>.gsc, and the text it points at.
struct ScriptSource
{
    std::string file;
    std::string text;
    RawFile rawfile;
};
std::vector<std::unique_ptr<ScriptSource>> g_sources;

ScriptSource *FindSource(const char *file)
{
    for (const auto &source : g_sources)
        if (source->file == file)
            return source.get();
    return nullptr;
}
std::vector<int> g_reports;
std::jmp_buf *g_dropTarget;
char g_dropMessage[4096];
bool g_scriptSystemInited;
char g_vaBuffers[4][4096];
unsigned g_vaIndex;
int g_mainHandle;

// Engine allocations stay referenced from here, as the hunk keeps them.
std::vector<void *> g_debugMem;
std::vector<void *> g_tempHigh;

struct TestHunk
{
    bool fixed;
    size_t blockSize;
    std::vector<char *> blocks;  // back() is current
    size_t pos;
};
TestHunk *g_tempUser;

// Hunk memory is mapped, as the engine's virtual reservations are, so parse
// nodes and bytecode sit above 4 GiB and a truncated pointer always faults.
char *NewBlock(size_t size)
{
    void *block = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED || reinterpret_cast<uintptr_t>(block) <= UINT32_MAX)
        std::abort();
    return static_cast<char *>(block);
}

[[noreturn]] void Fail(const char *what, const char *detail)
{
    std::fprintf(stderr, "%s: %s\n", what, detail ? detail : "");
    std::exit(3);
}

void ReportBuiltin() { g_reports.push_back(Scr_GetInt(0)); }
}  // namespace

// --- logging, errors and asserts -----------------------------------------
void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d: %s\n", filename ? filename : "?", line, fmt ? fmt : "");
    std::exit(3);
}
void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- passthrough shim; engine callers own the literal format.
    std::vsnprintf(g_dropMessage, sizeof(g_dropMessage), fmt, args);
    va_end(args);
    if (!g_dropTarget)
        Fail("unexpected Com_Error", g_dropMessage);
    std::longjmp(*g_dropTarget, 1);
}
void Sys_Error(const char *error, ...) { Fail("Sys_Error", error); }
void Com_Printf(int, const char *, ...) {}
void Com_PrintWarning(int, const char *, ...) {}
void Com_PrintError(int, const char *, ...) {}
void __cdecl Com_PrintMessage(int, const char *, int) {}
void __cdecl ProfLoad_Begin(const char *) {}
void __cdecl ProfLoad_End() {}
void __cdecl Profile_BeginScripts(uint32_t) {}
void __cdecl Profile_EndScripts(uint32_t) {}
void __cdecl Profile_BeginScript(int) {}
void __cdecl Profile_EndScript(int) {}
ProfileScript profileScript;
void NET_RestartDebug() {}
int __cdecl Sys_IsRemoteDebugClient() { return 0; }
void KISAK_CDECL Sys_EnterCriticalSection(int) {}
void KISAK_CDECL Sys_LeaveCriticalSection(int) {}
std::uint32_t KISAK_CDECL Sys_Milliseconds()
{
    using namespace std::chrono;
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// --- string helpers (engine semantics) ------------------------------------
void Com_Memset(void *dest, const int val, const size_t count) { std::memset(dest, val, count); }
void Com_Memcpy(void *dest, const void *src, const size_t count)
{
    std::copy_n(static_cast<const char *>(src), count, static_cast<char *>(dest));
}
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- passthrough shim; engine callers own the literal format and size.
    const int n = std::vsnprintf(dest, size, fmt, args);
    va_end(args);
    return n;
}
char *QDECL va(const char *format, ...)
{
    char *buffer = g_vaBuffers[g_vaIndex++ & 3];
    va_list args;
    va_start(args, format);
    // Flawfinder: ignore -- passthrough shim; engine callers own the literal format.
    std::vsnprintf(buffer, sizeof(g_vaBuffers[0]), format, args);
    va_end(args);
    return buffer;
}
void I_strncpyz(char *dest, const char *src, int destsize)
{
    std::snprintf(dest, static_cast<size_t>(destsize), "%s", src);
}
int I_stricmp(const char *s0, const char *s1) { return strcasecmp(s0, s1); }
const char *__cdecl I_stristr(const char *s0, const char *substr)
{
    const std::string_view text(s0);
    const std::string_view needle(substr);
    const auto match = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return match == text.end() && !needle.empty() ? nullptr : s0 + (match - text.begin());
}
bool I_iscsym(int c) { return std::isalnum(c) || c == '_'; }
float __cdecl Q_rint(float in) { return static_cast<float>(std::floor(in + 0.5)); }
float Q_fabs(float f) { return std::fabs(f); }

// --- memory ---------------------------------------------------------------
HunkUser *__cdecl Hunk_UserCreate(int maxSize, const char *, bool fixed, bool, int)
{
    auto *user = new TestHunk{fixed, static_cast<size_t>(maxSize), {NewBlock(maxSize)}, 0};
    return reinterpret_cast<HunkUser *>(user);
}
void *Hunk_UserAlloc(HunkUser *handle, uint32_t size, int alignment)
{
    TestHunk *user = reinterpret_cast<TestHunk *>(handle);
    size_t start = (user->pos + alignment - 1) & ~static_cast<size_t>(alignment - 1);
    if (start + size > user->blockSize)
    {
        if (user->fixed || size > user->blockSize)
            Fail("Hunk_UserAlloc", "out of memory");
        user->blocks.push_back(NewBlock(user->blockSize));
        start = 0;
    }
    user->pos = start + size;
    return user->blocks.back() + start;
}
void __cdecl Hunk_UserDestroy(HunkUser *handle)
{
    TestHunk *user = reinterpret_cast<TestHunk *>(handle);
    for (char *block : user->blocks)
        munmap(block, user->blockSize);
    delete user;
}
void __cdecl TempMemoryReset(HunkUser *user) { g_tempUser = reinterpret_cast<TestHunk *>(user); }
char *__cdecl TempMalloc(uint32_t len) { return static_cast<char *>(Hunk_UserAlloc(reinterpret_cast<HunkUser *>(g_tempUser), len, 1)); }
char *__cdecl TempMallocAlignStrict(uint32_t len) { return TempMalloc(len); }
void __cdecl TempMemorySetPos(char *pos) { g_tempUser->pos = static_cast<size_t>(pos - g_tempUser->blocks.back()); }
void *__cdecl Hunk_AllocateTempMemoryHigh(int size, const char *)
{
    g_tempHigh.push_back(std::calloc(1, size));
    return g_tempHigh.back();
}
void Hunk_ClearTempMemoryHigh()
{
    for (void *p : g_tempHigh)
        std::free(p);
    g_tempHigh.clear();
}
void Hunk_CheckTempMemoryClear() {}
void Hunk_CheckTempMemoryHighClear() {}
void *Hunk_AllocDebugMem(uint32_t size)
{
    g_debugMem.push_back(std::calloc(1, size));
    return g_debugMem.back();
}
void __cdecl Hunk_FreeDebugMem(void *) {}
void *Z_Malloc(int size, const char *, int) { return std::calloc(1, size); }
void Z_Free(void *ptr, int) { std::free(ptr); }
char *__cdecl Z_TryVirtualAlloc(int size, const char *, int) { return static_cast<char *>(std::calloc(1, size)); }
void __cdecl Z_VirtualFree(void *ptr) { std::free(ptr); }

// --- files, dvars and assets ----------------------------------------------
namespace
{
dvar_s g_emptyStringDvar = [] {
    dvar_s dvar{};
    dvar.current.string = "";
    return dvar;
}();
dvar_s g_boolDvar{};
}  // namespace
const dvar_s *fs_gameDirVar = &g_emptyStringDvar;
const dvar_s *__cdecl Dvar_RegisterBool(const char *, bool value, uint16_t, const char *)
{
    g_boolDvar.current.enabled = value;
    return &g_boolDvar;
}
uint32_t __cdecl FS_FOpenFileRead(const char *, int *file)
{
    *file = 0;
    return UINT32_MAX;  // not found
}
uint32_t __cdecl FS_FOpenFileByMode(char *, int *file, fsMode_t)
{
    *file = 0;
    return UINT32_MAX;
}
uint32_t __cdecl FS_Read(unsigned char *, uint32_t, int) { return 0; }
void __cdecl FS_FCloseFile(int) {}
XAssetHeader __cdecl DB_FindXAssetHeader(XAssetType type, const char *name)
{
    ScriptSource *source = type == ASSET_TYPE_RAWFILE ? FindSource(name) : nullptr;
    return source ? XAssetHeader(&source->rawfile) : XAssetHeader();
}
char *__cdecl XAnimGetAnimDebugName(const XAnim_s *, uint32_t) { return const_cast<char *>(""); }

// Com_Parse for the field keys file (Scr_AddFieldsForFile): the next
// whitespace-separated token; *data_p becomes null at the end of the text.
parseInfo_t g_parseToken;
void __cdecl Com_BeginParseSession(const char *) {}
void __cdecl Com_EndParseSession() {}
parseInfo_t *__cdecl Com_Parse(const char **data_p)
{
    g_parseToken.token[0] = 0;
    const char *p = *data_p;
    while (p && *p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    if (!p || !*p)
    {
        *data_p = nullptr;
        return &g_parseToken;
    }
    std::size_t n = 0;
    while (p[n] && !std::isspace(static_cast<unsigned char>(p[n])) && n + 1 < sizeof(g_parseToken.token))
        ++n;
    // Flawfinder: ignore (n < sizeof(token), checked above)
    std::memcpy(g_parseToken.token, p, n); // Flawfinder: ignore
    g_parseToken.token[n] = 0;
    *data_p = p + n;
    return &g_parseToken;
}

// --- the game's side of the script interface ------------------------------
void(__cdecl *__cdecl Scr_GetFunction(const char **pName, int *type))()
{
    *type = 0;
    return std::strcmp(*pName, "report") ? nullptr : ReportBuiltin;
}
void(__cdecl *__cdecl Scr_GetMethod(const char **, int *type))(scr_entref_t)
{
    *type = 0;
    return nullptr;
}
void __cdecl Scr_GetObjectField(uint32_t, int, int) { Fail("Scr_GetObjectField", "no entities"); }
int32_t __cdecl Scr_SetObjectField(uint32_t, uint32_t, uint32_t) { Fail("Scr_SetObjectField", "no entities"); }

// --- the harness ----------------------------------------------------------
namespace gsc
{
void SetSource(const std::string &name, const std::string &text)
{
    const std::string file = name + ".gsc";
    ScriptSource *source = FindSource(file.c_str());
    if (!source)
    {
        g_sources.push_back(std::make_unique<ScriptSource>());
        source = g_sources.back().get();
        source->file = file;
    }
    source->text = text;
    source->rawfile = RawFile{source->file.c_str(), static_cast<int>(source->text.size()), source->text.c_str()};
}

void SetLoadFields(const std::string &keys)
{
    ScriptSource *source = FindSource("radiant/keys.txt");
    if (!source)
    {
        g_sources.push_back(std::make_unique<ScriptSource>());
        source = g_sources.back().get();
        source->file = "radiant/keys.txt";
    }
    source->text = keys;
    source->rawfile = RawFile{source->file.c_str(), static_cast<int>(source->text.size()), source->text.c_str()};
}

bool Load(const std::string &name, std::string *error)
{
    if (!g_scriptSystemInited)
    {
        SL_Init();
        Scr_InitVariables();
        Scr_Init();
        Scr_Settings(0, 0, 0);
        g_scriptSystemInited = true;
    }
    g_mainHandle = 0;
    std::jmp_buf drop;
    g_dropTarget = &drop;
    if (setjmp(drop))
    {
        g_dropTarget = nullptr;
        *error = g_dropMessage;
        return false;
    }
    // GScr_LoadScripts, then BG_LoadAnim and G_InitGame's system start.
    Scr_BeginLoadScripts();
    if (ScriptSource *fields = FindSource("radiant/keys.txt"); fields && !fields->text.empty())
        Scr_AddFields("radiant", "txt");
    g_mainHandle = Scr_LoadScript(name.c_str()) ? Scr_GetFunctionHandle(name.c_str(), "main") : 0;
    Scr_PostCompileScripts();
    Scr_EndLoadScripts();
    Scr_EndLoadAnimTrees();
    Scr_InitSystem(1);
    Scr_AllocGameVariable();
    g_dropTarget = nullptr;
    if (!g_mainHandle)
        *error = "no " + name + "::main";
    return g_mainHandle != 0;
}

std::vector<int> RunMain()
{
    if (!g_mainHandle)
        Fail("RunMain", "no script loaded");
    g_reports.clear();
    Scr_FreeThread(Scr_ExecThread(g_mainHandle, 0));
    return g_reports;
}

std::vector<int> RunMain(const std::vector<int> &args)
{
    if (!g_mainHandle)
        Fail("RunMain", "no script loaded");
    g_reports.clear();
    for (auto arg = args.rbegin(); arg != args.rend(); ++arg)
        Scr_AddInt(*arg);
    Scr_FreeThread(Scr_ExecThread(g_mainHandle, static_cast<uint32_t>(args.size())));
    return g_reports;
}

void Unload()
{
    Scr_ShutdownSystem(1, 1);
    Scr_FreeScripts(1);
    Hunk_ClearTempMemoryHigh();
}

bool CompileWatchExpression(const char *text)
{
    ScriptExpression_t expr{};
    scrVarPub.evaluate = 1;
    Scr_CompileText(text, &expr);
    scrVarPub.evaluate = 0;
    const bool ok = expr.parseData.node[0].type != ENUM_bad_expression;
    Scr_FreeDebugExpr(&expr);
    return ok;
}
}  // namespace gsc
