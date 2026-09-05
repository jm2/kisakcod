// script_native64_layout_test: contract tests for the M4 (ki-n1et)
// script VM header widening. Every src/script header migrated off raw
// `static_assert(sizeof(T) == N)` onto RUNTIME_SIZE(T, n32, n64) now
// asserts the ILP32 retail sizes on 32-bit targets and the natural
// widened sizes on 64-bit targets. This test pins both halves of that
// contract:
//
//   1. Headers whose include chain is portable (scr_variable.h,
//      scr_parser.h, scr_memorytree.h) are included for real, so the
//      migrated RUNTIME_SIZE asserts inside them fire on this target and
//      the widened sizes are re-checked at runtime against the exact
//      constants the headers declare.
//
//   2. Headers whose include chain is still Windows/production-bound
//      (scr_debugger.h -> ui_shared.h, scr_vm.h / scr_animtree.h /
//      scr_compiler.h -> bg_local.h) are mirrored member-for-member in
//      this TU. Each mirror asserts the same RUNTIME_SIZE(n32, n64)
//      constants as its engine counterpart, so the constants are proven
//      to be the compiler's natural layout on BOTH widths, not guesses.
//      tests/script_native_layout_source_test.cmake pins the engine
//      headers to those same constants, so mirror and header cannot
//      drift apart silently.
//
//   3. The widened structures hold live host pointers (value cells,
//      stacks, parse nodes, code positions); the frozen ones carry only
//      scalar ids/handles. Widening must not -- and here does not --
//      change any serialized format: the save path (scr_readwrite.cpp)
//      writes type bytes and scalar payloads and rebuilds host pointers
//      at load, and the frozen save-image mirrors live in scr_native.hpp.

#include <universal/kisak_abi.h>

#include <script/scr_variable.h>
#include <script/scr_parser.h>
#include <script/scr_memorytree.h>

#include <cstdint>
#include <cstdio>

// Forward declarations for pointer-only members of mirrored structs; the
// mirrors never dereference these, they only need the pointer width.
struct scr_block_s;
struct PrecacheEntry;
struct XAnim_s;

