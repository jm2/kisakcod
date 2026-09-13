// Production animation records and function bodies; external engine services
// are bounded doubles. This is not a complete game or retail-content run.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <qcommon/com_error.h>
#include <script/scr_main.h>
#include <script/scr_compiler.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <vector>
struct XAnim_s;
#include "script_animation_types.inc"

namespace {
int checks = 0;
void Check(bool condition) { ++checks; if (!condition) { std::fprintf(stderr, "animation fixup check %d failed\n", checks); std::abort(); } }
void High(const void *p) {
#if defined(KISAK_REQUIRE_HIGH_POINTERS) && UINTPTR_MAX > UINT32_MAX
    Check(reinterpret_cast<uintptr_t>(p) > UINT32_MAX);
#else
    Check(p != nullptr);
#endif
}
struct EngineError { bool compile; };
std::map<uint32_t, VariableValueInternal_u> cells;
std::set<void *> liveAllocations;
std::vector<uint32_t> releasedObjects;
bool failAllocation = false;
char *diagnosticPosition = nullptr;
std::array<char, 25> code{};
VariableValue vmValue;
int inParams = 0;
}
scrVarPub_t scrVarPub{};
scrCompilePub_t scrCompilePub{};
scrVarDebugPub_t debugState{};
scrVarDebugPub_t *scrVarDebugPub = &debugState;
scrAnimPub_t scrAnimPub{};
struct { VariableValue *top; } scrVmPub{&vmValue};
void IncInParam() { ++inParams; }
void *Z_Malloc(int size, const char *, int) {
    if (failAllocation) return nullptr;
    void *p = std::malloc(static_cast<size_t>(size)); Check(p != nullptr); High(p);
    Check(liveAllocations.insert(p).second); return p;
}
void Z_Free(void *p, int) { Check(liveAllocations.erase(p) == 1); std::free(p); }
uint32_t FindVariable(uint32_t names, uint32_t name) { auto id = names * 256 + name; return cells.count(id) ? id : 0; }
uint32_t GetNewVariable(uint32_t names, uint32_t name) { auto id = names * 256 + name; Check(!cells.count(id)); cells[id] = {}; return id; }
VariableValueInternal_u *GetVariableValueAddress(uint32_t id) { Check(cells.count(id) == 1); return &cells.at(id); }
void SetVariableValue(uint32_t id, VariableValue *value) { Check(value->type == VAR_CODEPOS); cells.at(id).u = value->u; }
uint32_t GetVariableName(uint32_t id) { return id % 256; }
uint32_t FindFirstSibling(uint32_t names) { auto it = cells.lower_bound(names * 256); return it != cells.end() && it->first / 256 == names ? it->first : 0; }
uint32_t FindNextSibling(uint32_t id) { auto it = cells.upper_bound(id); return it != cells.end() && it->first / 256 == id / 256 ? it->first : 0; }
const char *SL_ConvertToString(uint32_t) { return "fixture"; }
char *va(const char *, ...) { static char message[] = "undefined animation"; return message; }
void Com_Error(errorParm_t, const char *, ...) { throw EngineError{false}; }
void CompileError2(char *pos, const char *, ...) { diagnosticPosition = pos; throw EngineError{true}; }
void ClearObject(uint32_t id) { Check(id == 7); cells.clear(); }
void RemoveRefToObject(uint32_t id) { releasedObjects.push_back(id); }
void SL_ShutdownSystem(uint32_t user) { Check(user == 2); }
char *TempMalloc(uint32_t size) { Check(size == 0); return code.data() + code.size(); }
#include "script_animation_slice.inc"

