// Actual production bodies with real runtime records; only engine services
// are doubled. ASan/UBSan runs cover native allocations and consumers above
// 4 GiB. This is a focused contract fixture, not a complete engine session.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <script/scr_main.h>
#include <script/scr_debugger.h>
#include <script/scr_evaluate.h>
#include <script/scr_compiler.h>
#include <script/scr_stringlist.h>
#include <universal/memfile.h>
#include <universal/sys_atomic.h>
#include <script/scr_bytecode.hpp>
#include <cmath>
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

scrVarPub_t scrVarPub{};
scrVarGlob_t scrVarGlob{};
scrCompilePub_t scrCompilePub{};
scrCompileGlob_t scrCompileGlob{};
scrVarDebugPub_t scrVarDebugPubBuf{};
struct { struct { int breakpointCount; const char *name; } func_table[SCR_FUNC_TABLE_SIZE]; } scrVmDebugPub;
scrEvaluateGlob_t scrEvaluateGlob{};
struct FixtureFrame { struct { uint32_t localId = 37; } fs; } fixtureFrame;
struct { int function_count = 0; FixtureFrame *function_frame = &fixtureFrame; } scrVmPub;
scrVarDebugPub_t debugState{};
scrVarDebugPub_t *scrVarDebugPub = &debugState;
scrStringDebugGlob_t *scrStringDebugGlob = nullptr;
// The UI debugger global is deliberately absent in headless headers.
struct { char *assignHeadCodePos = nullptr; Scr_OpcodeList_s *assignHead = nullptr; } scrDebuggerGlob;

namespace {
std::vector<void *> allocations;
VariableValueInternal_u archived{};
VariableStackBuffer *observedStack = nullptr;
VariableValue savedChild{};
bool siblingPending = true;
bool notifyFixture = false;
uint32_t fixtureSelf = 71;
std::vector<const float *> releasedVectors;
const float *formattedVector = nullptr;
struct InitializedRange { uint32_t first; uint32_t second; };
std::vector<InitializedRange> initializedRanges;
char bytecode[4096]{};
size_t codeSize = 0;
CaseStatementInfo fixtureCaseStorage[3]{};
CaseStatementInfo *fixtureCases = nullptr;
char fixtureDiagnostic[] = "fixture diagnostic";
int callbackCount = 0;
int methodEntity = 0;
const char *formattedCodePos = nullptr;
int checks = 0;
void CheckAt(bool okay, const char *expression, int line) { ++checks; if (!okay) { std::fprintf(stderr, "script runtime:%d: %s failed\n", line, expression); std::abort(); } }
#define Check(condition) CheckAt((condition), #condition, __LINE__)
void *Allocate(size_t bytes) { void *p = std::malloc(bytes); Check(p != nullptr); allocations.push_back(p); return p; }
void HighAddress(const void *p) {
#if defined(KISAK_REQUIRE_HIGH_POINTERS) && UINTPTR_MAX > 0xffffffffu
    Check(reinterpret_cast<uintptr_t>(p) > UINT32_MAX);
#else
    Check(p != nullptr);
#endif
}
}
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
uint32_t *Scr_AllocDebugMem(int size, const char *) { return static_cast<uint32_t *>(Allocate(size)); }
void *Hunk_AllocDebugMem(uint32_t size) { return Allocate(size); }
const char *CopyString(const char *p) { return p; }
const char *SL_ConvertToString(uint32_t id) { return id == 1 ? "zebra" : "alpha"; }
void AddRefToObject(uint32_t) {}
void RemoveRefToObject(uint32_t) {}
uint32_t FindFirstSibling(uint32_t id) { if (notifyFixture) return id == 11 ? 12 : id == 13 ? 14 : 0; bool found = siblingPending; siblingPending = false; return found ? 2 : 0; }
uint32_t FindNextSibling(uint32_t) { return 0; }
uint32_t FindLastSibling(uint32_t) { return 2; }
uint32_t FindPrevSibling(uint32_t) { return 0; }
uint32_t GetVariableKeyObject(uint32_t) { return 3; }
Vartype_t GetValueType(uint32_t) { return VAR_STACK; }
VariableValueInternal_u *GetVariableValueAddress(uint32_t) { return &archived; }
void RemoveObjectVariable(uint32_t, uint32_t) {}
void Scr_ClearWaitTime(uint32_t) {}
void VM_TerminateStack(uint32_t, uint32_t, VariableStackBuffer *p) { observedStack = p; Check(p->localId == 37); }
void Scr_AddDebugRefCount(uint16_t *) {}
void CheckReferenceRange(unsigned int, unsigned int) {}
bool Scr_IsVariableBreakpoint(unsigned int id) { return id == 2; }
bool IsObject(VariableValueInternal *entry) { return (entry->w.type & VAR_MASK) >= VAR_THREAD; }
void WriteId(unsigned int, unsigned int, MemoryFile *) {}
void SafeWriteString(unsigned __int16, MemoryFile *) {}
int MemFile_GetUsedSize(MemoryFile *) { return 0; }
void MemFile_WriteData(MemoryFile *, int, const void *) {}
void DoSaveEntry(VariableValue *value, VariableValue *, bool, MemoryFile *) { savedChild = *value; }

