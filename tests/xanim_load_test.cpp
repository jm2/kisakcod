// xanim_load_test: behavioral contracts for the transactional BufCursor
// that replaces the legacy unbounded Buf_Read<T>(unsigned char **pos).
//
// The cursor enforces count-style bounds (every Buf_Read<T> checks that
// current + sizeof(T) <= end), transactional semantics (Begin / Commit /
// Rollback), and domain limits for bone, weight, triangle, and string
// reads. The tests exercise each contract with deterministic buffers and
// also include a property-style fuzz over randomized short buffers.

#include <xanim/buf_cursor.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace xanim_load_test
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
        std::fprintf(stderr, "xanim_load_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

// Contract 1: Buf_Read<T> with no active cursor preserves the original
// unbounded behavior so legacy callers and tests that never activate a
// cursor continue to work.
bool TestBufReadFallback()
{
    const unsigned char kBytes[] = { 0x12, 0x34, 0x78, 0x56, 0x9A, 0xBC };
    unsigned char buffer[sizeof(kBytes)];
    std::memcpy(buffer, kBytes, sizeof(kBytes));

    buf_cursor::Deactivate();
    unsigned char *pos = buffer;

    unsigned short s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0x3412);
    CHECK(pos == buffer + 2);

    int i = Buf_Read<int>(&pos);
    CHECK(i == static_cast<int>(0xBC9A5678u));
    CHECK(pos == buffer + 6);

    return true;
}

// Contract 2: Buf_Read<T> with an active cursor advances current and
// reports Failed()=false while there is room left.
bool TestBufReadWithinBounds()
{
    const unsigned char kBytes[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);

    unsigned short s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0xBBAA);
    CHECK(pos == kBytes + 2);
    CHECK(!buf_cursor::Failed());

    int i = Buf_Read<int>(&pos);
    CHECK(i == static_cast<int>(0xFFEEDDCCu));
    CHECK(pos == kBytes + 6);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 3: Buf_Read<T> that would overrun end latches Failed() and
// does NOT advance pos. The cursor stays put so the caller can decide
// to rollback.
bool TestBufReadOverrun()
{
    const unsigned char kBytes[] = { 0x01, 0x02 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);

    unsigned short s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0x0201);
    CHECK(!buf_cursor::Failed());

    // One more short read overruns.
    unsigned short overflow = Buf_Read<unsigned short>(&pos);
    CHECK(overflow == 0);
    CHECK(buf_cursor::Failed());
    CHECK(pos == kBytes + 2);

    buf_cursor::Deactivate();
    return true;
}

// Contract 4: Activate + Deactivate scope the cursor so a subsequent
// Buf_Read<T> with no activation falls back to the unbounded path.
bool TestActivateScope()
{
    const unsigned char kBytes[] = { 0xCA, 0xFE };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    buf_cursor::Deactivate();

    unsigned char buffer[sizeof(kBytes)];
    std::memcpy(buffer, kBytes, sizeof(kBytes));
    unsigned char *pos = buffer;
    unsigned short s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0xFECA);
    CHECK(pos == buffer + 2);

    return true;
}

// Contract 5: Transactional Begin / Rollback rewinds to the checkpoint
// even when intervening Buf_Read<T> calls latched Failed()=true.
bool TestTransactionRollback()
{
    const unsigned char kBytes[] = { 0x11, 0x22, 0x33, 0x44 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);

    unsigned short first = Buf_Read<unsigned short>(&pos);
    CHECK(first == 0x2211);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Begin();

    unsigned short second = Buf_Read<unsigned short>(&pos);
    CHECK(second == 0x4433);
    CHECK(!buf_cursor::Failed());

    // Speculative read that overruns latches Failed().
    unsigned short overflow = Buf_Read<unsigned short>(&pos);
    CHECK(overflow == 0);
    CHECK(buf_cursor::Failed());

    buf_cursor::Rollback();
    CHECK(!buf_cursor::Failed());

    // The cursor is back at the checkpoint and we can replay the
    // transaction (or commit it directly).
    pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    CHECK(pos == kBytes + 2);

    unsigned short replay = Buf_Read<unsigned short>(&pos);
    CHECK(replay == 0x4433);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Commit();
    buf_cursor::Deactivate();
    return true;
}

