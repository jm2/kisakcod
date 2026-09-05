#pragma once

#include "scr_debugger.h"

#define MAX_PRECACHE_ENTRIES 1024

enum : __int32
{
    SOURCE_TYPE_NONE = 0,
    SOURCE_TYPE_BREAKPOINT = 0x1,
    SOURCE_TYPE_CALL = 0x2,
    SOURCE_TYPE_THREAD_START = 0x4,
    SOURCE_TYPE_BUILTIN_CALL = 0x8,
    SOURCE_TYPE_NOTIFY = 0x10,
};
enum : __int32
{
    SCR_DEV_NO = 0x0,
    SCR_DEV_YES = 0x1,
    SCR_DEV_IGNORE = 0x2,
    SCR_DEV_EVALUATE = 0x3,
};

enum : __int32
{
    SCR_ABORT_NONE = 0x0,
    SCR_ABORT_CONTINUE = 0x1,
    SCR_ABORT_BREAK = 0x2,
    SCR_ABORT_RETURN = 0x3,
    SCR_ABORT_MAX = 0x3,
};

struct CaseStatementInfo // sizeof=0x10
{
    uint32_t name;
    const char *codePos;
    uint32_t sourcePos;
    CaseStatementInfo *next;
};
// M4 (ki-n1et): carries `const char *codePos` and a self-typed next
// pointer; widens 0x10 -> 0x20 on 64-bit.
RUNTIME_SIZE(CaseStatementInfo, 0x10, 0x20);

struct BreakStatementInfo // sizeof=0xC
{
    char *codePos;
    const char *nextCodePos;
    BreakStatementInfo *next;
};
// M4 (ki-n1et): three host pointers; widens 0xC -> 0x18 on 64-bit.
RUNTIME_SIZE(BreakStatementInfo, 0xC, 0x18);

struct ContinueStatementInfo // sizeof=0xC
{
    char *codePos;
    const char *nextCodePos;
    ContinueStatementInfo *next;
};
// M4 (ki-n1et): three host pointers; widens 0xC -> 0x18 on 64-bit.
RUNTIME_SIZE(ContinueStatementInfo, 0xC, 0x18);

struct VariableCompileValue // sizeof=0xC
{                                       // ...
    VariableValue value;                // ...
    sval_u sourcePos;
};
// M4 (ki-n1et): embeds the widened VariableValue cell and sval_u parse
// cell; widens 0xC -> 0x18 on 64-bit.
RUNTIME_SIZE(VariableCompileValue, 0xC, 0x18);

#define VALUE_STACK_SIZE 32

struct scrCompileGlob_t // sizeof=0x1D8
{                                       // ...
    uint8_t *codePos;           // ...
    uint8_t *prevOpcodePos;     // ...
    uint32_t fileId;                // ...
    uint32_t threadId;              // ...
    int cumulOffset;                    // ...
    int maxOffset;                      // ...
    int maxCallOffset;                  // ...
    bool bConstRefCount;                // ...
    bool in_developer_thread;           // ...
    // padding byte
    // padding byte
    uint32_t developer_thread_sourcePos; // ...
    bool firstThread[2];                // ...
    // padding byte
    // padding byte
    CaseStatementInfo *currentCaseStatement; // ...
    bool bCanBreak;                     // ...
    // padding byte
    // padding byte
    // padding byte
    BreakStatementInfo *currentBreakStatement; // ...
    bool bCanContinue;                  // ...
    // padding byte
    // padding byte
    // padding byte
    ContinueStatementInfo *currentContinueStatement; // ...
    scr_block_s **breakChildBlocks;     // ...
    int *breakChildCount;               // ...
    scr_block_s *breakBlock;            // ...
    scr_block_s **continueChildBlocks;  // ...
    int *continueChildCount;            // ...
    bool forceNotCreate;                // ...
    // padding byte
    // padding byte
    // padding byte
    struct PrecacheEntry *precachescriptList;  // ...
    VariableCompileValue value_start[VALUE_STACK_SIZE]; // ...
};
// M4 (ki-n1et): compiler globals -- twelve host pointers plus the widened
// VariableCompileValue value_start[32] stack; widens 0x1D8 -> 0x390 on
// 64-bit. Compile-time scratch state, never serialized.
RUNTIME_SIZE(scrCompileGlob_t, 0x1D8, 0x390);

#define SCR_FUNC_TABLE_SIZE 1024

struct scrCompilePub_t
{
    int value_count;
    int far_function_count;
    uint32_t loadedscripts;
    uint32_t scripts;
    uint32_t builtinFunc;
    uint32_t builtinMeth;
    short canonicalStrings[65536];
    const char *in_ptr;
    const char *parseBuf;
    bool script_loading;
    bool allowedBreakpoint;
    int developer_statement;
    unsigned char *opcodePos;
    uint32_t programLen;
    int func_table_size;
    int func_table[SCR_FUNC_TABLE_SIZE];
};

void __cdecl Scr_CompileStatement(sval_u parseData);
void __cdecl ScriptCompile(
    sval_u val,
    uint32_t fileId,
    uint32_t scriptId,
    struct PrecacheEntry *entries,
    int entriesCount);

extern scrCompilePub_t scrCompilePub;
extern scrCompileGlob_t scrCompileGlob;