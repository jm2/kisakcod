// Actual save preparation and object/stack registration bodies, with bounded
// reference/variable-service doubles. No retail-content acceptance is implied.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <qcommon/com_error.h>
#include <script/scr_main.h>
#include <script/scr_stack_payload.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
int checks = 0;
void Check(bool condition) { ++checks; if (!condition) { std::fprintf(stderr, "save registration check %d failed\n", checks); std::abort(); } }
struct SaveError {};
unsigned int objectWithStack = 0;
int removedDebuggerRefs = 0;
}
scrVarPub_t scrVarPub{};
scrVarGlob_t scrVarGlob{};
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
void Com_Error(errorParm_t, const char *, ...) { throw SaveError{}; }
int CheckReferences() { return 1; }
void Scr_RemoveDebuggerRefs() { ++removedDebuggerRefs; }
void Com_Memset(void *p, const int value, const size_t length) { std::memset(p, value, length); }
bool IsObject(VariableValueInternal *v) { return (v->w.type & VAR_MASK) >= VAR_THREAD; }
uint32_t FindLastSibling(uint32_t id) { return id == objectWithStack ? 5 : 0; }
uint32_t FindPrevSibling(uint32_t) { return 0; }
VariableValue Scr_GetArrayIndexValue(uint32_t) { VariableValue value{}; value.type = VAR_INTEGER; return value; }
void AddSaveObject(unsigned int);
void Scr_AddSaveClassArrays() { AddSaveObject(60); }
#include "script_save_registration.inc"

namespace {
struct StackGraph {
    std::vector<VariableStackBuffer *> nodes;
    ~StackGraph() { for (auto *node : nodes) std::free(node); }
    VariableStackBuffer *Add(unsigned int localId, unsigned int count) {
        auto *node = static_cast<VariableStackBuffer *>(std::calloc(1, sizeof(VariableStackBuffer) + count * VARIABLE_STACK_RECORD_SIZE));
        if (!node) std::abort();
#if defined(KISAK_REQUIRE_HIGH_POINTERS) && UINTPTR_MAX > UINT32_MAX
        Check(reinterpret_cast<uintptr_t>(node) > UINT32_MAX);
#endif
        node->localId = static_cast<uint16_t>(localId); node->size = static_cast<uint16_t>(count);
        nodes.push_back(node); return node;
    }
    static void Link(VariableStackBuffer *parent, unsigned int index, VariableStackBuffer *child) {
        char *record = parent->buf + index * VARIABLE_STACK_RECORD_SIZE;
        *record = VAR_STACK; VariableUnion value{}; value.stackValue = child;
        VariableStackBuf_WriteCell(record + 1, value);
    }
    static void Pointer(VariableStackBuffer *parent, unsigned int index, unsigned int id) {
        char *record = parent->buf + index * VARIABLE_STACK_RECORD_SIZE;
        *record = VAR_POINTER; VariableUnion value{}; value.stringValue = id;
        VariableStackBuf_WriteCell(record + 1, value);
    }
    VariableStackBuffer *Chain(unsigned int links) {
        for (unsigned int i = 0; i <= links; ++i) Add(100 + i, i == links ? 0 : 1);
        for (unsigned int i = 0; i < links; ++i) Link(nodes[i], 0, nodes[i + 1]);
        return nodes[0];
    }
};
void Reset(VariableStackBuffer *stack, bool throughObject = false) {
    scrVarPub = {}; scrVarGlob = {}; removedDebuggerRefs = 0;
    scrVarPub.levelId = 1; scrVarPub.animId = 2; scrVarPub.timeArrayId = 3;
    scrVarPub.pauseArrayId = 4; scrVarPub.freeEntList = 6; scrVarPub.gameId = 7;
    for (unsigned int i = 0; i < 200; ++i) scrVarGlob.variableList[i + 1].w.type = VAR_ENTITY | 0x60;
    auto &game = scrVarGlob.variableList[7 + VARIABLELIST_CHILD_BEGIN];
    game.w.type = throughObject ? VAR_POINTER : VAR_STACK;
    game.u.u = {};
    objectWithStack = throughObject ? 30 : 0;
    if (throughObject) {
        game.u.u.stringValue = objectWithStack;
        auto &child = scrVarGlob.variableList[5 + VARIABLELIST_CHILD_BEGIN];
        child.hash.id = 5; child.w.type = VAR_STACK; child.u.u.stackValue = stack;
    } else game.u.u.stackValue = stack;
}
template<class F> void ExpectError(F fn) {
    bool rejected = false;
    try { fn(); } catch (const SaveError &) { rejected = true; }
    Check(rejected);
}
void TestLimitThroughSavePre(bool throughObject) {
    StackGraph graph; auto *root = graph.Chain(SCR_STACK_MAX_NESTING + 1);
    Reset(root, throughObject); ExpectError([] { Scr_SavePre(1); });
    Check(removedDebuggerRefs == 1);
    Check(scrVarPub.saveIdMap[100 + SCR_STACK_MAX_NESTING] != 0);
    Check(scrVarPub.saveIdMap[101 + SCR_STACK_MAX_NESTING] == 0);
    // A fresh valid save after the error has no persistent depth/visited state.
    graph.nodes[SCR_STACK_MAX_NESTING]->size = 0;
    Reset(root, throughObject); Scr_SavePre(1);
    for (unsigned int i = 0; i <= SCR_STACK_MAX_NESTING; ++i) Check(scrVarPub.saveIdMap[100 + i] != 0);
    Check(removedDebuggerRefs == 1);
}
void TestCyclesAndNulls() {
    StackGraph graph; auto *root = graph.Add(100, 1); StackGraph::Link(root, 0, root);
    for (bool throughObject : {false, true}) {
        Reset(root, throughObject); ExpectError([] { Scr_SavePre(1); });
        Check(scrVarPub.savecount < 12);
    }
    Reset(nullptr); ExpectError([] { Scr_SavePre(1); });
    StackGraph::Link(root, 0, nullptr); Reset(root); ExpectError([] { Scr_SavePre(1); });
    VariableUnion value{}; value.stackValue = root;
    Reset(root); ExpectError([&] { AddSaveEntryInternal(VAR_STACK, value); });
}
void TestOrderAndSharedSiblings() {
    StackGraph graph; auto *root = graph.Add(100, 3); auto *child = graph.Add(101, 1);
    StackGraph::Link(root, 0, child); StackGraph::Pointer(root, 1, 103); StackGraph::Link(root, 2, child); StackGraph::Pointer(child, 0, 102);
    Reset(root); Scr_SavePre(1);
    const unsigned int expected[] = {1, 2, 3, 4, 6, 60, 100, 101, 102, 103};
    Check(scrVarPub.savecount == 10);
    for (unsigned int i = 0; i < 10; ++i) { Check(scrVarPub.saveIdMap[expected[i]] == i + 1); Check(scrVarPub.saveIdMapRev[i + 1] == expected[i]); }
    Reset(root); VariableUnion value{}; value.stackValue = root; AddSaveEntryInternal(VAR_STACK, value);
    Check(scrVarPub.savecount == 4 && scrVarPub.saveIdMapRev[4] == 103);
    value.stringValue = 104; AddSaveEntryInternal(VAR_POINTER, value);
    Check(scrVarPub.savecount == 5 && scrVarPub.saveIdMapRev[5] == 104);
}
}
int main() {
    TestLimitThroughSavePre(false); TestLimitThroughSavePre(true);
    TestCyclesAndNulls(); TestOrderAndSharedSiblings();
    std::printf("save registration: %d checks passed\n", checks);
}
