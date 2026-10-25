// client_input_buffer_tests.cpp: the console, key-binding and server-browser
// buffer bounds (#197). The production Con_GetTextCopy (client/cl_console.cpp),
// the key-name code (client/cl_keys.cpp) and CL_GlobalServers_f
// (client_mp/cl_main_pc_mp.cpp) at native width; the MP client's defines.
//
// The rest of the engine boundary is weak, and --gc-sections drops the code
// no check reaches.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <client/client.h>
#include <client_mp/client_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/q_shared.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

int g_language = 0;
std::vector<std::string> g_args;
std::string g_sent;
dvar_s g_masterName{};
dvar_s g_masterPort{};
} // namespace

void __cdecl CL_GlobalServers_f();

clientStatic_t cls;
const dvar_t *com_masterServerName;
const dvar_t *com_masterPort;

WEAK int32_t __cdecl SEH_GetCurrentLanguage() { return g_language; }
WEAK int __cdecl Cmd_Argc() { return static_cast<int>(g_args.size()); }
WEAK const char *__cdecl Cmd_Argv(int arg)
{
    return arg < static_cast<int>(g_args.size()) ? g_args[arg].c_str() : "";
}
WEAK void __cdecl Com_Printf(int, const char *, ...) {}
WEAK bool __cdecl Dvar_GetBool(const char *) { return true; }
WEAK int __cdecl NET_StringToAdr(char *, netadr_t *) { return 1; }
WEAK void Com_Error(errorParm_t, const char *, ...) {}
WEAK bool __cdecl NET_OutOfBandPrint(netsrc_t, netadr_t, const char *data)
{
    g_sent = data;
    return true;
}
WEAK void __cdecl MyAssertHandler(const char *, int, int, const char *, ...) {}

namespace
{
// French digit keys name the retail AZERTY table, not memory past keynames_localized.
void TestFrenchDigitNames()
{
    g_language = 1;
    CHECK(std::strcmp(Key_KeynumToString('0', 1), "\xe0") == 0);
    CHECK(std::strcmp(Key_KeynumToString('1', 1), "&") == 0);
    CHECK(std::strcmp(Key_KeynumToString('9', 1), "\xe7") == 0);
    g_language = 0;
    CHECK(std::strcmp(Key_KeynumToString('9', 1), "9") == 0);
}

// A single high-bit character is not a negative key number.
void TestHighBitKeyName()
{
    CHECK(Key_StringToKeynum("a") == 'a');
    CHECK(Key_StringToKeynum("\xe9") >= -1);
    CHECK(Key_StringToKeynum("\xe9") < 256);
}

// Bindings stop at the buffer, quotes and all; nothing past it is touched.
void TestBindingsStayInBuffer()
{
    std::string binding(64, '"');
    for (int key = 'a'; key <= 'z'; ++key)
        playerKeys[0].keys[key].binding = binding.c_str();

    constexpr int kSize = 200;
    char buffer[kSize + 16];
    std::memset(buffer, 0x5a, sizeof(buffer));
    const int used = Key_WriteBindingsToBuffer(0, buffer, kSize);
    CHECK(used < kSize);
    CHECK(buffer[used] == 0);
    for (int i = kSize; i < static_cast<int>(sizeof(buffer)); ++i)
        CHECK(buffer[i] == 0x5a);

    for (int key = 'a'; key <= 'z'; ++key)
        playerKeys[0].keys[key].binding = nullptr;
}

// A full console ring copies at most maxSize - 1 bytes.
void TestConsoleCopyBounded()
{
    constexpr int kRing = 64;
    char ring[kRing];
    std::memset(ring, 'x', sizeof(ring));
    MessageLine line{};
    MessageWindow &window = con.consoleWindow;
    window.circularTextBuffer = ring;
    window.textBufSize = kRing;
    window.lines = &line;
    window.firstLineIndex = 0;
    window.activeLineCount = 1;

    constexpr int kMax = 32;
    char text[kMax + 16];
    for (int start = 0; start < kRing; ++start)
    {
        for (int pos = 0; pos < kRing; ++pos)
        {
            line.textBufPos = start;
            window.textBufPos = pos;
            std::memset(text, 0x5a, sizeof(text));
            Con_GetTextCopy(text, kMax);
            const int expected = (pos - start + kRing) % kRing;
            const int length = static_cast<int>(strnlen(text, sizeof(text)));
            CHECK(length == (expected < kMax ? expected : kMax - 1));
            for (int i = kMax; i < static_cast<int>(sizeof(text)); ++i)
                CHECK(text[i] == 0x5a);
        }
    }
}

// Long keyword lists are cut at the command buffer instead of overrunning it.
void TestGlobalServersBounded()
{
    g_masterName.current.string = "localhost";
    g_masterPort.current.integer = 20810;
    com_masterServerName = &g_masterName;
    com_masterPort = &g_masterPort;
    g_args = {"globalservers", "0", "7"};
    for (int i = 0; i < 64; ++i)
        g_args.push_back(std::string(60, 'k'));
    CL_GlobalServers_f();
    CHECK(g_sent.rfind("getservers 7 ", 0) == 0);
    CHECK(g_sent.size() < 1028);
}
} // namespace

int main()
{
    TestFrenchDigitNames();
    TestHighBitKeyName();
    TestBindingsStayInBuffer();
    TestConsoleCopyBounded();
    TestGlobalServersBounded();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("client input buffers: ok");
    return 0;
}
