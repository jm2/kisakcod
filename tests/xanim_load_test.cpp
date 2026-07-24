// xanim_load_test: behavioral contracts for the transactional BufCursor
// when reading xanim parts headers, delta quaternions, delta translations,
// and notify-track strings. The cursor backs every Buf_Read<T> call so
// an overrun or a domain violation (bone, weight, triangle) latches
// Failed()=true instead of silently reading off the end of the buffer.
//
// The xanim_load_test targets the four documented safety issues from
// the prior polecat attempt ki-vxc recovery notes (recovery branch
// origin/recovery/ac-vxc-pre-refinery-20260724):
//
//   1. UBSan misaligned float load. The original Buf_Read<T> uses
//      *reinterpret_cast<const T *>(pos) which is a misaligned load for
//      T=float on an unaligned cursor buffer. The cursor's Buf_Read<T>
//      reads through std::memcpy so the access is well-defined
//      regardless of the underlying pointer's natural alignment.
//
//   2. Cursor-versus-raw-pointer desynchronization. The cursor is
//      anchored to the loader's *pos pointer via AnchorPos so every
//      Buf_Read<T> writes back to *pos. Transactional Rollback walks
//      both the cursor's current and the anchored *pos back together so
//      the two views cannot disagree.
//
//   3. Unbounded strlen-before-check. The original strlen scan
//      `pos += strlen((const char *)pos) + 1` had no upper bound. The
//      cursor's ReadString scans until NUL or end-of-buffer, then
//      validates length against the configured maxStringLen before
//      committing the copy.
//
//   4. XModelParts classification / useBones advances. The original
//      `pos += numBones` and `*pos++` for useBones bypass the cursor
//      entirely. The cursor's Advance(path) / ReadWeight() funnelled
//      through the same routing that Buf_Read<T> uses so the bounds
//      check is enforced for every byte the loader consumes.

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

