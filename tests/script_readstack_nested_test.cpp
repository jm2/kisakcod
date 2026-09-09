// script_readstack_nested_test: production-path behavioral regression for
// the M4 (ki-n1et) nested VAR_STACK save reader (Scr_ReadStack and its
// helpers in src/script/scr_readwrite.cpp).
//
// The verbatim production reader slice -- bracketed by the
// "ki-n1et behavioral-test slice begin/end" anchors inside
// scr_readwrite.cpp -- is extracted at configure time into
// script_readstack_slice.inc and compiled INTO this TU against the real
// production MemoryFile reader (kisakcod-memfile-test-subject) plus small
// test doubles for the engine services the slice calls (codepos/id/string
// decoders, MT_Alloc, error boundary).
//
// Reproduces the operator P1 regression that motivated the fix: the
// iterative rewrite reused one `VariableValue` across nested frames, so a
// resumed parent record inherited the CHILD's last entry type and stored a
// live stack pointer tagged as VAR_INTEGER. Every case below asserts the
// parent nested record carries type VAR_STACK (0xA) and the exact child
// buffer pointer the allocator produced.
//
//   1. Scalar-ended child (parent [int][stack][float]).
//   2. Empty child (child size == 0).
//   3. Siblings (two nested records in one parent).
//   4. Multiple levels (three-deep nesting).
//   5. The SCR_READSTACK_MAX_NESTING bound fails loudly (Com_Error).
//   6. Baseline: a nesting-free stack decodes unchanged.

#include <universal/memfile.h>

#include <script/scr_variable.h>

#include <csetjmp>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Check harness (named namespace so the CHECK macro can qualify it).
// ---------------------------------------------------------------------------

