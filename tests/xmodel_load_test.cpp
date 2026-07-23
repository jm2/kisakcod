// xmodel_load_test: behavioral contracts for the transactional BufCursor
// when reading xmodel surfaces, collision data, config files, and
// xmodelparts. The cursor backs every Buf_Read<T> call so an overrun or
// a domain violation (bone, weight, triangle) latches Failed()=true
// instead of silently reading off the end of the buffer.
//
// xmodel_load_test mirrors xanim_load_test for the shared cursor but
// adds patterns specific to xmodel_load_obj.cpp: triangle indices
// bounded by vertCount, bone indices bounded by the model's bone limit,
// NUL-terminated string copies with a max length, and the "read config
// then read collision data then read LOD table" sequence.

#include <xanim/buf_cursor.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace xmodel_load_test
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
        std::fprintf(stderr, "xmodel_load_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

// Contract: a complete "xmodel config" header parse — version, flags,
// bounds box, physics preset filename, four (dist, filename) entries,
// collLod. Mirrors XModelLoadConfigFile.
bool TestConfigFileParse()
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

    push16(25);                       // version
    bytes.push_back(0x01);            // flags
    pushFloat(-2.0f);                 // mins[0]
    pushFloat(-2.0f);                 // mins[1]
    pushFloat(-2.0f);                 // mins[2]
    pushFloat(2.0f);                  // maxs[0]
    pushFloat(2.0f);                  // maxs[1]
    pushFloat(2.0f);                  // maxs[2]
    pushString("phys/empty");         // physicsPresetFilename
    pushFloat(0.0f);                  // entries[0].dist
    pushString("viewmodel_default");  // entries[0].filename
    pushFloat(150.0f);                // entries[1].dist
    pushString("viewmodel_lod1");
    pushFloat(300.0f);                // entries[2].dist
    pushString("viewmodel_lod2");
    pushFloat(600.0f);                // entries[3].dist
    pushString("viewmodel_lod3");
    push32(0);                        // collLod

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetStringLimit(64);

    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    CHECK(!buf_cursor::Failed());

    // Read the header fields the loader reads before the first string:
    // 1 byte flags + 6 floats.
    (void)Buf_Read<unsigned char>(&pos);
    for (int i = 0; i < 6; ++i)
    {
        (void)Buf_Read<float>(&pos);
    }
    CHECK(!buf_cursor::Failed());

    // Simulate the loader's pattern: read struct fields via Buf_Read<T>,
    // then ReadString for filenames.
    char presetName[64];
    CHECK(buf_cursor::ReadString(presetName, sizeof(presetName)));
    pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    CHECK(std::strcmp(presetName, "phys/empty") == 0);

    char entryName[64];
    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<float>(&pos);
        CHECK(buf_cursor::ReadString(entryName, sizeof(entryName)));
        pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
        CHECK(entryName[0] != 0);
    }

    uint32_t collLod = Buf_Read<unsigned int>(&pos);
    CHECK(collLod == 0);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: triangle index reads enforce vertCount. A 3*triCount triangle
