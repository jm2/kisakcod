// wire_identity_tests.cpp: the wire identity a dedicated server advertises
// and accepts (NET_STEAM18.md §7, gate G4a). Runs the production SV_Init,
// SVC_Info, SVC_Status and SV_DirectConnect, and reads the replies back with
// the production Info_ValueForKey.
//
// "steam18" is the default profile: protocol 7, so a retail Steam 1.8 client
// sees and may connect to the server. The old code advertised and accepted
// protocol 1, so every steam18 check fails on it. "fork" (opt-in, set the way
// "+set net_wireProfile fork" sets it) keeps the fork's protocol 1.
//
// steam18 also takes a stock "getchallenge <n> <md5 cdkey>" (NET_STEAM18 §8,
// option A): the hash, unchecked, is the client's ban key. fork still needs
// its third, identity argument. The old code refused every stock request.
//
// The engine boundary is weak: the engine TUs replace the stubs they define,
// and --gc-sections drops the engine code no check reaches.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <game_mp/g_main_mp.h>
#include <game_mp/g_public_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/files.h>
#include <qcommon/net_chan_mp.h>
#include <qcommon/net_local.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_local.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>
#include <qcommon/wire_profile.h>
#include <script/scr_variable.h>
#include <server/sv_game.h>
#include <server_mp/server_mp.h>
#include <universal/q_parse.h>
#include <universal/com_files.h>
#include <universal/com_math.h>
#include <universal/com_memory.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// The last connectionless reply the server sent, and the command it parsed.
std::string g_reply;
const char *g_argv[4] = {};
int g_argc = 0;

void Command(const char *a0, const char *a1)
{
    g_argv[0] = a0;
    g_argv[1] = a1;
    g_argc = 2;
}

// The infostring after a reply's first line ("infoResponse\n...").
std::string Body(const std::string &reply)
{
    const size_t newline = reply.find('\n');
    if (newline == std::string::npos)
        return std::string();
    const size_t end = reply.find('\n', newline + 1);
    return reply.substr(newline + 1, end == std::string::npos ? std::string::npos : end - newline - 1);
}

std::string Value(const std::string &info, const char *key)
{
    return Info_ValueForKey(info.c_str(), key);
}

netadr_t Remote()
{
    netadr_t from{};
    from.type = NA_IP;
    from.ip[0] = 203;
    from.ip[1] = 0;
    from.ip[2] = 113;
    from.ip[3] = 9;
    from.port = 28960;
    return from;
}

// Runs a stock two-argument getchallenge through SV_GetChallenge.
std::string StockChallenge(const char *cdkeyHash)
{
    g_argv[0] = "getchallenge";
    g_argv[1] = "0";
    g_argv[2] = cdkeyHash;
    g_argc = 3;
    g_reply.clear();
    SV_GetChallenge(Remote());
    return g_reply;
}

// Whether a challenge slot for the test's client holds this ban key.
bool ChallengeHolds(const char *cdkeyHash)
{
    for (const challenge_t &challenge : svs.challenges)
    {
        if (NET_CompareAdr(challenge.adr, Remote()) && std::strcmp(challenge.cdkeyHash, cdkeyHash) == 0)
            return true;
    }
    return false;
}

// Runs a connect with the given protocol through SV_DirectConnect.
std::string Connect(int protocol)
{
    char userinfo[128];
    std::snprintf(userinfo, sizeof(userinfo), "\\protocol\\%i\\challenge\\1234\\qport\\5", protocol);
    Command("connect", userinfo);
    g_reply.clear();
    SV_DirectConnect(Remote());
    return g_reply;
}
} // namespace

// ---------------------------------------------------------------------------
// Engine boundary
// ---------------------------------------------------------------------------
WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::exit(3);
}
WEAK void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}

WEAK bool __cdecl NET_OutOfBandPrint(netsrc_t, netadr_t, const char *data)
{
    g_reply = data ? data : "";
    return true;
}
WEAK const char *__cdecl SV_Cmd_Argv(int arg) { return arg >= 0 && arg < g_argc && g_argv[arg] ? g_argv[arg] : ""; }
WEAK int __cdecl SV_Cmd_Argc() { return g_argc; }
WEAK void __cdecl SV_AddOperatorCommands() {}

// Addresses: the test's client is remote, never on the LAN, and only matches
// itself; nothing is connected, so no slot or challenge holds its address.
WEAK const char *NET_AdrToString(netadr_t) { return "203.0.113.9:28960"; }
WEAK bool __cdecl NET_CompareAdr(netadr_t a, netadr_t b) { return a.type == b.type && std::memcmp(a.ip, b.ip, sizeof(a.ip)) == 0 && a.port == b.port; }
WEAK bool __cdecl NET_CompareBaseAdr(netadr_t a, netadr_t b) { return a.type == b.type && std::memcmp(a.ip, b.ip, sizeof(a.ip)) == 0; }
WEAK bool __cdecl NET_IsLocalAddress(netadr_t) { return false; }
WEAK bool Sys_IsLANAddress(netadr_t) { return false; }

