// Included inside the reader fixture namespace after its image helpers.
void TestStackSizeBoundaries()
{
    const int maximum = static_cast<int>((UINT16_MAX - (sizeof(VariableStackBuffer) - 1)) / VARIABLE_STACK_RECORD_SIZE);
    int bytes = -1;
    CHECK(VariableStackBuf_TrySize(0, bytes) && bytes == sizeof(VariableStackBuffer) - 1);
    CHECK(VariableStackBuf_TrySize(2048, bytes) && bytes <= UINT16_MAX);
    CHECK(VariableStackBuf_TrySize(maximum, bytes) && bytes <= UINT16_MAX);
    const int previous = bytes;
    CHECK(!VariableStackBuf_TrySize(maximum + 1, bytes) && bytes == previous);
    CHECK(!VariableStackBuf_TrySize(-1, bytes) && bytes == previous);
    CHECK(!VariableStackBuf_TrySize(INT32_MAX, bytes) && bytes == previous);
}

void TestMalformedStackBeforeAllocation()
{
    std::vector<uint8_t> image;
    AppendStackHead(image, UINT16_MAX, 1, 1, 1);
    g_readstackNonfatalAsserts = true;
    const auto threads = scrVarPub.numScriptThreads;
    try {
        RunReader(image);
        CHECK(false);
    } catch (const ReadstackComError &) {
        CHECK(g_allocations.empty());
        CHECK(scrVarPub.numScriptThreads == threads);
    }
    g_readstackNonfatalAsserts = false;
}

void TestEmptyCellInitialization()
{
    std::vector<uint8_t> image;
    AppendStackHead(image, 5, 1, 1, 1);
    AppendCodeposRecord(image, 0x91);
    image.push_back(VAR_UNDEFINED << 3);
    AppendFloatRecord(image, 1.5f);
    image.push_back(VAR_PRECODEPOS << 3);
    AppendIntegerRecord(image, 0xdeadbeefu);
    VariableStackBuffer *stack = RunReader(image);
    for (size_t index : {size_t{1}, size_t{3}}) {
        for (size_t byte = 0; byte < sizeof(VariableUnion); ++byte)
            CHECK(RecordPayload(stack, index)[byte] == 0);
    }
    for (size_t index : {size_t{2}, size_t{4}}) {
        for (size_t byte = sizeof(uint32_t); byte < sizeof(VariableUnion); ++byte)
            CHECK(RecordPayload(stack, index)[byte] == 0);
    }
}
