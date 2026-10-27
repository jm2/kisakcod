// game_mp_script_cleanup_tests.cpp: the game_mp cleanup sites from #217
// items 1 and 3. Each check drives the production implementation, not a copy:
//   * G_SpawnTurret's bad-weapon Com_Error text is the real apostrophe form,
//   * PlayerCmd_ClearPerks clears both perk masks after the dead 32-bit
//     pointer-magic checks are gone,
//   * Scr_RandomFloatRange prints its float arguments as floats, and
//   * GScr_IsPlayerNumber rejects negative values with an unsigned bound,
//   * setDvar/makeDvarServerInfo pass the value they sanitized (item 1).

#include <bgame/bg_local.h>
#include <game/game_public.h>
#include <game_mp/g_main_mp.h>
#include <game_mp/g_public_mp.h>
#include <game_mp/g_utils_mp.h>
#include <script/scr_variable.h>
#include <script/scr_vm.h>
#include <server/sv_world.h>

#include <csetjmp>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

gentity_s g_entities[MAX_GENTITIES];
level_locals_t level;
const dvar_t *loc_warnings;
const dvar_t *loc_warningsAsErrors;

namespace
{
int g_failures = 0;
gclient_s g_client;
int g_intArg = 0;
float g_floatArg[2] = {};
int g_numParams = 0;
const char *g_stringArg = "";
int g_stringType = 0;
char g_dvarSetValue[256] = "";
char g_dvarRegisteredValue[256] = "";
int g_lastInt = -1;
float g_lastFloat = 0.0f;
int g_scrErrors = 0;
uint32_t g_weaponIndex = 0;
std::string g_printfText;
std::string g_errorText;
std::jmp_buf g_errorJump;
bool g_captureError = false;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

void Format(char *dest, size_t size, const char *fmt, va_list args)
{
    std::vsnprintf(dest, size, fmt, args);
}
}  // namespace

void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file ? file : "?", line, fmt ? fmt : "");
    std::exit(3);
}

uint8_t __cdecl I_CleanChar(uint8_t character)
{
    return character == 146 ? 39 : character;
}

