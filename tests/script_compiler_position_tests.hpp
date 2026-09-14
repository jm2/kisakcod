#pragma once
// Recycled production variable records and their actual allocation/evaluation/
// declaration stores. Name lookup and diagnostic source lookup are bounded doubles.
void ExpectThreadPositionError(const char *expectedPosition)
{
    lookedUpSourcePosition = nullptr;
    expectCompileError = true;
    bool rejected = false;
    try { SpecifyThreadPosition(999, 1, 23, VAR_CODEPOS); }
    catch (const ExpectedCompileError &) { rejected = true; }
    expectCompileError = false;
    Check(rejected && lookedUpSourcePosition == expectedPosition);
}
void TestCompilerThreadPositions()
{
    scrVarDebugPub_t *previousDebug = scrVarDebugPub;
    scrVarDebugPub = nullptr;
    SourceBufferInfo source{};
    source.buf = fixtureDiagnostic;
    scrParserPub.sourceBufferLookup = &source;
    scrVarPub.varUsagePos = fixtureDiagnostic;
    for (Vartype_t type : {VAR_CODEPOS, VAR_DEVELOPER_CODEPOS, VAR_INCLUDE_CODEPOS}) {
        auto &entry = scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN + 1];
        entry.u.u.codePosValue = bytecode;
        HighAddress(bytecode);
        Scr_InitVariableRange(VARIABLELIST_CHILD_BEGIN, VARIABLELIST_CHILD_BEGIN + 4);
        fixtureThreadSlot = AllocValue();
        Check(fixtureThreadSlot == 1 && Scr_EvalVariable(1).type == VAR_UNDEFINED);
#if UINTPTR_MAX > UINT32_MAX
        // Free-list initialization and allocation leave the old upper bytes.
        Check((reinterpret_cast<uintptr_t>(entry.u.u.codePosValue) >> 32) == (reinterpret_cast<uintptr_t>(bytecode) >> 32));
#endif
        Check(SpecifyThreadPosition(999, 1, 23, type) == 1);
        VariableValue declared = Scr_EvalVariable(1);
        Check(declared.type == type && declared.u.codePosValue == nullptr);
        ExpectThreadPositionError(nullptr);
        entry.u.u.codePosValue = bytecode;
        ExpectThreadPositionError(bytecode);
#if UINTPTR_MAX > UINT32_MAX
        // A linked native address can have a zero low word. It is never dereferenced.
        const char *lowWordZero = reinterpret_cast<const char *>(uintptr_t{1} << 32);
        entry.u.u.codePosValue = lowWordZero;
        ExpectThreadPositionError(lowWordZero);
#endif
    }
    scrParserPub.sourceBufferLookup = nullptr;
    scrVarDebugPub = previousDebug;
}
void CheckDebugPositionPair(const char *left, const char *right, int expected)
{
    VariableDebugInfo a{};
    VariableDebugInfo b{};
    a.pos = left; b.pos = right;
    Check(CompareThreadDebugIndices(&a, &b) == expected);
    ThreadDebugInfo first{};
    ThreadDebugInfo second{};
    first.posSize = 2; second.posSize = 2;
    first.pos[1] = left; second.pos[1] = right;
    Check(ThreadInfoCompare(&first, &second) == expected);
    first.posSize = 1;
    Check(ThreadInfoCompare(&first, &second) < 0);
    Check(ThreadInfoCompare(&second, &first) > 0);
}
void TestNativeDebugOrdering()
{
    // These opaque positions span >2 GiB and the native address range; no loads.
    const uintptr_t addresses[] = {0, 0x10, 0x80000010u, UINTPTR_MAX - 0x100};
    VariableDebugInfo entries[4]{};
    for (int i = 0; i < 4; ++i) {
        entries[3 - i].pos = reinterpret_cast<const char *>(addresses[i]);
        for (int j = 0; j < 4; ++j)
            CheckDebugPositionPair(reinterpret_cast<const char *>(addresses[i]), reinterpret_cast<const char *>(addresses[j]), (i > j) - (i < j));
    }
    std::sort(std::begin(entries), std::end(entries), [](VariableDebugInfo &a, VariableDebugInfo &b) { return CompareThreadDebugIndices(&a, &b) < 0; });
    for (int i = 0; i < 4; ++i) Check(reinterpret_cast<uintptr_t>(entries[i].pos) == addresses[i]);
}