namespace script_readstack_nested_test
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
        std::fprintf(stderr, "script_readstack_nested_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace script_readstack_nested_test

#define CHECK(expr) \
    script_readstack_nested_test::Evaluate((expr), #expr, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Engine service doubles. Global-scope DEFINITIONS of the engine services
// the memfile subject and the reader slice reference (the qcommon headers
// already declare most of them; see tests/memfile_tests.cpp for the same
// pattern). The reader must hit its nesting bound through the loud
// Com_Error path, so that double longjmps into the harness.
// ---------------------------------------------------------------------------

jmp_buf g_readstackComErrorJump;
int g_readstackUnexpectedReports = 0;

char *QDECL va(const char *format, ...)
{
    static char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

void QDECL Com_Error(const errorParm_t code, const char *format, ...)
{
    (void)code;
    (void)format;
    std::longjmp(g_readstackComErrorJump, 1);
}

void QDECL Com_Printf(const int channel, const char *format, ...)
{
    (void)channel;
    (void)format;
    ++g_readstackUnexpectedReports;
}

void MyAssertHandler(
    const char *filename,
    int line,
    int type,
    const char *format,
    ...)
{
    (void)type;
    (void)format;
    std::fprintf(
        stderr,
        "script_readstack_nested_test: production assert fired at %s:%d\n",
        filename,
        line);
    std::abort();
}

bool __cdecl Sys_IsMainThread()
{
    return true;
}

bool __cdecl Sys_IsRenderThread()
{
    return false;
}

bool __cdecl Sys_IsDatabaseThread()
{
    return false;
}

// Reader decoder doubles: deterministic, one byte per id/codepos token.
// The test images below encode every id/codepos as exactly one byte.

unsigned int __cdecl Scr_ReadId(MemoryFile *memFile, unsigned int tag)
{
    (void)tag;
    uint8_t byte = 0;
    MemFile_ReadData(memFile, 1, &byte);
    return byte;
}

const char *__cdecl Scr_ReadCodepos(MemoryFile *memFile)
{
    uint8_t byte = 0;
    MemFile_ReadData(memFile, 1, &byte);
    return reinterpret_cast<const char *>(static_cast<uintptr_t>(0x4000u + byte));
}

uint16_t __cdecl Scr_ReadString(MemoryFile *memFile)
{
    (void)memFile;
    // Not exercised by any test image: strings decode through the real
    // production path, which this TU does not link.
    std::fputs("script_readstack_nested_test: Scr_ReadString double entered\n", stderr);
    std::abort();
}

const float *__cdecl Scr_ReadVec3(MemoryFile *memFile)
{
    (void)memFile;
    std::fputs("script_readstack_nested_test: Scr_ReadVec3 double entered\n", stderr);
    std::abort();
}

// Allocation double: records every block so a nested record's stored
// pointer can be asserted against the allocation it must identify.
std::vector<VariableStackBuffer *> g_allocations;

void *__cdecl MT_Alloc(int numBytes, int type)
{
    (void)type;
    void *block = std::malloc(static_cast<size_t>(numBytes));
    g_allocations.push_back(static_cast<VariableStackBuffer *>(block));
    return block;
}

// The slice only touches scrVarPub.numScriptThreads; a minimal definition
// satisfies it without dragging in the full scr_main.h surface.
struct scrVarPub_t
{
    uint32_t numScriptThreads;
};

scrVarPub_t scrVarPub{};

// ---------------------------------------------------------------------------
// Verbatim production slice (extracted at configure time).
// ---------------------------------------------------------------------------

#include <script_readstack_slice.inc>

// ---------------------------------------------------------------------------
// Test harness: builds packed retail save images and drives the production
// reader through the real MemoryFile. Nested child bodies are laid out
// DEPTH-FIRST -- the child stack bytes sit inline immediately after their
// VAR_STACK entry byte (the order WriteStack/DoSaveEntryInternal emit), and
// the parent's remaining records resume after the child body.
// ---------------------------------------------------------------------------

namespace
{
constexpr uint8_t kTypePointer = 0x01;     // (byte & 7) != 0 -> VAR_POINTER
constexpr uint8_t kTypeFloat = 0x28;       // 5 << 3
constexpr uint8_t kTypeInteger = 0x30;     // 6 << 3
constexpr uint8_t kTypeCodepos = 0x38;     // 7 << 3
constexpr uint8_t kTypeStack = 0x50;       // 10 << 3 (VAR_STACK)

void AppendU16(std::vector<uint8_t> &image, uint16_t value)
{
    image.push_back(static_cast<uint8_t>(value & 0xFFu));
    image.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void AppendU32(std::vector<uint8_t> &image, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        image.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
}

void AppendFloat(std::vector<uint8_t> &image, float value)
{
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float bit width");
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(image, bits);
}

// Stack head: [size:2 LE][pos byte][id tag byte][id byte][saveStamp byte].
void AppendStackHead(
    std::vector<uint8_t> &image,
    uint16_t size,
    uint8_t codePosToken,
    uint8_t localId,
    uint8_t saveStamp)
{
    AppendU16(image, size);
    image.push_back(codePosToken);
    image.push_back(0x00);  // id tag byte (consumed before Scr_ReadId)
    image.push_back(localId);
    image.push_back(saveStamp);
}

void AppendIntegerRecord(std::vector<uint8_t> &image, uint32_t payload)
{
    image.push_back(kTypeInteger);
    AppendU32(image, payload);
}

void AppendFloatRecord(std::vector<uint8_t> &image, float value)
{
    image.push_back(kTypeFloat);
    AppendFloat(image, value);
}

void AppendCodeposRecord(std::vector<uint8_t> &image, uint8_t token)
{
    image.push_back(kTypeCodepos);
    image.push_back(token);
}

// Seals a raw byte stream into a memfile archive via the production
// writer (the reader requires the segment table the writer emits).
std::vector<uint8_t> EncodeImage(const std::vector<uint8_t> &raw)
{
    std::vector<uint8_t> archive(raw.size() * 3 + 4096);
    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archive.size()),
        archive.data(),
        false,
        false);
    MemFile_WriteData(
        &writer,
        static_cast<int>(raw.size()),
        const_cast<uint8_t *>(raw.data()));
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    archive.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    return archive;
}

// Runs the verbatim production reader over an image. The decoded stack is
// MT_Alloc'd heap (not the archive), so the reader can be closed before
// the assertions run -- required to release the memfile global stream
// before the next case seals its own image.
VariableStackBuffer *RunReader(const std::vector<uint8_t> &image)
{
    std::vector<uint8_t> archive = EncodeImage(image);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    VariableStackBuffer *stack = Scr_ReadStack(&reader);
    if (!reader.memoryOverflow && reader.segmentIndex >= 0)
        MemFile_MoveToSegment(&reader, -1);
    MemFile_Shutdown(&reader);
    return stack;
}

const uint8_t *RecordTypeByte(const VariableStackBuffer *stack, size_t index)
{
    return reinterpret_cast<const uint8_t *>(stack->buf + index * VARIABLE_STACK_RECORD_SIZE);
}

// Payload bytes of a record (the widened cell behind the type byte).
const uint8_t *RecordPayload(const VariableStackBuffer *stack, size_t index)
{
    return reinterpret_cast<const uint8_t *>(stack->buf + index * VARIABLE_STACK_RECORD_SIZE + 1);
}

bool PayloadStartsWith(const VariableStackBuffer *stack, size_t index, const void *bytes, size_t count)
{
    return std::memcmp(RecordPayload(stack, index), bytes, count) == 0;
}

bool PayloadHoldsPointer(const VariableStackBuffer *stack, size_t index, const void *pointer)
{
    return std::memcmp(RecordPayload(stack, index), &pointer, sizeof(void *)) == 0;
}

void CheckStackHeader(const VariableStackBuffer *stack, uint16_t size, uint8_t localId, uint8_t saveStamp)
{
    CHECK(stack != nullptr);
    if (!stack)
        return;
    CHECK(stack->size == size);
    CHECK(stack->localId == localId);
    CHECK(stack->saveStamp == saveStamp);
    const size_t expectedBufLen =
        VARIABLE_STACK_RECORD_SIZE * size + (sizeof(VariableStackBuffer) - 1);
    CHECK(stack->bufLen == expectedBufLen);
}

// Case 1: scalar-ended child. Parent [int][stack][float], child [float].
void TestScalarEndedChild()
{
    std::vector<uint8_t> child;
    AppendStackHead(child, 1, 0x11, 0x42, 0x33);
    AppendFloatRecord(child, 1.5f);

    std::vector<uint8_t> parent;
    AppendStackHead(parent, 3, 0x12, 0x7F, 0x44);
    AppendIntegerRecord(parent, 0xAABBCCDDu);
    parent.push_back(kTypeStack);
    // Depth-first layout (matches WriteStack/DoSaveEntryInternal): the
    // child body is inline directly after its entry byte; the parent's
    // trailing record resumes after it.
    parent.insert(parent.end(), child.begin(), child.end());
    AppendFloatRecord(parent, 2.25f);

    VariableStackBuffer *stack = RunReader(parent);
    CheckStackHeader(stack, 3, 0x7F, 0x44);

    // Record 0: integer decoded into the widened cell (low dword exact).
    CHECK(*RecordTypeByte(stack, 0) == 6);
    CHECK(PayloadStartsWith(stack, 0, "\xDD\xCC\xBB\xAA", 4));

    // THE regression: the nested record must be tagged VAR_STACK and hold
    // the child buffer pointer -- not the child's last entry type.
    CHECK(*RecordTypeByte(stack, 1) == 10);
    CHECK(g_allocations.size() == 2);
    VariableStackBuffer *childBuf = g_allocations[1];  // [0] is the parent
    CHECK(PayloadHoldsPointer(stack, 1, childBuf));

    // Record 2: parent's trailing float survived the nested detour.
    CHECK(*RecordTypeByte(stack, 2) == 5);
    const float parentFloat = 2.25f;
    CHECK(PayloadStartsWith(stack, 2, &parentFloat, 4));

    // Child buffer contents: retail bytes rebuilt at the runtime stride.
    CheckStackHeader(childBuf, 1, 0x42, 0x33);
    CHECK(*RecordTypeByte(childBuf, 0) == 5);
    const float childFloat = 1.5f;
    CHECK(PayloadStartsWith(childBuf, 0, &childFloat, 4));
}

// Case 2: empty child (size == 0).
void TestEmptyChild()
{
    std::vector<uint8_t> child;
    AppendStackHead(child, 0, 0x21, 0x55, 0x66);

    std::vector<uint8_t> parent;
    AppendStackHead(parent, 3, 0x22, 0x56, 0x67);
    AppendIntegerRecord(parent, 7u);
    parent.push_back(kTypeStack);
    parent.insert(parent.end(), child.begin(), child.end());
    AppendCodeposRecord(parent, 0x77);

    VariableStackBuffer *stack = RunReader(parent);
    CheckStackHeader(stack, 3, 0x56, 0x67);

    CHECK(*RecordTypeByte(stack, 1) == 10);
    CHECK(g_allocations.size() == 2);
    VariableStackBuffer *childBuf = g_allocations[1];
    CHECK(PayloadHoldsPointer(stack, 1, childBuf));
    CHECK(childBuf->size == 0);

    CHECK(*RecordTypeByte(stack, 2) == 7);
    const char *expectedPos =
        reinterpret_cast<const char *>(static_cast<uintptr_t>(0x4000u + 0x77));
    CHECK(PayloadHoldsPointer(stack, 2, expectedPos));
}

// Case 3: siblings -- two nested records in one parent.
void TestSiblings()
{
    std::vector<uint8_t> childA;
    AppendStackHead(childA, 1, 0x31, 0x61, 0x62);
    AppendIntegerRecord(childA, 99u);

    std::vector<uint8_t> childB;
    AppendStackHead(childB, 2, 0x32, 0x63, 0x64);
    AppendCodeposRecord(childB, 0x81);
    AppendFloatRecord(childB, 0.5f);

    std::vector<uint8_t> parent;
    AppendStackHead(parent, 3, 0x33, 0x65, 0x66);
    parent.push_back(kTypeStack);
    parent.insert(parent.end(), childA.begin(), childA.end());
    parent.push_back(kTypeStack);
    parent.insert(parent.end(), childB.begin(), childB.end());
    AppendIntegerRecord(parent, 5u);

    VariableStackBuffer *stack = RunReader(parent);
    CheckStackHeader(stack, 3, 0x65, 0x66);

    CHECK(g_allocations.size() == 3);
    CHECK(*RecordTypeByte(stack, 0) == 10);
    CHECK(PayloadHoldsPointer(stack, 0, g_allocations[1]));
    CHECK(*RecordTypeByte(stack, 1) == 10);
    CHECK(PayloadHoldsPointer(stack, 1, g_allocations[2]));
    CHECK(*RecordTypeByte(stack, 2) == 6);

    CHECK(g_allocations[1]->size == 1);
    CHECK(g_allocations[2]->size == 2);
    CHECK(*RecordTypeByte(g_allocations[2], 0) == 7);
    CHECK(*RecordTypeByte(g_allocations[2], 1) == 5);
}

// Case 4: multiple levels -- three-deep nesting.
void TestMultipleLevels()
{
    std::vector<uint8_t> grandchild;
    AppendStackHead(grandchild, 1, 0x41, 0x71, 0x72);
    AppendCodeposRecord(grandchild, 0x91);

    std::vector<uint8_t> child;
    AppendStackHead(child, 2, 0x42, 0x73, 0x74);
    child.push_back(kTypeStack);
    child.insert(child.end(), grandchild.begin(), grandchild.end());
    AppendFloatRecord(child, 3.5f);

    std::vector<uint8_t> parent;
    AppendStackHead(parent, 2, 0x43, 0x75, 0x76);
    parent.push_back(kTypeStack);
    parent.insert(parent.end(), child.begin(), child.end());
    AppendIntegerRecord(parent, 1234u);

    VariableStackBuffer *stack = RunReader(parent);
    CheckStackHeader(stack, 2, 0x75, 0x76);

    CHECK(g_allocations.size() == 3);
    VariableStackBuffer *childBuf = g_allocations[1];  // [0] is the parent
    VariableStackBuffer *grandchildBuf = g_allocations[2];
    CHECK(*RecordTypeByte(stack, 0) == 10);
    CHECK(PayloadHoldsPointer(stack, 0, childBuf));
    CHECK(*RecordTypeByte(stack, 1) == 6);

    // The middle frame's nested record also carries VAR_STACK.
    CHECK(childBuf->size == 2);
    CHECK(*RecordTypeByte(childBuf, 0) == 10);
    CHECK(PayloadHoldsPointer(childBuf, 0, grandchildBuf));
    CHECK(*RecordTypeByte(childBuf, 1) == 5);
    const float childFloat = 3.5f;
    CHECK(PayloadStartsWith(childBuf, 1, &childFloat, 4));

    CHECK(grandchildBuf->size == 1);
    CHECK(*RecordTypeByte(grandchildBuf, 0) == 7);
}

// Case 5: the nesting bound fails loudly instead of recursing.
void TestNestingLimit()
{
    // SCR_READSTACK_MAX_NESTING + 1 suspended frames must hit Com_Error.
    std::vector<uint8_t> image;
    for (int i = 0; i < 17; ++i)
    {
        AppendStackHead(image, 1, 0x51, 0x80, 0x81);
        image.push_back(kTypeStack);
    }
    // The innermost stack never gets consumed; the error fires first.
    AppendStackHead(image, 1, 0x52, 0x82, 0x83);
    AppendIntegerRecord(image, 1u);

    if (setjmp(g_readstackComErrorJump) == 0)
    {
        RunReader(image);
        // Reaching here means the bound did not fire.
        CHECK(false);
    }
    else
    {
        CHECK(true);
    }
}

// Case 6: baseline -- a nesting-free stack decodes unchanged.
void TestNoNesting()
{
    std::vector<uint8_t> image;
    AppendStackHead(image, 2, 0x61, 0x90, 0x91);
    AppendIntegerRecord(image, 0xDEADBEEFu);
    AppendFloatRecord(image, -2.75f);

    VariableStackBuffer *stack = RunReader(image);
    CheckStackHeader(stack, 2, 0x90, 0x91);
    CHECK(*RecordTypeByte(stack, 0) == 6);
    CHECK(PayloadStartsWith(stack, 0, "\xEF\xBE\xAD\xDE", 4));
    CHECK(*RecordTypeByte(stack, 1) == 5);
    const float value = -2.75f;
    CHECK(PayloadStartsWith(stack, 1, &value, 4));
    CHECK(g_allocations.size() == 1);
}
}  // namespace

int main()
{
    TestNoNesting();
    g_allocations.clear();
    TestScalarEndedChild();
    g_allocations.clear();
    TestEmptyChild();
    g_allocations.clear();
    TestSiblings();
    g_allocations.clear();
    TestMultipleLevels();
    g_allocations.clear();
    TestNestingLimit();

    if (script_readstack_nested_test::g_failures)
    {
        std::fprintf(
            stderr,
            "script_readstack_nested_test: %d/%d checks failed\n",
            script_readstack_nested_test::g_failures,
            script_readstack_nested_test::g_runs);
        return 1;
    }
    std::printf(
        "script_readstack_nested_test: %d checks passed\n",
        script_readstack_nested_test::g_runs);
    return 0;
}
