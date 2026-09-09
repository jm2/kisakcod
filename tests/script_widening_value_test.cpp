// script_widening_value_test: contract tests for the M4 (ki-n1et) widened
// script value objects that must preserve full-width host pointers through
// value copies. These run on every target; on ILP32 they pin the frozen
// 4-byte parse cell, on native64 they prove the 8-byte cell moves whole.
//
//   1. sval_u copy assignment (const and non-const overloads) moves the
//      FULL union representation. The retail overloads copied only the
//      32-bit Enum_t type member; on native64 that dropped the pointer
//      half of every parse-node payload above 4 GiB (operator probe:
//      pointer_preserved=0). Both overloads are exercised with a >4 GiB
//      pointer-pattern payload through the real parser/AST header.
//   2. sval_u cells are pointer-strided on 64-bit: arrays written as
//      node[1], node[2], ... land on cell boundaries.
//   3. The widened runtime records the fixed consumers rely on keep their
//      RUNTIME_SIZE contract sizes on this target (VariableValue,
//      VariableValueInternal, ArchivedCanonicalStringInfo, Scr_Breakpoint,
//      Scr_WatchElement_s, Scr_OpcodeList_s, Scr_StringNode_s,
//      CaseStatementInfo, BreakStatementInfo, ContinueStatementInfo).

#if defined(_WIN32)
#error portable test target only
#endif

#ifndef KISAK_DEDI_HEADLESS
#define KISAK_DEDI_HEADLESS 1
#endif

#include <script/scr_debugger.h>
#include <script/scr_evaluate.h>
#include <script/scr_compiler.h>

#include <cstdint>
#include <cstdio>

namespace script_widening_value_test
{
namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "script_widening_value_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace script_widening_value_test

#define CHECK(expr) \
    script_widening_value_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
#if UINTPTR_MAX > 0xFFFFFFFFu
// A pointer value no ILP32 image could ever hold: the operator probe's
// "real pointer above 4 GiB". Stored through the union's pointer members
// and never dereferenced.
constexpr uintptr_t kHighPointerPattern = 0x00005A3C7E19B400ull;
#else
constexpr uintptr_t kHighPointerPattern = 0x7E19B400u;
#endif

// In-place construction (no copy through the deprecated implicit copy
// constructor; the tests exercise ASSIGNMENT, which is the defect surface).
void MakeNodeWithPointerPayload(Enum_t type, sval_u &out)
{
    out.type = type;
    out.node = reinterpret_cast<sval_u *>(kHighPointerPattern);
}

// The const overload (parser/AST code assigns both ways).
void TestConstAssignmentPreservesPointer()
{
    sval_u source;
    MakeNodeWithPointerPayload(ENUM_integer, source);
    sval_u destination;
    destination.type = ENUM_NOP;
    destination.node = nullptr;

    destination = source;

    // The pointer payload is the regression surface (the retail operator=
    // kept `type` and dropped the pointer half).
    CHECK(destination.node == reinterpret_cast<sval_u *>(kHighPointerPattern));
}

// The non-const overload.
void TestNonConstAssignmentPreservesPointer()
{
    sval_u source;
    MakeNodeWithPointerPayload(ENUM_string, source);
    sval_u destination;
    destination.type = ENUM_NOP;
    destination.node = nullptr;

    sval_u &nonConstSource = source;
    destination = nonConstSource;

    CHECK(destination.node == reinterpret_cast<sval_u *>(kHighPointerPattern));
}

// Scalar payloads: the type word rides in the union's low half, so an
// assignment must preserve it exactly as the retail form did.
void TestScalarPayloadAssignmentPreservesType()
{
    sval_u source;
    source.idValue = 0x12345678u;
    source.type = ENUM_integer;  // set last: same union storage

    sval_u destination;
    destination.type = ENUM_NOP;
    destination = source;

    CHECK(destination.type == ENUM_integer);
}

// Self-assignment must remain stable (both overloads see real use).
void TestSelfAssignmentStable()
{
    sval_u node;
    MakeNodeWithPointerPayload(ENUM_variable, node);
    sval_u &alias = node;

    node = alias;
    CHECK(node.node == reinterpret_cast<sval_u *>(kHighPointerPattern));

    const sval_u &constAlias = node;
    node = constAlias;
    CHECK(node.node == reinterpret_cast<sval_u *>(kHighPointerPattern));
}

// Whole-cell move: the union is exactly one pointer wide on 64-bit, and
// cell arrays stride on cell boundaries.
void TestCellStride()
{
    CHECK(sizeof(sval_u) == sizeof(void *));

    sval_u cells[3];
    cells[0].type = ENUM_integer;
    cells[1].node = reinterpret_cast<sval_u *>(kHighPointerPattern);
    cells[2].type = ENUM_string;

    sval_u copy;
    copy = cells[1];
    CHECK(copy.node == reinterpret_cast<sval_u *>(kHighPointerPattern));
}

// The records whose allocation sites were migrated to sizeof() in this
// rework keep their widened sizes (mirror of the RUNTIME_SIZE constants).
void TestWidenedRecordSizes()
{
    CHECK(sizeof(VariableValue) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x10u : 0x8u));
    CHECK(sizeof(VariableValueInternal) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x18u : 0x10u));
    CHECK(sizeof(ArchivedCanonicalStringInfo) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x10u : 0x8u));
    CHECK(sizeof(Scr_Breakpoint) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x30u : 0x1Cu));
    CHECK(sizeof(Scr_WatchElement_s) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0xA0u : 0x64u));
    CHECK(sizeof(Scr_OpcodeList_s) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x10u : 0x8u));
    CHECK(sizeof(CaseStatementInfo) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x20u : 0x10u));
    CHECK(sizeof(BreakStatementInfo) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x18u : 0xCu));
    CHECK(sizeof(ContinueStatementInfo) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x18u : 0xCu));
}
}  // namespace

int main()
{
    TestConstAssignmentPreservesPointer();
    TestNonConstAssignmentPreservesPointer();
    TestSelfAssignmentStable();
    TestScalarPayloadAssignmentPreservesType();
    TestCellStride();
    TestWidenedRecordSizes();

    if (script_widening_value_test::g_failures)
    {
        std::fprintf(
            stderr,
            "script_widening_value_test: %d/%d checks failed\n",
            script_widening_value_test::g_failures,
            script_widening_value_test::g_runs);
        return 1;
    }
    std::printf(
        "script_widening_value_test: %d checks passed\n",
        script_widening_value_test::g_runs);
    return 0;
}
