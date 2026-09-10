// Actual production bodies with real runtime records; only engine services
// are doubled. ASan/UBSan runs cover native allocations and consumers above
// 4 GiB. This is a focused contract fixture, not a complete engine session.
#include <script/scr_main.h>
#include <script/scr_debugger.h>
#include <script/scr_evaluate.h>
#include <script/scr_compiler.h>
#include <script/scr_stringlist.h>
#include <universal/memfile.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

scrVarPub_t scrVarPub{};
scrVarGlob_t scrVarGlob{};
scrCompilePub_t scrCompilePub{};
scrEvaluateGlob_t scrEvaluateGlob{};
struct { int function_count = 0; } scrVmPub;
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
uint32_t FindFirstSibling(uint32_t) { bool found = siblingPending; siblingPending = false; return found ? 2 : 0; }
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
int main()
{
    TestAllocations(); TestTerminate(); TestDebugReferences(); TestSaveObject();
    for (void *p : allocations) std::free(p);
    std::printf("script runtime: %d checks passed\n", checks);
}
