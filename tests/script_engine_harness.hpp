// script_engine_harness.hpp: GSC source through the production script
// system (every src/script TU the servers build: scanner, parser, compiler,
// VM, variables, strings), for the parser and VM regressions of NOW row 20.
// script_engine_harness.cpp is the engine boundary those TUs call into.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace gsc
{
// Serves <name>.gsc to the fast-file script loader.
void SetSource(const std::string &name, const std::string &text);

// One G_InitGame-style load of <name>.gsc, which must define main().
// Returns false, with the engine's Com_Error text, when the load drops; the
// script system is then unusable for the rest of the process, as it is in
// the engine until the drop is handled.
bool Load(const std::string &name, std::string *error);
// The entity field keys Load adds before compiling, as G_InitGame's
// Scr_AddFields("radiant", "txt") does: "type name" pairs. Empty adds none.
void SetLoadFields(const std::string &keys);

// Runs main() of the loaded script as a level thread and returns the values
// it passed to the test builtin report(value).
std::vector<int> RunMain();
// main(args...) as the engine starts a callback thread: each argument pushed,
// the last first, then Scr_ExecThread with their count.
std::vector<int> RunMain(const std::vector<int> &args);

// G_ShutdownGame: frees the scripts loaded by Load.
void Unload();

// The debugger's watch-expression compile (evaluate mode). Returns false if
// the expression did not parse or compile.
bool CompileWatchExpression(const char *text);
}  // namespace gsc

// Counts a failed check into the including test's gsc_failures.
#define GSC_CHECK(expr)                                                        \
    do                                                                         \
    {                                                                          \
        if (!(expr))                                                           \
        {                                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #expr); \
            ++gsc_failures;                                                    \
        }                                                                      \
    } while (0)
