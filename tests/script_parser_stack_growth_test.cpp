// script_parser_stack_growth_test.cpp: GSC parser defects (NOW row 20,
// #225). Real GSC source goes through the production scanner, parser,
// compiler and VM (script_engine_harness.hpp); run as
//   kisakcod-script-parser-stack-growth-tests <case>
//
//   stack-growth    an else-if chain deep enough to grow the parser stacks,
//                   several times, compiles and every branch runs; nesting
//                   past the 10000-state limit is a compile error.
//   long-lexeme     lexemes that fill the scanner buffer: no write past it,
//                   and an overlong one is a compile error, not exit(2).
//   error-recovery  malformed watch expressions (evaluate mode) recover
//                   inside the parser tables and the live state stack.

#include <cstdio>
#include <cstring>
#include <string>

#include "script_engine_harness.hpp"

namespace
{
int gsc_failures = 0;

// f(i) returns 10 * i + 1 through branch i of an n-branch else-if chain.
std::string ElseIfChain(int n)
{
    std::string text = "f(x)\n{\n\tif (x == 0)\n\t\treturn 1;\n";
    for (int i = 1; i < n; ++i)
        text += "\telse if (x == " + std::to_string(i) + ")\n\t\treturn " + std::to_string(10 * i + 1) + ";\n";
    text += "\treturn -1;\n}\n\nmain()\n{\n\tfor (i = 0; i <= " + std::to_string(n) +
            "; i++)\n\t\treport(f(i));\n}\n";
    return text;
}

void StackGrowth()
{
    // About 6 states per branch stay on the stack until the chain ends; the
    // first growth is at about 215 states, so 50 branches grow once and 400
    // grow four times.
    for (int n : {50, 400})
    {
        const std::string name = "chain" + std::to_string(n);
        gsc::SetSource(name, ElseIfChain(n));
        std::string error;
        GSC_CHECK(gsc::Load(name, &error));
        const std::vector<int> values = gsc::RunMain();
        GSC_CHECK(values.size() == static_cast<size_t>(n) + 1);
        for (int i = 0; i < n && i < static_cast<int>(values.size()); ++i)
            GSC_CHECK(values[i] == 10 * i + 1);
        GSC_CHECK(values.size() == static_cast<size_t>(n) + 1 && values[n] == -1);
        gsc::Unload();
    }

    // Each '(' holds a state until the innermost operand reduces.
    gsc::SetSource("nested", "main()\n{\n\tx = " + std::string(12000, '(') + "1" + std::string(12000, ')') + ";\n}\n");
    std::string error;
    GSC_CHECK(!gsc::Load("nested", &error));
    GSC_CHECK(error.find("bad syntax") != std::string::npos);
}

void LongLexeme()
{
    // The scanner refills at most 8192 bytes at a time into a 16384-byte
    // buffer, so a comment that starts a line fills it up at about 16380
    // characters. Every length across that edge either compiles or fails the
    // compile; none writes past the buffer or exits the process.
    int longestCompiled = 0;
    for (int length = 16360; length <= 16400; ++length)
    {
        const std::string name = "comment" + std::to_string(length);
        gsc::SetSource(name, "//" + std::string(length - 2, 'c') + "\nmain()\n{\n\treport(7);\n}\n");
        std::string error;
        if (!gsc::Load(name, &error))
        {
            GSC_CHECK(error.find("scanner input buffer overflow") != std::string::npos);
            GSC_CHECK(longestCompiled >= 16380);
            return;
        }
        GSC_CHECK(gsc::RunMain() == std::vector<int>{7});
        gsc::Unload();
        longestCompiled = length;
    }
    GSC_CHECK(!"a 16400-character lexeme compiled");
}

void ErrorRecovery()
{
    // Each is a syntax error that evaluate mode recovers from. yypact goes
    // down to -50, so the error-token lookup must reject negative indices,
    // and the pop loop must stop at the bottom live state.
    gsc::SetSource("empty", "main()\n{\n}\n");
    std::string error;
    GSC_CHECK(gsc::Load("empty", &error));
    for (const char *text : {")", ";", "]", "}", "+", ",", "1 +", "a[", "(1"})
        GSC_CHECK(!gsc::CompileWatchExpression(text));
    GSC_CHECK(gsc::CompileWatchExpression("1 + 2"));
    gsc::Unload();
}
}  // namespace

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "";
    if (!std::strcmp(which, "stack-growth"))
        StackGrowth();
    else if (!std::strcmp(which, "long-lexeme"))
        LongLexeme();
    else if (!std::strcmp(which, "error-recovery"))
        ErrorRecovery();
    else
    {
        std::fprintf(stderr, "usage: %s stack-growth|long-lexeme|error-recovery\n", argv[0]);
        return 2;
    }
    std::printf("%s: %d failure(s)\n", which, gsc_failures);
    return gsc_failures ? 1 : 0;
}