void Com_Error(errorParm_t, const char *, ...) { std::abort(); }
void CompileError(uint32_t, const char *, ...) { std::abort(); }
char *TempMalloc(uint32_t size) { Check(codeSize + size <= sizeof(bytecode)); char *p = bytecode + codeSize; codeSize += size; return p; }
char *TempMallocAlignStrict(uint32_t size) { return TempMalloc(size); }
void EmitExpression(sval_u, scr_block_s *) {}
void EmitOpcode(uint32_t op, int, int) { *TempMalloc(1) = static_cast<char>(op); }
void EmitSwitchStatementList(sval_u, bool, uint32_t, scr_block_s *) { scrCompileGlob.currentCaseStatement = fixtureCases; }
void AddOpcodePos(uint32_t, int) {}
void ConnectBreakStatements() {}
void Scr_InitVariableRange(uint32_t begin, uint32_t end) { initializedRanges.emplace_back(begin, end); }
uint32_t GetSafeParentLocalId(uint32_t) { return 0; }
uint32_t Scr_GetSelf(uint32_t) { return fixtureSelf; }
uint32_t FindVariable(uint32_t, uint32_t) { return notifyFixture ? 10 : 0; }
uint32_t FindObject(uint32_t id) { return id == 10 ? 11 : id == 12 ? 13 : 0; }
void Scr_CastWeakerPair(VariableValue *a, VariableValue *b) { Check(a->type == b->type); }
void Scr_UnmatchingTypesError(VariableValue *, VariableValue *) { std::abort(); }
void SL_RemoveRefToString(uint32_t) {}
void RemoveRefToVector(const float *p) { releasedVectors.push_back(p); }
uint32_t SL_GetStringForVector(const float *p) { formattedVector = p; Check(p[2] == 3); return 77; }
uint32_t SL_GetStringForFloat(float) { return 78; }
uint32_t SL_GetStringForInt(int) { return 79; }
float I_fabs(float value) { return std::fabs(value); }

struct XAnim_s;
const XAnim_s *Scr_GetAnims(unsigned int) { std::abort(); }
char *XAnimGetAnimDebugName(const XAnim_s *, unsigned int) { std::abort(); }
uint32_t GetObjectType(uint32_t) { return VAR_OBJECT; }
int Scr_GetEntNum(uint32_t) { return 0; }
char Scr_GetEntClassId(uint32_t) { return 'e'; }
uint32_t GetArraySize(uint32_t) { return 0; }
void I_strncpyz(char *out, const char *text, int size) { std::snprintf(out, size, "%s", text); }
int Com_sprintf(char *out, uint32_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    // Flawfinder: ignore -- every fixture call uses a production literal format and explicit buffer size.
    int count = std::vsnprintf(out, size, format, args);
    va_end(args);
    return count;
}
void Scr_GetCodePos(const char *position, unsigned int, char *out, int size) { formattedCodePos = position; I_strncpyz(out, "function", size); }

char *Scr_GetReturnPos(uint32_t *) { return nullptr; }
void AddRefToValue(int, VariableUnion) {}
void Scr_CastBool(VariableValue *value) { Check(value->type == VAR_INTEGER); }
void Scr_ClearErrorMessage() { scrVarPub.error_message = nullptr; }
bool IsValidArrayIndex(uint32_t value) { return value < MAX_ARRAYINDEX; }
uint32_t GetInternalVariableIndex(uint32_t value) { return value; }
char *va(const char *, ...) { return fixtureDiagnostic; }
void EmitByte(unsigned char value) { *TempMalloc(1) = static_cast<char>(value); }

#include "script_runtime_slice.inc"