char *__cdecl va(const char *format, ...)
{
    static char text[512];
    va_list args;
    va_start(args, format);
    Format(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

void QDECL Com_Printf(int, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    Format(text, sizeof(text), fmt, args);
    va_end(args);
    g_printfText = text;
}

void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    Format(text, sizeof(text), fmt, args);
    va_end(args);
    g_errorText = text;
    if (!g_captureError)
    {
        std::fprintf(stderr, "unexpected Com_Error: %s\n", text);
        std::exit(2);
    }
    std::longjmp(g_errorJump, 1);
}

void Scr_Error(const char *error)
{
    ++g_scrErrors;
    (void)error;
}

void __cdecl Scr_ObjectError(const char *error)
{
    ++g_scrErrors;
    (void)error;
}

int __cdecl Scr_GetInt(uint32_t)
{
    return g_intArg;
}

const char *__cdecl Scr_GetString(uint32_t index)
{
    return index == 0 ? "b217_dvar" : g_stringArg;
}

int __cdecl Scr_GetType(uint32_t index)
{
    return index == 1 ? g_stringType : 0;
}

uint32_t __cdecl Scr_GetNumParam()
{
    return g_numParams;
}

const char *__cdecl Scr_GetIString(uint32_t)
{
    return "";
}

uint32_t __cdecl Scr_GetPointerType(uint32_t)
{
    return 0;
}

gentity_s *__cdecl Scr_GetEntity(uint32_t)
{
    return nullptr;
}

void __cdecl Scr_ParamError(uint32_t, const char *) {}

void QDECL Com_PrintWarning(int, const char *, ...) {}

char __cdecl Dvar_IsValidName(const char *)
{
    return 1;
}

const dvar_s *__cdecl Dvar_FindVar(const char *)
{
    return nullptr;
}

void __cdecl Dvar_AddFlags(dvar_s *, int) {}

const dvar_s *__cdecl Dvar_RegisterString(const char *, const char *value, uint16_t, const char *)
{
    std::snprintf(g_dvarRegisteredValue, sizeof(g_dvarRegisteredValue), "%s", value);
    return nullptr;
}

void __cdecl Dvar_SetFromStringByName(const char *, const char *value)
{
    std::snprintf(g_dvarSetValue, sizeof(g_dvarSetValue), "%s", value);
}

float __cdecl Scr_GetFloat(uint32_t index)
{
    return g_floatArg[index < 2 ? index : 0];
}

void __cdecl Scr_AddInt(int value)
{
    g_lastInt = value;
}

void __cdecl Scr_AddFloat(float value)
{
    g_lastFloat = value;
}

uint32_t __cdecl G_GetWeaponIndexForName(const char *)
{
    return g_weaponIndex;
}

WeaponDef *__cdecl BG_GetWeaponDef(uint32_t)
{
    return nullptr;
}

int32_t __cdecl IsItemRegistered(uint32_t)
{
    return 1;
}

int __cdecl G_SoundAliasIndex(char *)
{
    return 0;
}

int32_t __cdecl G_SpawnFloat(const char *, const char *, float *)
{
    return 0;
}

int32_t __cdecl G_SpawnInt(const char *, const char *, int32_t *)
{
    return 0;
}

void __cdecl G_DObjUpdate(gentity_s *) {}
void __cdecl G_SetOrigin(gentity_s *, const float *) {}
void __cdecl G_SetAngle(gentity_s *, const float *) {}
void __cdecl SV_LinkEntity(gentity_s *) {}

namespace
{
void ClearPerksClearsBothMasks()
{
    g_entities[3] = gentity_s{};
    g_client.ps.perks = 0x3;
    g_client.sess.cs.perks = 0x5;
    g_entities[3].client = &g_client;

    scr_entref_t entref;
    entref.entnum = 3;
    entref.classnum = 0;
    PlayerCmd_ClearPerks(entref);
    Check(g_client.ps.perks == 0, "clearPerks clears the player-state perks");
    Check(g_client.sess.cs.perks == 0, "clearPerks clears the session perks");
}

void TurretErrorUsesARealQuote()
{
    gentity_s self{};
    g_errorText.clear();
    g_captureError = true;
    if (setjmp(g_errorJump) == 0)
        G_SpawnTurret(&self, "broken_turret");
    g_captureError = false;
    Check(g_errorText == "bad weaponinfo 'broken_turret' specified for turret",
          "the turret error uses apostrophes, not IDA's ',27h,'");
}

void RandomFloatRangePrintsFloats()
{
    g_floatArg[0] = 3.5f;
    g_floatArg[1] = 1.25f;
    g_printfText.clear();
    g_scrErrors = 0;
    Scr_RandomFloatRange();
    Check(g_scrErrors == 1, "the reversed float range raises a script error");
    Check(g_printfText == "Scr_RandomFloatRange parms: 3.5 1.25 ",
          "the range diagnostic prints both float values");
}

void IsPlayerNumberRejectsNegativeValues()
{
    g_intArg = -1;
    g_lastInt = -1;
    GScr_IsPlayerNumber();
    Check(g_lastInt == 0, "isPlayerNumber(-1) is false");

    g_intArg = 63;
    g_lastInt = -1;
    GScr_IsPlayerNumber();
    Check(g_lastInt == 1, "isPlayerNumber(63) is true");

    g_intArg = 64;
    g_lastInt = -1;
    GScr_IsPlayerNumber();
    Check(g_lastInt == 0, "isPlayerNumber(64) is false");
}
// Both dvars build outString with I_CleanChar, turn a double quote into an
// apostrophe, cap at 1023 bytes and NUL-terminate; that is the value the
// engine must receive, not the raw script string.
void SetDvarPassesTheSanitizedValue()
{
    g_numParams = 2;
    g_stringArg = "a\"b\x92" "c";
    g_dvarSetValue[0] = 0;
    GScr_SetDvar();
    Check(std::strcmp(g_dvarSetValue, "a'b'c") == 0,
          "setDvar stores the cleaned value");
}

void MakeDvarServerInfoPassesTheSanitizedValue()
{
    g_numParams = 2;
    g_stringArg = "a\"b\x92" "c";
    g_dvarRegisteredValue[0] = 0;
    GScr_MakeDvarServerInfo();
    Check(std::strcmp(g_dvarRegisteredValue, "a'b'c") == 0,
          "makeDvarServerInfo registers the cleaned value");
}
}  // namespace

int main()
{
    static dvar_t falseDvar;
    loc_warnings = loc_warningsAsErrors = &falseDvar;
    ClearPerksClearsBothMasks();
    TurretErrorUsesARealQuote();
    RandomFloatRangePrintsFloats();
    IsPlayerNumberRejectsNegativeValues();
    SetDvarPassesTheSanitizedValue();
    MakeDvarServerInfoPassesTheSanitizedValue();
    std::printf("game_mp_script_cleanup_tests: %d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
