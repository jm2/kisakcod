// gsc_function_table_tests.cpp: the adapters the MP builtin table
// (g_scr_main_mp.cpp) uses so that every entry has the void (*)() type the
// VM calls it through (Scr_GetFunction). The table itself holds no casts,
// so the compiler checks each entry's type; these run the adapters as the
// VM does, under -fsanitize=function in the sanitizer build.

#include <cstdio>
#include <vector>

#include <game_mp/g_public_mp.h>

namespace
{
std::vector<int> g_added;
int g_calls = 0;
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

// A builtin with a result, like GScr_PrecacheTurret or GScr_SetTeamRadar.
int ResultBuiltin()
{
    ++g_calls;
    return 42;
}
bool BoolBuiltin()
{
    ++g_calls;
    return true;
}
} // namespace

void __cdecl Scr_AddInt(int value) { g_added.push_back(value); }

int main()
{
    const BuiltinFunctionDef table[] = {
        {"result", &GScr_IgnoreResult<&ResultBuiltin>, 0},
        {"flag", &GScr_IgnoreResult<&BoolBuiltin>, 0},
        {"zero", &GScr_ReturnZero, 0},
    };
    for (const BuiltinFunctionDef &entry : table)
        entry.actionFunc(); // as the VM calls a builtin
    Check(g_calls == 2, "each adapted builtin runs once");
    Check((g_added == std::vector<int>{0}), "the folded zero builtin returns 0 to the script");

    if (!g_failures)
        std::printf("gsc function table: all checks passed\n");
    return g_failures ? 1 : 0;
}