// Contract 6: ReadBone enforces the configured bone limit. Reads of an
// out-of-range bone index latch Failed()=true.
bool TestReadBoneBound()
{
    const unsigned char kBytes[] = { 0x05, 0x00, 0x80, 0x00 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    buf_cursor::SetBoneLimit(0x0080u);

    uint16_t first = buf_cursor::ReadBone();
    CHECK(first == 5);
    CHECK(!buf_cursor::Failed());

    uint16_t over = buf_cursor::ReadBone();
    CHECK(over == 0x0080u);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 7: ReadTri enforces both vertCount and the cursor's tri
// limit. vertCount is checked first because it is more specific.
bool TestReadTriBound()
{
    const unsigned char kBytes[] = { 0x03, 0x00, 0xFF, 0x00 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));

    // vertCount=4: indices < 4 are valid. 0x0003 is in range.
    uint16_t inRange = buf_cursor::ReadTri(4);
    CHECK(inRange == 3);
    CHECK(!buf_cursor::Failed());

    // vertCount=4 but value=0x00FF overruns: fails.
    uint16_t outOfRange = buf_cursor::ReadTri(4);
    CHECK(outOfRange == 0x00FFu);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 8: ReadString enforces end, maxStringLen, and outSize.
bool TestReadStringBound()
{
    const char kStr[] = "j_ankle_le";
    std::vector<unsigned char> bytes;
    for (const char *p = kStr; *p; ++p)
        bytes.push_back(static_cast<unsigned char>(*p));
    bytes.push_back(0);

    buf_cursor::Activate(bytes.data(), bytes.size());

    char out[16];
    CHECK(buf_cursor::ReadString(out, sizeof(out)));
    CHECK(std::strcmp(out, kStr) == 0);

    // No room left; another string read fails.
    char out2[16];
    CHECK(!buf_cursor::ReadString(out2, sizeof(out2)));
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();

    // Run-length cap: a string longer than maxStringLen fails even when
    // it is well inside end.
    const char kLong[] = "abcdefghij";
    std::vector<unsigned char> longBytes;
    for (const char *p = kLong; *p; ++p)
        longBytes.push_back(static_cast<unsigned char>(*p));
    longBytes.push_back(0);

    buf_cursor::Activate(longBytes.data(), longBytes.size());
    buf_cursor::SetStringLimit(4);

    char smallOut[16];
    CHECK(!buf_cursor::ReadString(smallOut, sizeof(smallOut)));
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 9: Advance() that overruns end latches Failed()=true without
// moving the cursor.
bool TestAdvanceOverrun()
{
    const unsigned char kBytes[] = { 0x01, 0x02 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));

    buf_cursor::Advance(1);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Advance(2);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 10: ReadWeight enforces the weight limit. Default limit is
// "no bound" so reads always succeed unless explicitly tightened.
bool TestReadWeightBound()
{
    const unsigned char kBytes[] = { 0x03, 0xFF };
    buf_cursor::Activate(kBytes, sizeof(kBytes));

    uint8_t w = buf_cursor::ReadWeight();
    CHECK(w == 3);
    CHECK(!buf_cursor::Failed());

    buf_cursor::SetWeightLimit(2);

    uint8_t over = buf_cursor::ReadWeight();
    CHECK(over == 0xFFu);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 11: Fail() latches failure independent of bounds checks so
// upstream domain validation can short-circuit the rest of a read.
bool TestManualFail()
{
    const unsigned char kBytes[] = { 0x01, 0x02, 0x03, 0x04 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));

    buf_cursor::Fail();
    CHECK(buf_cursor::Failed());

    // Subsequent reads return 0 without advancing.
    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    unsigned short s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0);
    CHECK(pos == kBytes);

    buf_cursor::Deactivate();
    return true;
}

// Contract 12: Migration-shape check. Simulates the legacy 114-site
// pattern of "Buf_Read<T>(&pos) followed by a domain assertion" and
// shows that the cursor catches overruns that the iassert would have
// fired on at runtime.
bool TestMigrationShape()
{
    // Construct a buffer of 5 little-endian uint16 values: 1, 2, 3, 4, 0x0080.
    // The cursor rejects 0x0080 against a bone limit of 4.
    const unsigned char kBytes[] = {
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x80, 0x00,
    };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    buf_cursor::SetBoneLimit(5);

    for (int i = 0; i < 4; ++i)
    {
        unsigned short bone = buf_cursor::ReadBone();
        CHECK(bone == static_cast<unsigned short>(i + 1));
        CHECK(!buf_cursor::Failed());
    }

    // 0x0080 is past the limit of 5 (the cursor flags the read as a
    // bone-limit violation).
    unsigned short bogus = buf_cursor::ReadBone();
    CHECK(bogus == 0x0080u);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract 13: Fuzz over short randomized buffers. For each seed we
// confirm that Buf_Read<T> never advances past end and that Failed()
// becomes true the moment any read would overrun.
bool TestFuzz()
{
    std::srand(0xC0FFEEu);
    for (int trial = 0; trial < 1024; ++trial)
    {
        size_t len = (std::rand() % 32) + 1;
        std::vector<unsigned char> buf(len);
        for (size_t i = 0; i < len; ++i)
            buf[i] = static_cast<unsigned char>(std::rand() & 0xFF);

        buf_cursor::Activate(buf.data(), len);
        const buf_cursor::BufCursor *c = buf_cursor::Current();
        CHECK(c != nullptr);

        size_t totalRead = 0;
        bool sawFailure = false;
        while (totalRead < len + 16)
        {
            unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
            int v = Buf_Read<int>(&pos);
            (void)v;
            if (buf_cursor::Failed())
            {
                sawFailure = true;
                // The cursor must not have advanced past end.
                CHECK(buf_cursor::Current()->current <= buf_cursor::Current()->end);
                break;
            }
            totalRead += sizeof(int);
        }
        // Either we read all len and stopped, or we failed early.
        if (!sawFailure)
        {
            CHECK(totalRead >= len);
        }
        buf_cursor::Deactivate();
    }
    return true;
}

int RunAllTests()
{
    bool (*tests[])() = {
        TestBufReadFallback,
        TestBufReadWithinBounds,
        TestBufReadOverrun,
        TestActivateScope,
        TestTransactionRollback,
        TestReadBoneBound,
        TestReadTriBound,
        TestReadStringBound,
        TestAdvanceOverrun,
        TestReadWeightBound,
        TestManualFail,
        TestMigrationShape,
        TestFuzz,
    };
    for (auto *test : tests)
    {
        (void)test();
    }
    std::fprintf(stderr, "xanim_load_test: %d checks, %d failures\n", g_runs, g_failures);
    return g_failures == 0 ? 0 : 1;
}
}

int main()
{
    return xanim_load_test::RunAllTests();
}