namespace script_native64_layout_test
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
        std::fprintf(stderr, "script_native64_layout_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace script_native64_layout_test

#define CHECK(expr) \
    script_native64_layout_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// Width selector mirroring the RUNTIME_SIZE macro semantics: the runtime
// checks below spell both constants (n32, n64) and take the one this
// target's pointer width selects, exactly like the compile-time asserts
// in the headers and mirrors above.
constexpr std::size_t Width(std::size_t n32, std::size_t n64)
{
    return KISAK_ARCH_64BIT ? n64 : n32;
}

// ---------------------------------------------------------------------------
// Portable-header runtime re-checks. The compile-time asserts already ran
// while including the real headers; these pin the exact declared constants
// so a constant edit that silently breaks one width surfaces here.
// ---------------------------------------------------------------------------
void CheckPortableHeaderWidening()
{
    // scr_variable.h (M4 ki-n1et): value cell + variable table widen.
    CHECK(sizeof(VariableStackBuffer) == Width(0xC, 0x10));
    CHECK(sizeof(VariableUnion) == Width(0x4, 0x8));
    CHECK(sizeof(VariableValue) == Width(0x8, 0x10));
    CHECK(sizeof(VariableValueInternal_u) == Width(0x4, 0x8));
    CHECK(sizeof(VariableValueInternal) == Width(0x10, 0x18));
    CHECK(sizeof(scrVarDebugPub_t) == Width(0xE0004, 0x140008));
    CHECK(sizeof(scrVarGlob_t) == Width(0x180000, 0x240000));
    CHECK(sizeof(scr_classStruct_t) == Width(0xC, 0x10));
    CHECK(sizeof(VariableDebugInfo) == Width(0x10, 0x20));
    CHECK(sizeof(ThreadDebugInfo) == Width(0x8C, 0x110));

    // Frozen scr_variable.h scalars stay put on both widths.
    CHECK(sizeof(ObjectInfo_u) == Width(0x2, 0x2));
    CHECK(sizeof(ObjectInfo) == Width(0x4, 0x4));
    CHECK(sizeof(Variable_u) == Width(0x2, 0x2));
    CHECK(sizeof(Variable) == Width(0x4, 0x4));
    CHECK(sizeof(VariableValueInternal_w) == Width(0x4, 0x4));
    CHECK(sizeof(VariableValueInternal_v) == Width(0x2, 0x2));
    CHECK(sizeof(scr_entref_t) == Width(0x4, 0x4));

    // scr_parser.h (M4 ki-n1et): pointer-bearing parser globals widen.
    CHECK(sizeof(OpcodeLookup) == Width(0x18, 0x20));
    CHECK(sizeof(SourceBufferInfo) == Width(44, 56));
    CHECK(sizeof(SaveSourceBufferInfo) == Width(0x8, 0x10));
    CHECK(sizeof(scrParserGlob_t) == Width(0x34, 0x50));
    CHECK(sizeof(scrParserPub_t) == Width(0x10, 0x20));

    // Frozen scr_parser.h scalars stay put.
    CHECK(sizeof(Scr_SourcePos_t) == Width(0xC, 0xC));
    CHECK(sizeof(SourceLookup) == Width(8, 8));

    // scr_memorytree.h (M4 ki-n1et): 16-bit-handle tree, frozen on both.
    CHECK(sizeof(MemoryNode) == Width(12, 12));
    CHECK(sizeof(scrMemTreeGlob_t) == Width(0xC0380, 0xC0380));
}

// ---------------------------------------------------------------------------
// Mirrors for headers whose include chain cannot compile in a portable TU
// yet. Member lists are transcribed verbatim from the engine headers; the
// RUNTIME_SIZE constants are the exact ones the headers declare.
// ---------------------------------------------------------------------------

// scr_debugger.h (M4 ki-n1et)
union MirroredSvalU
{
    int32_t type;
    uint32_t stringValue;
    uint32_t idValue;
    float floatValue;
    int intValue;
    MirroredSvalU *node;
    uint32_t sourcePosValue;
    const char *codePosValue;
    const char *debugString;
    scr_block_s *block;
};
RUNTIME_SIZE(MirroredSvalU, 0x4, 0x8);

struct MirroredDebuggerSvalS
{
    MirroredDebuggerSvalS *next;
};
RUNTIME_SIZE(MirroredDebuggerSvalS, 0x4, 0x8);

struct MirroredScriptExpressionT
{
    MirroredSvalU parseData;
    int breakonExpr;
    MirroredDebuggerSvalS *exprHead;
};
RUNTIME_SIZE(MirroredScriptExpressionT, 0xC, 0x18);

struct MirroredScrBreakpoint
{
    int line;
    uint32_t bufferIndex;
    char *codePos;
    void *element;  // Scr_WatchElement_s *
    int builtinIndex;
    MirroredScrBreakpoint *next;
    MirroredScrBreakpoint **prev;
};
RUNTIME_SIZE(MirroredScrBreakpoint, 0x1C, 0x30);

struct MirroredScrWatchElementS
{
    MirroredScriptExpressionT expr;
    const char *valueText;
    const char *refText;
    bool directObject;
    // padding
    uint32_t objectId;
    uint8_t objectType;
    uint8_t oldObjectType;
    bool expand;
    uint8_t breakpointType;
    bool hitBreakpoint;
    bool changed;
    bool valueDefined;
    bool threadList;
    bool endonList;
    // padding
    VariableValue value;
    uint32_t fieldName;
    uint32_t childCount;
    uint32_t hardcodedCount;
    int id;
    MirroredScrBreakpoint *breakpoint;
    const char *deadCodePos;
    uint32_t bufferIndex;
    uint32_t sourcePos;
    int changedTime;
    MirroredScrWatchElementS *parent;
    MirroredScrWatchElementS *childArrayHead;
    MirroredScrWatchElementS *childHead;
    MirroredScrWatchElementS *next;
};
RUNTIME_SIZE(MirroredScrWatchElementS, 0x64, 0xA0);

struct MirroredScrOpcodeListS
{
    char *codePos;
    MirroredScrOpcodeListS *next;
};
RUNTIME_SIZE(MirroredScrOpcodeListS, 0x8, 0x10);

struct MirroredScrWatchElementNodeS
{
    void *element;  // Scr_WatchElement_s *
    MirroredScrWatchElementNodeS *next;
};
RUNTIME_SIZE(MirroredScrWatchElementNodeS, 0x8, 0x10);

struct MirroredScrWatchElementDoubleNodeT
{
    MirroredScrWatchElementNodeS *list;
    MirroredScrWatchElementNodeS *removedList;
};
RUNTIME_SIZE(MirroredScrWatchElementDoubleNodeT, 0x8, 0x10);

// scr_evaluate.h (M4 ki-n1et)
struct MirroredArchivedCanonicalStringInfo
{
    uint16_t canonicalStr;
    // padding
    const char *value;
};
RUNTIME_SIZE(MirroredArchivedCanonicalStringInfo, 0x8, 0x10);

struct MirroredScrEvaluateGlobT
{
    char *archivedCanonicalStringsBuf;
    MirroredArchivedCanonicalStringInfo *archivedCanonicalStrings;
    int *canonicalStringLookup;
    bool freezeScope;
    bool freezeObjects;
    bool objectChanged;
    // padding
};
RUNTIME_SIZE(MirroredScrEvaluateGlobT, 0x10, 0x20);

// scr_vm.h (M4 ki-n1et)
struct MirroredScrStringNodeS
{
    const char *text;
    MirroredScrStringNodeS *next;
};
RUNTIME_SIZE(MirroredScrStringNodeS, 0x8, 0x10);

struct MirroredFunctionStackT
{
    const char *pos;
    uint32_t localId;
    uint32_t localVarCount;
    VariableValue *top;
    VariableValue *startTop;
};
RUNTIME_SIZE(MirroredFunctionStackT, 0x14, 0x20);

struct MirroredFunctionFrameT
{
    MirroredFunctionStackT fs;
    Vartype_t topType;
};
RUNTIME_SIZE(MirroredFunctionFrameT, 0x18, 0x28);

struct MirroredScrVmPubT
{
    uint32_t *localVars;
    VariableValue *maxstack;
    int function_count;
    MirroredFunctionFrameT *function_frame;
    VariableValue *top;
    bool debugCode;
    bool abort_on_error;
    bool terminal_error;
    // padding
    uint32_t inparamcount;
    uint32_t outparamcount;
    uint32_t breakpointOutparamcount;
    bool showError;
    // padding
    MirroredFunctionFrameT function_frame_start[32];
    VariableValue stack[2048];
};
RUNTIME_SIZE(MirroredScrVmPubT, 0x4328, 0x8540);

struct MirroredFuncDebugData
{
    int breakpointCount;
    const char *name;
    int prof;
    int usage;
};
RUNTIME_SIZE(MirroredFuncDebugData, 0x10, 0x18);

struct MirroredScrVmDebugPubT
{
    MirroredFuncDebugData func_table[1024];
    int checkBreakon;
    int profileEnable[32768];
    int builtInTime;
    const char *jumpbackHistory[128];
    int jumpbackHistoryIndex;
    int dummy;
};
RUNTIME_SIZE(MirroredScrVmDebugPubT, 0x24210, 0x26410);

struct MirroredScrVmGlobT
{
    VariableValue eval_stack[2];
    const char *dialog_error_message;
    int loading;
    int starttime;
    uint32_t localVarsStack[2048];
    bool recordPlace;
    // padding
    char *lastFileName;
    int lastLine;
};
RUNTIME_SIZE(MirroredScrVmGlobT, 0x2028, 0x2048);

// scr_compiler.h (M4 ki-n1et)
struct MirroredCaseStatementInfo
{
    uint32_t name;
    const char *codePos;
    uint32_t sourcePos;
    MirroredCaseStatementInfo *next;
};
RUNTIME_SIZE(MirroredCaseStatementInfo, 0x10, 0x20);

struct MirroredBreakStatementInfo
{
    char *codePos;
    const char *nextCodePos;
    MirroredBreakStatementInfo *next;
};
RUNTIME_SIZE(MirroredBreakStatementInfo, 0xC, 0x18);

struct MirroredContinueStatementInfo
{
    char *codePos;
    const char *nextCodePos;
    MirroredContinueStatementInfo *next;
};
RUNTIME_SIZE(MirroredContinueStatementInfo, 0xC, 0x18);

struct MirroredVariableCompileValue
{
    VariableValue value;
    MirroredSvalU sourcePos;
};
RUNTIME_SIZE(MirroredVariableCompileValue, 0xC, 0x18);

struct MirroredScrCompileGlobT
{
    uint8_t *codePos;
    uint8_t *prevOpcodePos;
    uint32_t fileId;
    uint32_t threadId;
    int cumulOffset;
    int maxOffset;
    int maxCallOffset;
    bool bConstRefCount;
    bool in_developer_thread;
    // padding
    uint32_t developer_thread_sourcePos;
    bool firstThread[2];
    // padding
    MirroredCaseStatementInfo *currentCaseStatement;
    bool bCanBreak;
    // padding
    MirroredBreakStatementInfo *currentBreakStatement;
    bool bCanContinue;
    // padding
    MirroredContinueStatementInfo *currentContinueStatement;
    scr_block_s **breakChildBlocks;
    int *breakChildCount;
    scr_block_s *breakBlock;
    scr_block_s **continueChildBlocks;
    int *continueChildCount;
    bool forceNotCreate;
    // padding
    PrecacheEntry *precachescriptList;
    MirroredVariableCompileValue value_start[32];
};
RUNTIME_SIZE(MirroredScrCompileGlobT, 0x1D8, 0x390);

// scr_animtree.h (M4 ki-n1et). scr_animtree_t lives in bg_local.h and
// carries a live `XAnim_s *anims` host pointer: it is the 4 -> 8 widening
// that drives scrAnimPub_t.
struct MirroredScrAnimtreeT
{
    XAnim_s *anims;
};
RUNTIME_SIZE(MirroredScrAnimtreeT, 0x4, 0x8);

struct MirroredScrAnimPubT
{
    uint32_t animtrees;
    uint32_t animtree_node;
    uint32_t animTreeNames;
    MirroredScrAnimtreeT xanim_lookup[2][128];
    uint32_t xanim_num[2];
    uint32_t animTreeIndex;
    bool animtree_loading;
    // padding
};
RUNTIME_SIZE(MirroredScrAnimPubT, 0x41C, 0x820);

struct MirroredScrAnimGlobT
{
    const char *start;
    const char *pos;
    uint16_t using_xanim_lookup[2][128];
    int bAnimCheck;
};
RUNTIME_SIZE(MirroredScrAnimGlobT, 0x20C, 0x218);

// scr_const.h (M4 ki-n1et): every field is a uint16_t script-string
// handle, so the struct is frozen at 186 * 2 == 0x174 on every target.
template <int N>
struct MirroredUint16Table
{
    uint16_t v[N];
};
RUNTIME_SIZE(MirroredUint16Table<186>, 0x174, 0x174);

void CheckMirroredWidening()
{
    CHECK(sizeof(MirroredSvalU) == Width(0x4, 0x8));
    CHECK(sizeof(MirroredDebuggerSvalS) == Width(0x4, 0x8));
    CHECK(sizeof(MirroredScriptExpressionT) == Width(0xC, 0x18));
    CHECK(sizeof(MirroredScrBreakpoint) == Width(0x1C, 0x30));
    CHECK(sizeof(MirroredScrWatchElementS) == Width(0x64, 0xA0));
    CHECK(sizeof(MirroredScrOpcodeListS) == Width(0x8, 0x10));
    CHECK(sizeof(MirroredScrWatchElementNodeS) == Width(0x8, 0x10));
    CHECK(sizeof(MirroredScrWatchElementDoubleNodeT) == Width(0x8, 0x10));
    CHECK(sizeof(MirroredArchivedCanonicalStringInfo) == Width(0x8, 0x10));
    CHECK(sizeof(MirroredScrEvaluateGlobT) == Width(0x10, 0x20));
    CHECK(sizeof(MirroredScrStringNodeS) == Width(0x8, 0x10));
    CHECK(sizeof(MirroredFunctionStackT) == Width(0x14, 0x20));
    CHECK(sizeof(MirroredFunctionFrameT) == Width(0x18, 0x28));
    CHECK(sizeof(MirroredScrVmPubT) == Width(0x4328, 0x8540));
    CHECK(sizeof(MirroredFuncDebugData) == Width(0x10, 0x18));
    CHECK(sizeof(MirroredScrVmDebugPubT) == Width(0x24210, 0x26410));
    CHECK(sizeof(MirroredScrVmGlobT) == Width(0x2028, 0x2048));
    CHECK(sizeof(MirroredCaseStatementInfo) == Width(0x10, 0x20));
    CHECK(sizeof(MirroredBreakStatementInfo) == Width(0xC, 0x18));
    CHECK(sizeof(MirroredContinueStatementInfo) == Width(0xC, 0x18));
    CHECK(sizeof(MirroredVariableCompileValue) == Width(0xC, 0x18));
    CHECK(sizeof(MirroredScrCompileGlobT) == Width(0x1D8, 0x390));
    CHECK(sizeof(MirroredScrAnimtreeT) == Width(0x4, 0x8));
    CHECK(sizeof(MirroredScrAnimPubT) == Width(0x41C, 0x820));
    CHECK(sizeof(MirroredScrAnimGlobT) == Width(0x20C, 0x218));
    CHECK(sizeof(MirroredUint16Table<186>) == Width(0x174, 0x174));
}
}  // namespace

int main()
{
    // 1: real portable headers -- widened + frozen contracts.
    CheckPortableHeaderWidening();

    // 2: mirrored headers -- both-width natural layout at the declared
    // constants.
    CheckMirroredWidening();

    if (script_native64_layout_test::g_failures != 0)
    {
        std::fprintf(stderr, "script_native64_layout_test: %d/%d checks failed\n",
                     script_native64_layout_test::g_failures,
                     script_native64_layout_test::g_runs);
        return 1;
    }
    std::printf("script_native64_layout_test: %d checks passed\n",
                script_native64_layout_test::g_runs);
    return 0;
}