namespace {
void Begin(bool debug = true) {
    Check(liveAllocations.empty()); Check(scrAnimationFixups == nullptr);
    cells.clear(); releasedObjects.clear(); code.fill(static_cast<char>(0x55));
    scrAnimPub = {}; scrAnimPub.animtrees = 7; scrAnimPub.animtree_loading = true;
    scrVarPub = {}; scrVarPub.programBuffer = code.data(); scrCompilePub.programLen = code.size();
    scrVarPub.varUsagePos = "fixture"; scrVarDebugPub = debug ? &debugState : nullptr;
    debugState.extRefCount[7] = 1; diagnosticPosition = nullptr;
}
void End() {
    Scr_EndLoadAnimTrees();
    Check(liveAllocations.empty() && scrAnimationFixups == nullptr && cells.empty());
    Check(scrAnimPub.animtrees == 0 && !scrAnimPub.animtree_loading);
    Check(scrVarPub.varUsagePos == nullptr && scrVarPub.endScriptBuffer == code.data() + code.size());
    Check(!releasedObjects.empty() && releasedObjects[0] == 7);
    if (scrVarDebugPub) Check(debugState.extRefCount[7] == 0);
    Scr_ClearAnimationFixups(); Check(liveAllocations.empty());
}
template<class F> void Error(F fn, bool compile) {
    bool failed = false;
    try { fn(); } catch (EngineError e) { failed = true; Check(e.compile == compile); }
    Check(failed);
}
void TestFrozenHandlesAndMixedSites() {
    Begin();
    static_assert(sizeof(scr_anim_s) == 4 && sizeof(loadAnim_t) == 72);
    auto handle = std::make_unique<scr_anim_s>();
    auto record = std::make_unique<loadAnim_t>();
    High(handle.get()); High(record.get()); High(code.data());
    record->iNameHash = 0x12345678; std::memcpy(record->szAnimName, "keep", 5);
    Scr_EmitAnimationInternal(reinterpret_cast<char *>(&record->anim), 2, 1);
    Scr_EmitAnimationInternal(code.data() + 1, 2, 1);
    Scr_EmitAnimationInternal(reinterpret_cast<char *>(handle.get()), 2, 1);
    Check(record->iNameHash == 0x12345678 && std::strcmp(record->szAnimName, "keep") == 0);
    Check(handle->packed == 0 && record->anim.packed == 0 && liveAllocations.size() == 3);
    High(cells.at(258).u.codePosValue);
    ConnectScriptToAnim(1, 0xabcd, 9, 2, 0x1234);
    Check(handle->index == 0xabcd && handle->tree == 0x1234);
    Check(record->anim.packed == handle->packed && record->iNameHash == 0x12345678);
    scr_anim_s fromCode; std::memcpy(&fromCode, code.data() + 1, sizeof(fromCode));
    Check(fromCode.packed == handle->packed);
    Check(code[0] == 0x55 && code[5] == 0x55 && code.back() == 0x55);
    Check(cells.at(258).u.codePosValue == nullptr);
    Scr_CheckAnimsDefined(1, 9);
    Error([] { ConnectScriptToAnim(1, 1, 9, 2, 1); }, false);
    vmValue.u.codePosValue = code.data(); inParams = 0; Scr_AddAnim(*handle);
    Check(inParams == 1 && vmValue.type == VAR_ANIMATION && vmValue.u.stringValue == handle->packed);
    const auto *bytes = reinterpret_cast<const unsigned char *>(&vmValue.u);
    for (size_t i = 4; i < sizeof(vmValue.u); ++i) Check(bytes[i] == 0);
    scrAnimPub.animtree_node = 9; End(); Check(releasedObjects.size() == 2 && releasedObjects[1] == 9);
}
void TestUndefinedDiagnosticsAndAbortCleanup() {
    Begin(false);
    Scr_EmitAnimationInternal(code.data() + 7, 3, 1);
    Error([] { Scr_CheckAnimsDefined(1, 9); }, true);
    Check(diagnosticPosition == code.data() + 7);
    End();
    Begin(); auto handle = std::make_unique<scr_anim_s>();
    Scr_EmitAnimationInternal(reinterpret_cast<char *>(handle.get()), 3, 2);
    Error([] { Scr_CheckAnimsDefined(2, 9); }, false);
    Check(diagnosticPosition == nullptr);
    Check(!Scr_IsInOpcodeMemory(reinterpret_cast<char *>(handle.get())));
    Check(Scr_IsInOpcodeMemory(code.data()) && Scr_IsInOpcodeMemory(code.data() + code.size() - 1));
    Check(!Scr_IsInOpcodeMemory(code.data() + code.size()));
    Check(!Scr_IsInOpcodeMemory(reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(code.data()) - 1)));
    End();
}
void TestIndependentNamesAndAllocationFailure() {
    Begin(); scr_anim_s first, other, failed(17);
    Scr_EmitAnimationInternal(reinterpret_cast<char *>(&first), 1, 1);
    Scr_EmitAnimationInternal(reinterpret_cast<char *>(&other), 1, 2);
    auto head = cells.at(257).u.codePosValue;
    failAllocation = true;
    Error([&] { Scr_EmitAnimationInternal(reinterpret_cast<char *>(&failed), 1, 1); }, false);
    failAllocation = false;
    Check(cells.at(257).u.codePosValue == head && failed.packed == 17 && liveAllocations.size() == 2);
    ConnectScriptToAnim(1, 4, 9, 1, 5); Check(first.index == 4 && first.tree == 5 && other.packed == 0);
    ConnectScriptToAnim(2, 6, 9, 1, 7); Check(other.index == 6 && other.tree == 7);
    End(); Begin(); End();
}
}
int main() {
    TestFrozenHandlesAndMixedSites(); TestUndefinedDiagnosticsAndAbortCleanup(); TestIndependentNamesAndAllocationFailure();
    std::printf("animation fixups: %d checks passed\n", checks);
}