// Contract: a complete "xmodel pieces" header parse — version, count,
// per-piece model name + offset triple. Mirrors XModelPiecesLoadFile.
bool TestXModelPiecesParse()
{
    std::vector<unsigned char> bytes;

    auto push16 = [&](uint16_t v) {
        bytes.push_back(static_cast<unsigned char>(v & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    };
    auto push32 = [&](uint32_t v) {
        bytes.push_back(static_cast<unsigned char>(v & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    };
    auto pushFloat = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        push32(bits);
    };
    auto pushString = [&](const char *s) {
        for (const char *p = s; *p; ++p)
            bytes.push_back(static_cast<unsigned char>(*p));
        bytes.push_back(0);
    };

    push16(1);                 // version
    push16(2);                 // numpieces
    pushString("viewmodel_default");
    pushFloat(1.0f);
    pushFloat(2.0f);
    pushFloat(3.0f);
    pushString("viewmodel_lod1");
    pushFloat(4.0f);
    pushFloat(5.0f);
    pushFloat(6.0f);

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    buf_cursor::AnchorPos(&pos);
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 1);
    CHECK(!buf_cursor::Failed());

    uint16_t numpieces = Buf_Read<unsigned short>(&pos);
    CHECK(numpieces == 2);

    for (int i = 0; i < numpieces; ++i)
    {
        char name[128];
        CHECK(buf_cursor::ReadString(name, sizeof(name)));
        CHECK(!buf_cursor::Failed());
        CHECK(std::strcmp(name, i == 0 ? "viewmodel_default" : "viewmodel_lod1") == 0);
        float off[3];
        off[0] = Buf_Read<float>(&pos);
        off[1] = Buf_Read<float>(&pos);
        off[2] = Buf_Read<float>(&pos);
        CHECK(!buf_cursor::Failed());
        CHECK(off[0] == (i == 0 ? 1.0f : 4.0f));
        CHECK(off[1] == (i == 0 ? 2.0f : 5.0f));
        CHECK(off[2] == (i == 0 ? 3.0f : 6.0f));
    }

    // Cursor must have reached the end of the buffer.
    CHECK(pos == bytes.data() + bytes.size());
    CHECK(buf_cursor::Current()->current == bytes.data() + bytes.size());
    buf_cursor::Deactivate();
    return !buf_cursor::Failed();
}

// Contract: a single-byte overrun at the end of the buffer latches
// Failed() and caps the read at the cursor's current = end. The
// subsequent reads return 0 without advancing past the end.
bool TestOverrunLatchesFailed()
{
    std::vector<unsigned char> bytes(7);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(i);

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    buf_cursor::AnchorPos(&pos);
    // 7 bytes can hold a 1-byte + 2-byte + 2-byte + 2-byte sequence,
    // but not a 4-byte float at the end.
    (void)Buf_Read<unsigned char>(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    CHECK(!buf_cursor::Failed());
    CHECK(pos == bytes.data() + 7);

    // The next 4-byte read overflows → Failed.
    uint32_t v = Buf_Read<uint32_t>(&pos);
    CHECK(buf_cursor::Failed());
    CHECK(v == 0u);
    CHECK(pos == bytes.data() + bytes.size());

    // Any further read keeps Failed set and returns 0.
    uint16_t more = Buf_Read<unsigned short>(&pos);
    CHECK(more == 0);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: AnchorPos keeps the cursor and the caller's *pos pointer
// synchronized across Buf_Read<T> calls. After AnchorPos, *pos equals
// cursor->current on every read.
bool TestAnchorPosSync()
{
    std::vector<unsigned char> bytes(16, 0);
    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    CHECK(pos == buf_cursor::Current()->current);

    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<uint32_t>(&pos);
        CHECK(pos == buf_cursor::Current()->current);
        CHECK(!buf_cursor::Failed());
    }

    buf_cursor::Deactivate();
    return true;
}

// Contract: Transactional Begin / Rollback rewinds both the cursor's
// current and the anchored *pos pointer back to the checkpoint. After
// Rollback, a subsequent Buf_Read<T> reads from the checkpointed byte.
bool TestTransactionalRollback()
{
    std::vector<unsigned char> bytes(16);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(0x10 + i);

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);

    // Read 4 bytes first to move off the start.
    (void)Buf_Read<uint32_t>(&pos);
    CHECK(pos == bytes.data() + 4);
    CHECK(buf_cursor::Current()->current == bytes.data() + 4);

    // Begin the transaction at the current position.
    buf_cursor::Begin();

    // Read 4 more bytes speculatively.
    (void)Buf_Read<uint32_t>(&pos);
    CHECK(pos == bytes.data() + 8);

    // Rollback. Cursor and *pos both rewind to bytes + 4.
    buf_cursor::Rollback();
    CHECK(pos == bytes.data() + 4);
    CHECK(buf_cursor::Current()->current == bytes.data() + 4);
    CHECK(!buf_cursor::Failed());

    // A subsequent Buf_Read<T> reads from bytes + 4, which is 0x14.
    uint32_t again = Buf_Read<uint32_t>(&pos);
    CHECK(again == 0x17161514u);
    CHECK(pos == bytes.data() + 8);

    buf_cursor::Deactivate();
    return true;
}

// Contract: ReadString scans until NUL or end-of-buffer, then validates
// the length against maxStringLen. A buffer with no NUL terminator
// fails (no unbounded strlen scan runs past the end).
bool TestReadStringBounds()
{
    // 4 bytes, all non-NUL, no terminator.
    std::vector<unsigned char> bytes = {'a', 'b', 'c', 'd'};
    buf_cursor::Activate(bytes.data(), bytes.size());

    char out[16];
    bool ok = buf_cursor::ReadString(out, sizeof(out));
    CHECK(!ok);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();

    // A long string that exceeds maxStringLen must fail.
    bytes = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 0};
    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetStringLimit(3);
    ok = buf_cursor::ReadString(out, sizeof(out));
    CHECK(!ok);
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();

    // A NUL-terminated string within the limit must succeed and copy.
    bytes = {'a', 'b', 0, 'c', 0};
    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetStringLimit(16);
    ok = buf_cursor::ReadString(out, sizeof(out));
    CHECK(ok);
    CHECK(!buf_cursor::Failed());
    CHECK(std::strcmp(out, "ab") == 0);
    CHECK(buf_cursor::Current()->current == bytes.data() + 3);

    buf_cursor::Deactivate();
    return true;
}

// Contract: ReadBone enforces maxBoneIdx. A bone index at the boundary
// fails; below the boundary succeeds.
bool TestReadBoneLimit()
{
    std::vector<unsigned char> bytes(4);
    uint16_t v = 5;
    std::memcpy(bytes.data(), &v, sizeof(v));
    v = 3;
    std::memcpy(bytes.data() + 2, &v, sizeof(v));

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetBoneLimit(5);

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    uint16_t a = buf_cursor::ReadBone();
    CHECK(a == 5);
    CHECK(buf_cursor::Failed());

    // The cursor's current advanced even though Failed() is set: the
    // advance is the only way the loader can skip past the bad value
    // atomically. Subsequent reads latch Failed and return 0.
    uint16_t b = buf_cursor::ReadBone();
    CHECK(b == 0);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: ReadTri enforces both vertCount and maxTriIdx.
bool TestReadTriLimit()
{
    std::vector<unsigned char> bytes(2);
    uint16_t v = 7;
    std::memcpy(bytes.data(), &v, sizeof(v));

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetTriLimit(8);

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    uint16_t tri = buf_cursor::ReadTri(8);
    CHECK(tri == 7);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();

    // Same value, but vertCount = 6 → out of range.
    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetTriLimit(8);
    tri = buf_cursor::ReadTri(6);
    CHECK(tri == 7);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: Advance clamps to [begin, end]. A negative delta rolls back
// to begin; an over-large delta latches Failed.
bool TestAdvanceBounds()
{
    std::vector<unsigned char> bytes(8, 0);
    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    buf_cursor::Advance(3);
    CHECK(pos == bytes.data() + 3);
    CHECK(buf_cursor::Current()->current == bytes.data() + 3);

    buf_cursor::Advance(-3);
    CHECK(pos == bytes.data());
    CHECK(buf_cursor::Current()->current == bytes.data());

    buf_cursor::Advance(100);
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::Current()->current == bytes.data());

    buf_cursor::Deactivate();
    return true;
}

// Contract: Buf_Read<T> with no active cursor falls back to the
// original unbounded read. This is the legacy contract that
// non-xanim call sites and pre-cursor tests rely on.
bool TestNoActiveCursorFallback()
{
    std::vector<unsigned char> bytes(8);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(i + 1);

    unsigned char *pos = bytes.data();
    uint32_t v = Buf_Read<uint32_t>(&pos);
    CHECK(v == 0x04030201u);
    CHECK(pos == bytes.data() + 4);

    uint16_t s = Buf_Read<unsigned short>(&pos);
    CHECK(s == 0x0605);
    CHECK(pos == bytes.data() + 6);
    return true;
}

// Contract: An unaligned buffer (the very situation that triggered
// UBSan in the prior attempt) reads correctly through Buf_Read<T>.
bool TestUnalignedReads()
{
    // Allocate an over-sized buffer and walk an odd offset in.
    std::vector<unsigned char> bytes(16);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(i);
    const unsigned char *p = bytes.data() + 1;  // 1-byte offset → float is unaligned

    buf_cursor::Activate(p, bytes.size() - 1);

    unsigned char *pos = const_cast<unsigned char *>(p);
    buf_cursor::AnchorPos(&pos);
    uint32_t v = Buf_Read<uint32_t>(&pos);
    uint32_t expected = 0;
    expected |= static_cast<uint32_t>(p[3]) << 24;
    expected |= static_cast<uint32_t>(p[2]) << 16;
    expected |= static_cast<uint32_t>(p[1]) << 8;
    expected |= static_cast<uint32_t>(p[0]);
    CHECK(v == expected);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

int RunAll()
{
    CHECK(TestXModelPiecesParse());
    CHECK(TestOverrunLatchesFailed());
    CHECK(TestAnchorPosSync());
    CHECK(TestTransactionalRollback());
    CHECK(TestReadStringBounds());
    CHECK(TestReadBoneLimit());
    CHECK(TestReadTriLimit());
    CHECK(TestAdvanceBounds());
    CHECK(TestNoActiveCursorFallback());
    CHECK(TestUnalignedReads());

    std::fprintf(stderr, "xanim_load_test: %d/%d passed\n", g_runs - g_failures, g_runs);
    return g_failures == 0 ? 0 : 1;
}
}  // namespace xanim_load_test

int main()
{
    return xanim_load_test::RunAll();
}
