// q_parse_csv_tests.cpp: Com_ParseCSV (universal/q_parse.cpp) on quoted
// fields. An unterminated quote must end at the text's NUL; the old scan ran
// past it to the next '"' byte in memory.
//
// The engine boundary is weak: --gc-sections drops what the checks never
// reach. Com_Error and asserts fail the test.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <qcommon/qcommon.h>
#include <universal/q_parse.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// Parses one CSV token from text and returns where the cursor ended up.
const char *ParseOne(const char *text, char *token, size_t tokenSize)
{
    Com_BeginParseSession("q_parse_csv_tests");
    Com_SetCSV(1);
    const char *cursor = text;
    const parseInfo_t *pi = Com_Parse(&cursor);
    std::snprintf(token, tokenSize, "%s", pi->token);
    Com_EndParseSession();
    return cursor;
}
} // namespace

// Engine boundary.
WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", file ? file : "?", line);
    std::exit(3);
}
// The parser's per-thread state: this test is the main thread.
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsDatabaseThread() { return false; }
WEAK bool Sys_IsRenderThread() { return false; }

int main()
{
    char token[64];

    // A terminated quoted field, with a doubled quote inside, then the next field.
    const char quoted[] = "\"a\"\"b\",next";
    const char *end = ParseOne(quoted, token, sizeof(token));
    CHECK(!std::strcmp(token, "a\"b"));
    CHECK(end == quoted + 7);

    // An unterminated quote: the text ends after "abc", and a '"' follows in
    // the same buffer past the NUL, where the old scan found it.
    const char unterminated[] = {'"', 'a', 'b', 'c', '\0', 'x', '"', ',', 't', '\0'};
    end = ParseOne(unterminated, token, sizeof(token));
    CHECK(!std::strcmp(token, "abc"));
    CHECK(end == unterminated + 4);

    if (g_failures == 0)
        std::printf("q_parse csv: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