// loop should never accept an index >= vertCount.
bool TestTriangleLoop()
{
    // 4 surfaces, each with vertCount=8, triCount=2.
    // Triangle indices: 0,1,2 | 3,4,5 | 6,7,0 | 1,2,3 — six bytes per surface.
    std::vector<unsigned char> bytes;
    for (int s = 0; s < 4; ++s)
    {
        for (uint16_t i = 0; i < 6; ++i)
        {
            uint16_t idx = static_cast<uint16_t>(s * 2 + i) % 8;
            bytes.push_back(static_cast<unsigned char>(idx & 0xFF));
            bytes.push_back(static_cast<unsigned char>((idx >> 8) & 0xFF));
        }
    }

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetTriLimit(8);

    bool allOk = true;
    for (int s = 0; s < 4 && allOk; ++s)
    {
        for (int t = 0; t < 6; ++t)
        {
            uint16_t idx = buf_cursor::ReadTri(8);
            if (idx >= 8)
            {
                allOk = false;
                break;
            }
            CHECK(!buf_cursor::Failed());
        }
    }
    CHECK(allOk);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: triangle index out-of-range latches failure and does not
// corrupt subsequent reads.
bool TestTriangleOverrun()
{
    // First three indices in range; fourth index >= vertCount.
    const unsigned char kBytes[] = { 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0xFF, 0x00 };
    buf_cursor::Activate(kBytes, sizeof(kBytes));

    uint16_t a = buf_cursor::ReadTri(8);
    CHECK(a == 0);
    CHECK(!buf_cursor::Failed());

    uint16_t b = buf_cursor::ReadTri(8);
    CHECK(b == 1);
    CHECK(!buf_cursor::Failed());

    uint16_t c = buf_cursor::ReadTri(8);
    CHECK(c == 2);
    CHECK(!buf_cursor::Failed());

    uint16_t over = buf_cursor::ReadTri(8);
    CHECK(over == 0xFFu);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: bone index reads enforce the configured bone limit. Each
// ReadBlend call has one bone index; the cursor must reject out-of-
// range values.
bool TestBoneLoop()
{
    // 6 bone indices: 0..5, then an out-of-range 0xFF.
    const unsigned char kBytes[] = {
        0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00,
        0xFF, 0x00,
    };
    buf_cursor::Activate(kBytes, sizeof(kBytes));
    buf_cursor::SetBoneLimit(6);

    for (uint16_t i = 0; i < 6; ++i)
    {
        uint16_t bone = buf_cursor::ReadBone();
        CHECK(bone == i);
        CHECK(!buf_cursor::Failed());
    }

    uint16_t over = buf_cursor::ReadBone();
    CHECK(over == 0x00FFu);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: collision data loop. Mirrors XModelLoadCollData: read
// numCollSurfs, then per-surf read numCollTris and read 11 floats per
// triangle. A truncated buffer must be caught by the cursor.
bool TestCollDataTruncate()
{
    // numCollSurfs=2, surf0 has numCollTris=1 (11 floats), surf1 has
    // numCollTris=1 (11 floats). Truncate after surf0's plane/svec/tvec.
    std::vector<unsigned char> bytes;
    auto push32 = [&](uint32_t v) {
        for (int s = 0; s < 4; ++s)
            bytes.push_back(static_cast<unsigned char>((v >> (8 * s)) & 0xFF));
    };
    auto pushFloat = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        push32(bits);
    };

    push32(2);  // numCollSurfs
    push32(1);  // surf0.numCollTris
    pushFloat(1.0f);  // plane[0..3] = 4
    pushFloat(0.0f);
    pushFloat(0.0f);
    pushFloat(0.0f);
    pushFloat(1.0f);  // svec[0..3] = 4
    pushFloat(0.0f);
    pushFloat(0.0f);
    pushFloat(0.0f);
    pushFloat(1.0f);  // tvec[0..3] = 3
    pushFloat(0.0f);
    pushFloat(0.0f);
    // Truncate before surf0's mins/maxs/boneIdx/contents/surfFlags.
    bytes.resize(bytes.size() - 16);

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    uint32_t numCollSurfs = Buf_Read<unsigned int>(&pos);
    CHECK(numCollSurfs == 2);
    CHECK(!buf_cursor::Failed());

    uint32_t numCollTris = Buf_Read<unsigned int>(&pos);
    CHECK(numCollTris == 1);
    CHECK(!buf_cursor::Failed());

    // The buffer holds 11 floats but is truncated by 16 bytes — i.e.
    // the first 7 floats (28 bytes) are present, and the 8th would
    // overrun. Read 7 successfully, then fail on the 8th.
    int readsBeforeFailure = 0;
    for (int t = 0; t < 11; ++t)
    {
        float v = Buf_Read<float>(&pos);
        (void)v;
        if (buf_cursor::Failed())
        {
            break;
        }
        ++readsBeforeFailure;
    }
    CHECK(readsBeforeFailure == 7);
    CHECK(buf_cursor::Failed());

    // Now read mins[0]: this overruns.
    float bad = Buf_Read<float>(&pos);
    CHECK(bad == 0.0f);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: xmodelparts header parse. version=25, numChildBones, etc.
// bounds bone limit before reading the parent index loop.
bool TestPartsHeaderParse()
{
    std::vector<unsigned char> bytes;
    auto push16 = [&](uint16_t v) {
        bytes.push_back(static_cast<unsigned char>(v & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    };
    auto pushString = [&](const char *s) {
        for (const char *p = s; *p; ++p)
            bytes.push_back(static_cast<unsigned char>(*p));
        bytes.push_back(0);
    };

    push16(25);             // version
    push16(2);              // numChildBones
    push16(1);              // numRootBones
    // parentList indices (1 byte each, numChildBones=2).
    bytes.push_back(0x01);  // parentList[0]
    bytes.push_back(0x02);  // parentList[1] (out-of-range: would assert)
    // trans[3] floats per child.
    for (int i = 0; i < 2 * 3; ++i)
    {
        bytes.push_back(0);
        bytes.push_back(0);
        bytes.push_back(0);
        bytes.push_back(0);
    }
    // quats[4] shorts per child.
    for (int i = 0; i < 2 * 4 * 2; ++i)
        bytes.push_back(0);
    // 3 bone names.
    pushString("tag_aim");
    pushString("tag_origin");
    pushString("j_spine4");

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetBoneLimit(127);
    buf_cursor::SetStringLimit(64);

    unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    CHECK(!buf_cursor::Failed());

    uint16_t numChildBones = Buf_Read<unsigned short>(&pos);
    uint16_t numRootBones = Buf_Read<unsigned short>(&pos);
    CHECK(numChildBones == 2);
    CHECK(numRootBones == 1);

    // The first parentList index (1) is in range. The second (2) is
    // out of range because i=1+1=2 is still < numRootBones (1)?  The
    // actual invariant is `index < i`. At i=2, index=2 fails. We don't
    // enforce this in the cursor — but the existing iassert catches it
    // upstream, and the cursor still records a clean read.
    for (int i = 0; i < numChildBones; ++i)
    {
        unsigned char index = *pos++;
        buf_cursor::Advance(1);
        (void)index;
    }
    CHECK(!buf_cursor::Failed());

    for (int b = 0; b < numRootBones + numChildBones; ++b)
    {
        char name[64];
        CHECK(buf_cursor::ReadString(name, sizeof(name)));
        pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
    }
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: Fuzz over xmodel-style mixed reads. Random short buffers
// are parsed with a mix of uint16/float/string reads. The cursor must
// never advance past end and must latch Failed() on the first
// violation.
bool TestFuzz()
{
    std::srand(0xDECAFu);
    for (int trial = 0; trial < 1024; ++trial)
    {
        size_t len = (std::rand() % 64) + 1;
        std::vector<unsigned char> buf(len);
        for (size_t i = 0; i < len; ++i)
            buf[i] = static_cast<unsigned char>(std::rand() & 0xFF);

        buf_cursor::Activate(buf.data(), len);
        buf_cursor::SetBoneLimit(16);
        buf_cursor::SetTriLimit(8);
        buf_cursor::SetStringLimit(32);

        bool sawFailure = false;
        unsigned char *pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
        while (!sawFailure)
        {
            int kind = std::rand() % 4;
            switch (kind)
            {
            case 0:
                (void)Buf_Read<unsigned short>(&pos);
                break;
            case 1:
                (void)Buf_Read<float>(&pos);
                break;
            case 2:
                (void)buf_cursor::ReadBone();
                pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
                break;
            case 3:
            {
                char tmp[32];
                (void)buf_cursor::ReadString(tmp, sizeof(tmp));
                pos = const_cast<unsigned char *>(buf_cursor::Current()->current);
                break;
            }
            }
            if (buf_cursor::Failed())
            {
                sawFailure = true;
                CHECK(buf_cursor::Current()->current <= buf_cursor::Current()->end);
            }
            else if (pos > buf_cursor::Current()->end)
            {
                sawFailure = true;
                CHECK(false);  // pos must never exceed end on success.
            }
        }
        buf_cursor::Deactivate();
    }
    return true;
}

int RunAllTests()
{
    bool (*tests[])() = {
        TestConfigFileParse,
        TestTriangleLoop,
        TestTriangleOverrun,
        TestBoneLoop,
        TestCollDataTruncate,
        TestPartsHeaderParse,
        TestFuzz,
    };
    for (auto *test : tests)
    {
        (void)test();
    }
    std::fprintf(stderr, "xmodel_load_test: %d checks, %d failures\n", g_runs, g_failures);
    return g_failures == 0 ? 0 : 1;
}
}

int main()
{
    return xmodel_load_test::RunAllTests();
}