void TestAllocations()
{
    Scr_WatchElement_s *watch = nullptr;
    Scr_CreateWatchElement(const_cast<char *>("watch"), &watch, "runtime test");
    HighAddress(watch);
    Check(watch->next == nullptr && std::strcmp(watch->refText, "watch") == 0);
    scrVarPub.developer = true;
    char code = 0;
    Scr_AddAssignmentPos(&code);
    HighAddress(scrDebuggerGlob.assignHead);
    Check(scrDebuggerGlob.assignHead->codePos == &code);
    scrVarPub.canonicalStrCount = 2;
    scrCompilePub.canonicalStrings[1] = 1;
    scrCompilePub.canonicalStrings[2] = 2;
    Scr_ArchiveCanonicalStrings();
    HighAddress(scrEvaluateGlob.archivedCanonicalStrings);
    Check(std::strcmp(scrEvaluateGlob.archivedCanonicalStrings[0].value, "alpha") == 0);
    Check(scrEvaluateGlob.archivedCanonicalStrings[0].canonicalStr == 2);
    Check(std::strcmp(scrEvaluateGlob.archivedCanonicalStrings[1].value, "zebra") == 0);
    Check(scrEvaluateGlob.canonicalStringLookup[1] == 1);
}
void TestTerminate()
{
    auto *stack = static_cast<VariableStackBuffer *>(Allocate(sizeof(VariableStackBuffer)));
    *stack = {};
    stack->localId = 37;
    archived.u.stackValue = stack;
    HighAddress(stack);
    siblingPending = true;
    VM_TerminateTime(1);
    Check(observedStack == stack);
}
void TestDebugReferences()
{
    // The former 0x70002 byte offset lands in varUsage on native64.
    std::fill_n(debugState.varUsage, 0x18000, "unchanged");
    Check(CheckReferences() == 1);
    Check(debugState.refCount[1] == 1);
    Check(std::all_of(std::begin(debugState.varUsage), std::end(debugState.varUsage),
        [](const char *p) { return p == debugState.varUsage[0]; }));
}
void TestSaveObject()
{
    scrVarGlob.variableList[2].w.type = VAR_OBJECT | 0x60;
    scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN + 2].hash.id = 7;
    auto &child = scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN + 7];
    child.w.type = VAR_VECTOR | 0x20;
    float vector[3] = {1, 2, 3};
    HighAddress(vector);
    child.u.u.vectorValue = vector;
    siblingPending = true;
    DoSaveObjectInfo(1, nullptr);
    Check(savedChild.type == VAR_VECTOR);
    Check(savedChild.u.vectorValue == vector);
    Check(savedChild.u.vectorValue[2] == 3);
}
void BuiltinCallback() { ++callbackCount; }
void BuiltinMethod(scr_entref_t entity) { methodEntity = entity.entnum; }
void TestBuiltinsAndSwitch()
{
    const auto callback = reinterpret_cast<uintptr_t>(&BuiltinCallback);
    HighAddress(reinterpret_cast<const void *>(callback));
    Check(AddFunction(callback, "callback") == 0);
    Check(AddFunction(callback, "duplicate") == 0);
    Check(scrCompilePub.func_table_size == 1);
    reinterpret_cast<void (*)()>(scrCompilePub.func_table[0])();
    Check(callbackCount == 1);
    Check(scrVmDebugPub.func_table[0].breakpointCount == 0);
    const auto method = reinterpret_cast<uintptr_t>(&BuiltinMethod);
    HighAddress(reinterpret_cast<const void *>(method));
    Check(AddFunction(method, "method") == 1);
    scr_entref_t entity{}; entity.entnum = 37;
    reinterpret_cast<void (*)(scr_entref_t)>(scrCompilePub.func_table[1])(entity);
    Check(methodEntity == 37);
    Check(std::strcmp(scrVmDebugPub.func_table[0].name, "callback") == 0);
    // The real compiler emits and sorts unaligned records, then the real VM
    // readers must retain each name's matching native branch pointer.
    char *branches = static_cast<char *>(Allocate(3));
    HighAddress(branches);
    CaseStatementInfo *cases = fixtureCaseStorage;
    cases[0] = {3, branches, 1, nullptr};
    cases[1] = {9, branches + 1, 2, nullptr};
    cases[2] = {0, branches + 2, 3, nullptr};
    cases[0].next = &cases[1]; cases[1].next = &cases[2];
    fixtureCases = cases;
    EmitSwitchStatement({}, {}, {}, false, 0, nullptr);
    Check(static_cast<unsigned char>(bytecode[0]) == OP_switch);
    const char *position = bytecode + 1;
    const uintptr_t offset = Scr_ReadUnsigned(&position);
    position += offset;
    Check(Scr_ReadUnsignedShort(&position) == 3);
    const char *skip = position;
    const uintptr_t names[3] = {9, 3, 0};
    const char *targets[3] = {branches + 1, branches, branches + 2};
    for (int i = 0; i < 3; ++i) {
        Check(Scr_ReadUnsigned(&position) == names[i]);
        Check(Scr_ReadCodePos(&position) == targets[i]);
    }
    Scr_SkipSwitchCases(&skip, 3);
    Check(skip == position && position == bytecode + codeSize);
}
void TestNativeConsumers()
{
    float first[3] = {1, 2, 3};
    float second[3] = {1, 2, 3};
    HighAddress(first); HighAddress(second);
    for (int same = 1; same >= 0; --same) {
        second[0] = same ? 1 : 9;
        VariableValue a{}; VariableValue b{};
        a.type = b.type = VAR_VECTOR;
        a.u.vectorValue = first; b.u.vectorValue = second;
        releasedVectors.clear();
        Scr_EvalEquality(&a, &b);
        Check(a.type == VAR_INTEGER && a.u.intValue == same);
        Check(releasedVectors == std::vector<const float *>({first, second}));
    }
    for (int direction = 0; direction < 2; ++direction) {
        VariableValue string{}; VariableValue vector{};
        string.type = VAR_STRING; string.u.stringValue = 1;
        vector.type = VAR_VECTOR; vector.u.vectorValue = first;
        releasedVectors.clear(); formattedVector = nullptr;
        if (direction) Scr_CastWeakerStringPair(&vector, &string);
        else Scr_CastWeakerStringPair(&string, &vector);
        Check(vector.type == VAR_STRING && vector.u.stringValue == 77);
        Check(formattedVector == first);
        Check(releasedVectors == std::vector<const float *>({first}));
    }
#if UINTPTR_MAX > 0xffffffffu
    VariableValue a{}; VariableValue b{};
    a.type = b.type = VAR_FUNCTION;
    a.u.codePosValue = reinterpret_cast<const char *>(uintptr_t{0x100001234});
    b.u.codePosValue = reinterpret_cast<const char *>(uintptr_t{0x200001234});
    Scr_EvalEquality(&a, &b);
    Check(a.u.intValue == 0);
#endif
}
void TestNativeOperandPositions()
{
    // The active compiler uses EmitCodepos for these scalar-valued slots.
    // Check payload AND consumption, with a following opcode as a sentinel.
    for (int number : {70000, -70000}) {
        codeSize = 0;
        EmitGetInteger(number, {});
        EmitOpcode(OP_GetZero, 1, 0);
        Check(static_cast<unsigned char>(bytecode[0]) == OP_GetInteger);
        const char *position = bytecode + 1;
        Check(Scr_ReadInt(&position) == number);
        Check(position == bytecode + codeSize - 1 && *position == OP_GetZero);
    }
    scrVarPub.evaluate = true;
    VariableValue top{}; top.type = VAR_INTEGER; top.u.intValue = 3;
    uint32_t localId = 0;
    for (Opcode_t op : {OP_GetInteger, OP_GetAnimation, OP_GetFunction,
             OP_ScriptFunctionCall2, OP_ScriptFunctionCall, OP_ScriptMethodCall,
             OP_ScriptThreadCallPointer, OP_ScriptMethodThreadCallPointer}) {
        codeSize = 0; EmitOpcode(op, 0, 0); EmitCodepos(nullptr); EmitOpcode(OP_GetZero, 1, 0);
        Check(Scr_GetNextCodepos(&top, bytecode, op, 1, &localId) == bytecode + 1 + sizeof(uintptr_t));
    }
    for (Opcode_t op : {OP_ScriptThreadCall, OP_ScriptMethodThreadCall, OP_object}) {
        codeSize = 0; EmitOpcode(op, 0, 0); EmitCodepos(nullptr); EmitCodepos(nullptr); EmitOpcode(OP_GetZero, 1, 0);
        Check(Scr_GetNextCodepos(&top, bytecode, op, 1, &localId) == bytecode + 1 + 2 * sizeof(uintptr_t));
    }
    codeSize = 0; EmitOpcode(OP_GetFloat, 0, 0); TempMalloc(sizeof(float)); EmitOpcode(OP_GetZero, 1, 0);
    Check(Scr_GetNextCodepos(&top, bytecode, OP_GetFloat, 1, &localId) == bytecode + 1 + sizeof(float));
    char branch{}; HighAddress(&branch);
    codeSize = 0; EmitOpcode(OP_ScriptFunctionCall, 0, 0); EmitCodepos(&branch);
    Check(Scr_GetNextCodepos(&top, bytecode, OP_ScriptFunctionCall, 2, &localId) == &branch);
    codeSize = 0; EmitOpcode(OP_jump, 0, 0); EmitCodepos(reinterpret_cast<const char *>(uintptr_t{3})); TempMalloc(4);
    Check(Scr_GetNextCodepos(&top, bytecode, OP_jump, 1, &localId) == bytecode + 1 + sizeof(uintptr_t) + 3);
    const char *position = bytecode + 1; const int jump = Scr_ReadInt(&position); position += jump;
    Check(position == bytecode + 1 + sizeof(uintptr_t) + 3);
    // Exercise the actual debugger switch walker against production emission.
    codeSize = 0; EmitSwitchStatement({}, {}, {}, false, 0, nullptr);
    Check(Scr_GetNextCodepos(&top, bytecode, OP_switch, 1, &localId) == fixtureCases->codePos);
    const char *endSwitch = bytecode + 1 + sizeof(uintptr_t);
    Check(Scr_GetNextCodepos(&top, endSwitch, OP_endswitch, 1, &localId) == bytecode + codeSize);
}
void TestDebuggerFormatting()
{
    float coordinates[3] = {1, 2, 3}; HighAddress(coordinates);
    VariableValue vector{}; vector.type = VAR_VECTOR; vector.u.vectorValue = coordinates;
    char text[32]{};
    Scr_GetValueString(0, &vector, sizeof(text), text);
    Check(std::strcmp(text, "(1, 2, 3)") == 0);
    char bounded[5]{};
    Scr_GetValueString(0, &vector, sizeof(bounded), bounded);
    Check(bounded[4] == 0);
    char code[2]{}; HighAddress(code);
    VariableValue function{}; function.type = VAR_FUNCTION; function.u.codePosValue = code + 1;
    Scr_GetValueString(0, &function, sizeof(text), text);
    Check(formattedCodePos == code && std::strcmp(text, "function") == 0);
}
void TestArchivedThreads()
{
    std::memset(&scrVarGlob, 0, sizeof(scrVarGlob));
    auto *stack = static_cast<VariableStackBuffer *>(Allocate(sizeof(VariableStackBuffer)));
    *stack = {}; stack->localId = 37; stack->size = 2; stack->bufLen = 19;
    HighAddress(stack);
    auto &entry = scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN + 1];
    entry.w.status = VAR_STACK | 0x20; entry.u.u.stackValue = stack;
    uint32_t threads[4]{};
    Check(Scr_FindAllThreads(71, threads, 0) == 1 && threads[0] == 37);
    entry.w.status = 0;
    archived.u.stackValue = stack; notifyFixture = true; fixtureSelf = 72;
    Check(Scr_FindAllThreads(71, threads, 0) == 1 && threads[0] == 37);
    notifyFixture = false;
}
void TestVariableReinitialization()
{
    for (int pass = 0; pass < 2; ++pass) {
        std::fill_n(debugState.varUsage, 0x18000, "previous session");
        initializedRanges.clear();
        Scr_InitVariables();
        Check(std::all_of(std::begin(debugState.varUsage), std::end(debugState.varUsage), [](const char *p) { return p == nullptr; }));
        Check(initializedRanges.size() == 2);
        Check(initializedRanges[0].first == VARIABLELIST_PARENT_BEGIN);
        Check(initializedRanges[1].first == VARIABLELIST_CHILD_BEGIN && initializedRanges[1].second == 0x18000);
    }
}
int main()
{
    TestAllocations(); TestTerminate(); TestDebugReferences(); TestSaveObject();
    TestBuiltinsAndSwitch(); TestNativeOperandPositions(); TestNativeConsumers(); TestDebuggerFormatting(); TestArchivedThreads(); TestVariableReinitialization();
    for (void *p : allocations) std::free(p);
    std::printf("script runtime: %d checks passed\n", checks);
}