// Strings and memory keep the engine's semantics; the rest is inert.
WEAK const char *CopyString(const char *in) { return strdup(in ? in : ""); }
WEAK void __cdecl FreeString(const char *str)
{
    std::free(const_cast<char *>(str));
}
WEAK void Z_Free(void *ptr, int)
{
    std::free(ptr);
}
WEAK bool __cdecl Vec4Compare(const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; }
WEAK char info1[1024];
WEAK uint8_t tempServerMsgBuf[131072];
WEAK const dvar_t *com_dedicated;
WEAK const dvar_t *nextmap;
WEAK const dvar_s *fs_gameDirVar;
WEAK int gameInitialized;
WEAK int fs_numServerIwds;
WEAK void KISAK_CDECL Sys_LockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockRead(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_LockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_UnlockWrite(FastCriticalSection *) {}
WEAK void KISAK_CDECL Sys_Sleep(std::uint32_t) {}
WEAK bool __cdecl Sys_IsMainThread() { return true; }
// Value 1 is the thread's va()/Info_ValueForKey buffers.
WEAK void *__cdecl Sys_GetValue(int valueIndex)
{
    static va_info_t vaInfo;
    return valueIndex == 1 ? &vaInfo : nullptr;
}
WEAK void __cdecl SV_Cmd_TokenizeString(char *) {}
WEAK void __cdecl SV_Cmd_EndTokenizedString() {}
WEAK int __cdecl FS_iwIwd(char *, char *) { return 1; }
WEAK void __cdecl FS_FCloseFile(int) {}
WEAK bool __cdecl Com_LogFileOpen() { return false; }
WEAK void __cdecl Dvar_AddCommands() {}
WEAK void __cdecl SV_Heartbeat_f() {}
WEAK int32_t __cdecl G_GetClientScore(int32_t) { return 0; }
WEAK playerState_s *__cdecl SV_GameClientNum(int) { return nullptr; }
WEAK gentity_s *__cdecl SV_GentityNum(int) { return nullptr; }
WEAK void Scr_FreeValue(uint32_t) {}
WEAK uint32_t Scr_AllocArray() { return 0; }
WEAK void __cdecl Netchan_Setup(netsrc_t, netchan_t *, netadr_t, int, char *, int, char *, int) {}
WEAK char *__cdecl ClientConnect(uint32_t, uint16_t) { return nullptr; }
WEAK void __cdecl ClientDisconnect(int32_t) {}
// The permanent ban list is a file; there is none, so only temporary bans apply.
WEAK int __cdecl FS_ReadFile(const char *, void **) { return -1; }
WEAK void __cdecl FS_FreeFile(char *) {}
WEAK parseInfo_t *__cdecl Com_Parse(const char **) { static parseInfo_t empty{}; return &empty; }
WEAK void __cdecl Com_SkipRestOfLine(const char **) {}
WEAK int __cdecl Kisak_rand() { return 4; }

int main(int argc, char **argv)
{
    const bool fork = argc > 1 && std::strcmp(argv[1], "fork") == 0;

    Dvar_Init();
    if (fork)
        Dvar_SetFromStringByName("net_wireProfile", "fork"); // what "+set" does before init
    SV_Init(); // svs.clients is static and starts empty: nobody is connected

    const char *const protocol = fork ? "1" : "7";
    CHECK(Com_GetWireProfile() == (fork ? WIRE_PROFILE_FORK : WIRE_PROFILE_STEAM18));

    // getinfo: what a server browser reads.
    Command("getinfo", "xyz");
    g_reply.clear();
    SVC_Info(Remote());
    CHECK(g_reply.rfind("infoResponse\n", 0) == 0);
    CHECK(Value(Body(g_reply), "protocol") == protocol);
    CHECK(Value(Body(g_reply), "challenge") == "xyz");

    // getstatus: the serverinfo dvars, among them the protocol SV_Init registered.
    Command("getstatus", "abc");
    g_reply.clear();
    SVC_Status(Remote());
    CHECK(g_reply.rfind("statusResponse\n", 0) == 0);
    CHECK(Value(Body(g_reply), "protocol") == protocol);
    CHECK(Value(Body(g_reply), "challenge") == "abc");

    // connect: the profile's protocol passes the version check and reaches the
    // challenge lookup (no challenge was issued); any other is told to update.
    const std::string accepted = Connect(fork ? 1 : 7);
    CHECK(accepted == "error\nEXE_BAD_CHALLENGE");
    const std::string rejected = Connect(fork ? 7 : 1);
    CHECK(rejected == std::string("EXE_SERVER_IS_DIFFERENT_VER ") + (fork ? "1.0" : "1.8"));

    // getchallenge from a stock client: its CD-key hash only.
    const char *const hash = "0123456789abcdef0123456789abcdef";
    const std::string stock = StockChallenge(hash);
    if (fork)
    {
        CHECK(stock == "error\n\x15" "A client identity is required");
        CHECK(!ChallengeHolds(hash));
    }
    else
    {
        CHECK(stock.rfind("challengeResponse ", 0) == 0);
        CHECK(ChallengeHolds(hash));
        // A banned hash is refused like any banned identity.
        const char *const banned = "fedcba9876543210fedcba9876543210";
        std::memcpy(svs.tempBans[0].cdkeyHash, banned, sizeof(svs.tempBans[0].cdkeyHash));
        svs.tempBans[0].banTime = svs.time;
        CHECK(StockChallenge(banned) == "error\n\x15" "You are temporarily banned from this server");
        CHECK(!ChallengeHolds(banned));
    }

    // The identity the common and game code register as gamename and shortversion.
    CHECK(std::strcmp(Com_WireGameName(), fork ? "KisakCoD4" : "Call of Duty 4") == 0);
    CHECK(std::strcmp(Com_WireShortVersion(), fork ? "1.0" : "1.8") == 0);

    if (g_failures == 0)
        std::printf("wire identity (%s): all checks passed\n", fork ? "fork" : "steam18");
    return g_failures == 0 ? 0 : 1;
}
