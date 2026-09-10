#pragma once
// Behavior tests for actual save-flow bodies, with real native records.
void TestClassArraySaveLoad()
{
    for (bool debug : {true, false}) {
        scrVarDebugPub = debug ? &debugState : nullptr;
        const char *names[CLASS_NUM_COUNT];
        char chars[CLASS_NUM_COUNT];
        for (unsigned int i = 0; i < CLASS_NUM_COUNT; ++i) {
            names[i] = g_classMap[i].name;
            chars[i] = g_classMap[i].charId;
            HighAddress(names[i]);
            g_classMap[i].entArrayId = static_cast<uint16_t>((i + 1) * 10);
            debugState.extRefCount[(i + 1) * 10] = 1;
            debugState.extRefCount[61 + i] = 0;
        }
        writtenSaveIds.clear(); addedSaveIds.clear(); removedSaveIds.clear();
        Scr_WriteClassArrays(nullptr);
        Scr_AddSaveClassArrays();
        Check(writtenSaveIds == std::vector<unsigned int>({10, 20, 30, 40}));
        Check(addedSaveIds == writtenSaveIds);
        nextClassTag = 61;
        Scr_LoadClassArrays(nullptr);
        Check(removedSaveIds == writtenSaveIds && nextClassTag == 65);
        for (unsigned int i = 0; i < CLASS_NUM_COUNT; ++i) {
            Check(g_classMap[i].entArrayId == 61 + i);
            Check(g_classMap[i].name == names[i] && g_classMap[i].charId == chars[i]);
            Check(g_classMap[i].id == 100 + i);
            Check(debugState.extRefCount[(i + 1) * 10] == (debug ? 0 : 1));
            Check(debugState.extRefCount[61 + i] == (debug ? 1 : 0));
        }
    }
    scrVarDebugPub = &debugState;
}

void TestSaveShutdownEntries()
{
    std::memset(&scrVarGlob, 0, sizeof(scrVarGlob));
    std::fill_n(scrVarPub.saveIdMap, 0x8000, 0);
    for (unsigned int i : {2u, 3u, 0x8000u}) {
        scrVarGlob.variableList[i].w.type = VAR_ENTITY | 0x20;
        debugState.varUsage[i] = i == 3 ? "second object" : "last object";
    }
    scrVarGlob.variableList[4].w.type = VAR_ARRAY | 0x20;
    debugState.varUsage[4] = "array is excluded";
    scrVarPub.saveIdMap[1] = 1; // first object is saved
    leakPositions.clear(); debuggerRestoreCount = 0;
    Scr_SaveShutdown(false);
    Check(leakPositions == std::vector<const char *>({debugState.varUsage[3], debugState.varUsage[0x8000]}));
    Check(debuggerRestoreCount == 1);
    scrVarPub.saveIdMap[2] = 2;
    scrVarPub.saveIdMap[0x7fff] = 3;
    leakPositions.clear();
    Scr_SaveShutdown(true);
    Check(leakPositions.empty() && debuggerRestoreCount == 2);
    scrVarDebugPub = nullptr;
    Scr_SaveShutdown(true);
    Check(leakPositions.empty() && debuggerRestoreCount == 3);
    scrVarDebugPub = &debugState;
}

void TestDebugExpressionSaveRefs()
{
    struct Node { debugger_sval_s header; sval_u values[2]; } nodes[3]{};
    HighAddress(nodes);
    nodes[0].header.next = &nodes[1].header;
    nodes[1].header.next = &nodes[2].header;
    nodes[0].values[0].type = ENUM_thread_object;
    nodes[0].values[1].intValue = 17;
    nodes[1].values[0].type = ENUM_integer;
    nodes[2].values[0].type = ENUM_thread_object;
    nodes[2].values[1].intValue = 23;
    uint16_t refs[32]{};
    Scr_AddDebugExprRefCount(refs, &nodes[0].header);
    Check(refs[17] == 1 && refs[23] == 1 && refs[0] == 0);
    Scr_AddDebugExprRefCount(refs, nullptr);
    Check(refs[17] == 1 && refs[23] == 1);
}
