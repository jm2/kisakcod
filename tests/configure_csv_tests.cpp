// configure_csv_tests.cpp: configure_mp.csv parsing and checksum in the
// production com_playerprofile.cpp, at 64-bit under the sanitizers. The
// dvar name and value tables are char[64][32]; the decompiled code indexed
// row n as (*table)[32 * n], past the first row's 32 bytes, and hashed the
// file in signed int arithmetic that overflows on any real file.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <qcommon/qcommon.h>
#include <universal/q_parse.h>
#include <universal/q_shared.h>

// The production signatures (com_playerprofile.cpp); a row is one bounded name or value.
using Row = char[32]; // Flawfinder: ignore (the production tables' row; writes go through I_strncpyz)
int __cdecl Com_GetConfigureDvarNames(const char **text, Row *dvarNames);
void __cdecl Com_GetConfigureDvarValues(int dvarCount, const char **text, Row *dvarValues);
void __cdecl Com_SetConfigureDvars(int dvarCount, const Row *dvarNames, const Row *dvarValues);
int __cdecl Com_ConfigureChecksum(const char *csv, int filesize);

// ---- Engine boundary: a line-aware tokenizer and a dvar recorder ----------
namespace
{
parseInfo_t g_token;
std::vector<std::string> g_setNames, g_setValues;
dvar_s g_dvar{};
} // namespace

// Com_ParseOnLine's contract for this file: the next comma- or
// space-separated token on the current line, empty at the line's end (which
// it then consumes); *data_p becomes null at the end of the text.
parseInfo_t *__cdecl Com_ParseOnLine(const char **data_p)
{
    g_token.token[0] = 0;
    const char *p = *data_p;
    while (*p == ' ' || *p == ',')
        ++p;
    if (!*p)
    {
        *data_p = nullptr;
        return &g_token;
    }
    if (*p == '\n')
    {
        *data_p = p + 1;
        return &g_token;
    }
    std::size_t n = 0;
    while (p[n] && p[n] != ',' && p[n] != ' ' && p[n] != '\n' && n + 1 < sizeof(g_token.token))
        ++n;
    std::memcpy(g_token.token, p, n); // Flawfinder: ignore (n < sizeof(g_token.token), checked above)
    g_token.token[n] = 0;
    *data_p = p + n;
    return &g_token;
}
void I_strncpyz(char *dest, const char *src, int destsize)
{
    std::snprintf(dest, static_cast<std::size_t>(destsize), "%s", src);
}
const dvar_s *__cdecl Dvar_SetFromStringByNameFromSource(const char *name, const char *value, DvarSetSource)
{
    g_setNames.emplace_back(name);
    g_setValues.emplace_back(value);
    return &g_dvar;
}
const dvar_s *__cdecl Dvar_FindVar(const char *) { return &g_dvar; }
void __cdecl Dvar_AddFlags(dvar_s *dvar, int flags) { dvar->flags |= static_cast<uint16_t>(flags); }
void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file ? file : "?", line, fmt ? fmt : "");
    std::exit(3);
}

namespace
{
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

// The hash as the x86 build computes it: 32-bit two's-complement wraparound.
int ReferenceChecksum(const char *csv, int size)
{
    uint32_t sum = 0;
    for (int i = 0; i < size; ++i)
        sum = static_cast<uint32_t>(static_cast<int>(csv[i])) + 31337u * sum;
    return static_cast<int>(sum & 0xFFFFFFFu) + 1;
}
} // namespace

int main()
{
    static char names[64][32]; // Flawfinder: ignore (the production tables' shape; writes go through I_strncpyz)
    static char values[64][32]; // Flawfinder: ignore (as above)
    const char *text = "r_one,r_two,r_three\n1,two,3\n";
    const int count = Com_GetConfigureDvarNames(&text, names);
    Check(count == 3, "three dvar names parse");
    Check(!std::strcmp(names[0], "r_one") && !std::strcmp(names[1], "r_two") && !std::strcmp(names[2], "r_three"),
          "each name lands in its own row");
    Com_GetConfigureDvarValues(count, &text, values);
    Check(!std::strcmp(values[1], "two") && !std::strcmp(values[2], "3"), "each value lands in its own row");
    Com_SetConfigureDvars(count, names, values);
    Check(g_setNames.size() == 3 && g_setNames[2] == "r_three" && g_setValues[2] == "3",
          "each row's dvar is set to its row's value");

    const std::string csv = "renderer,r_picmip\n" + std::string(4096, 'x') + "\xE9\n";
    const int size = static_cast<int>(csv.size());
    Check(Com_ConfigureChecksum(csv.data(), size) == ReferenceChecksum(csv.data(), size),
          "the checksum wraps as the 32-bit build's does");
    Check(Com_ConfigureChecksum("", 0) == 1, "an empty file hashes to 1");

    if (!g_failures)
        std::printf("configure csv: all checks passed\n");
    return g_failures ? 1 : 0;
}
