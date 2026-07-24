// xmodel_load_test: behavioral contracts for the transactional BufCursor
// when reading xmodel surfaces, collision data, config files, and
// xmodelparts. The cursor backs every Buf_Read<T> call so an overrun or
// a domain violation (bone, weight, triangle) latches Failed()=true
// instead of silently reading off the end of the buffer.
//
// xmodel_load_test mirrors xanim_load_test for the shared cursor but
// focuses on the xmodel-specific patterns: triangle indices bounded by
// vertCount, bone indices bounded by the model's bone limit, NUL-
// terminated filename copies with a max length, the XModelParts
// classification memcpy + useBones byte, and the "read config then
// read collision data then read LOD table" sequence that the master
// xmodel_load_obj.cpp follows end-to-end.

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
    buf_cursor::AnchorPos(&pos);
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    CHECK(!buf_cursor::Failed());

    // Read the header fields the loader reads before the first string:
    // 1 byte flags + 6 floats.
    (void)Buf_Read<unsigned char>(&pos);
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);
    CHECK(!buf_cursor::Failed());

    char physicsPreset[64];
    CHECK(buf_cursor::ReadString(physicsPreset, sizeof(physicsPreset)));
    CHECK(std::strcmp(physicsPreset, "phys/empty") == 0);

    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<float>(&pos);
        char name[64];
        CHECK(buf_cursor::ReadString(name, sizeof(name)));
        CHECK(!buf_cursor::Failed());
    }

    int collLod = Buf_Read<int>(&pos);
    CHECK(collLod == 0);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: collision data parse — numCollSurfs, per-tri plane +
// svec + tvec + mins/maxs + boneIdx + contents + surfFlags. Mirrors
// XModelLoadCollData.
bool TestCollisionDataParse()
{
    std::vector<unsigned char> bytes;

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

    push32(1);                        // numCollSurfs
    push32(1);                        // numCollTris
    pushFloat(0.0f);                  // plane[0]
    pushFloat(0.0f);                  // plane[1]
    pushFloat(1.0f);                  // plane[2]
    pushFloat(0.0f);                  // plane[3]
    pushFloat(1.0f);                  // svec[0]
    pushFloat(0.0f);                  // svec[1]
    pushFloat(0.0f);                  // svec[2]
    pushFloat(0.0f);                  // svec[3]
    pushFloat(0.0f);                  // tvec[0]
    pushFloat(1.0f);                  // tvec[1]
    pushFloat(0.0f);                  // tvec[2]
    pushFloat(0.0f);                  // tvec[3]
    pushFloat(-0.5f);                 // mins
    pushFloat(-0.5f);
    pushFloat(-0.5f);
    pushFloat(0.5f);                  // maxs
    pushFloat(0.5f);
    pushFloat(0.5f);
    push32(7);                        // boneIdx
    push32(0x00000001);               // contents
    push32(0);                        // surfFlags

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    int numCollSurfs = Buf_Read<int>(&pos);
    CHECK(numCollSurfs == 1);

    for (int i = 0; i < numCollSurfs; ++i)
    {
        int numCollTris = Buf_Read<int>(&pos);
        CHECK(numCollTris == 1);
        for (int j = 0; j < numCollTris; ++j)
        {
            float plane[4];
            for (int k = 0; k < 4; ++k)
                plane[k] = Buf_Read<float>(&pos);
            for (int k = 0; k < 4; ++k)
                (void)Buf_Read<float>(&pos);
            for (int k = 0; k < 4; ++k)
                (void)Buf_Read<float>(&pos);
            CHECK(!buf_cursor::Failed());
            (void)plane;
        }
        for (int k = 0; k < 3; ++k)
            (void)Buf_Read<float>(&pos);
        for (int k = 0; k < 3; ++k)
            (void)Buf_Read<float>(&pos);
        int boneIdx = Buf_Read<int>(&pos);
        int contents = Buf_Read<int>(&pos);
        int surfFlags = Buf_Read<int>(&pos);
        CHECK(boneIdx == 7);
        CHECK(contents == 1);
        CHECK(surfFlags == 0);
        CHECK(!buf_cursor::Failed());
    }

    buf_cursor::Deactivate();
    return true;
}

// Contract: triangle indices are bounded by vertCount. A triIndex
// >= vertCount must trip Failed() instead of returning an out-of-range
// index.
bool TestTriBoundaryCheck()
{
    std::vector<unsigned char> bytes(6);
    uint16_t v = 2;
    std::memcpy(bytes.data(), &v, sizeof(v));
    v = 5;
    std::memcpy(bytes.data() + 2, &v, sizeof(v));
    v = 10;
    std::memcpy(bytes.data() + 4, &v, sizeof(v));

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetTriLimit(0xFFFF);
    buf_cursor::SetBoneLimit(0xFFFF);

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);
    uint16_t tri0 = buf_cursor::ReadTri(8);  // vertCount = 8, index 2 → ok
    CHECK(tri0 == 2);
    CHECK(!buf_cursor::Failed());

    uint16_t tri1 = buf_cursor::ReadTri(8);  // index 5 → ok
    CHECK(tri1 == 5);
    CHECK(!buf_cursor::Failed());

    uint16_t tri2 = buf_cursor::ReadTri(8);  // index 10 → out of range, latch Failed
    CHECK(tri2 == 10);
    CHECK(buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: a model load sequence that runs through config → collision
// → LOD table must keep the cursor and *pos synchronized across all
// Buf_Read<T> calls. This is the master sequence xmodel_load_obj.cpp
// exercises; if the cursor and *pos desync, the test catches it.
bool TestFullLoadSequenceSync()
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

    push16(25);                // version
    bytes.push_back(0x00);     // flags
    pushFloat(-1.0f);
    pushFloat(-1.0f);
    pushFloat(-1.0f);
    pushFloat(1.0f);
    pushFloat(1.0f);
    pushFloat(1.0f);
    pushString("phys/x");
    pushFloat(0.0f);
    pushString("viewmodel_only");
    pushFloat(0.0f);
    pushString("");
    pushFloat(0.0f);
    pushString("");
    pushFloat(0.0f);
    pushString("");
    push32(0);                 // collLod
    push32(0);                 // numCollSurfs (no collision data)
    push16(1);                 // numsurfs (LOD 0)
    pushString("surface_a");  // surface name
    // bone info: 1 bone, 6 floats
    pushFloat(-1.0f);
    pushFloat(-1.0f);
    pushFloat(-1.0f);
    pushFloat(1.0f);
    pushFloat(1.0f);
    pushFloat(1.0f);

    buf_cursor::Activate(bytes.data(), bytes.size());
    buf_cursor::SetStringLimit(64);

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);

    // Config header.
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    (void)Buf_Read<unsigned char>(&pos);
    for (int i = 0; i < 6; ++i)
        (void)Buf_Read<float>(&pos);
    char physicsPreset[64];
    CHECK(buf_cursor::ReadString(physicsPreset, sizeof(physicsPreset)));
    // XModelLoadConfigFile reads each entry as (dist, filename).
    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<float>(&pos);
        char entry[64];
        CHECK(buf_cursor::ReadString(entry, sizeof(entry)));
    }
    (void)Buf_Read<int>(&pos);
    CHECK(!buf_cursor::Failed());

    // Collision data header.
    int numCollSurfs = Buf_Read<int>(&pos);
    CHECK(numCollSurfs == 0);

    // LOD table — this is the master loader's two-level walk: per-LOD
    // numsurfs then per-surface name. The cursor advances through both
    // bursts and the anchored *pos keeps the cursor and the caller's
    // pointer in sync.
    for (int lod = 0; lod < 4; ++lod)
    {
        // The test only populates one LOD; the next 3 are empty.
        if (lod > 0)
            break;
        uint16_t numsurfs = Buf_Read<unsigned short>(&pos);
        CHECK(numsurfs == 1);
        for (int i = 0; i < numsurfs; ++i)
        {
            char sufName[64];
            CHECK(buf_cursor::ReadString(sufName, sizeof(sufName)));
            CHECK(std::strcmp(sufName, "surface_a") == 0);
        }
    }

    // The cursor and *pos must agree on the trailing position. If the
    // cursor anchored pos and tracked it across all reads, the trailing
    // pos == cursor->current.
    CHECK(pos == buf_cursor::Current()->current);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

// Contract: xmodelparts parse — version, numChildBones, numRootBones,
// per-bone parent index + trans + quat, bone names, classification,
// useBones byte. Mirrors XModelPartsLoadFile.
bool TestXModelPartsParse()
{
    std::vector<unsigned char> bytes;

    auto push16 = [&](uint16_t v) {
        bytes.push_back(static_cast<unsigned char>(v & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    };
    auto pushFloat = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<unsigned char>((bits >> (i * 8)) & 0xFF));
    };

    push16(25);          // version
    push16(1);           // numChildBones
    push16(1);           // numRootBones
    bytes.push_back(1);  // parent index for child bone (relative)
    pushFloat(0.0f);
    pushFloat(0.0f);
    pushFloat(0.0f);
    push16(0);           // quat[0]
    push16(0);
    push16(0);
    push16(0x7FFF);      // quat[3] (sqrt scaled)
    for (const char *p = "tag_root"; *p; ++p)
        bytes.push_back(static_cast<unsigned char>(*p));
    bytes.push_back(0);
    for (const char *p = "tag_child"; *p; ++p)
        bytes.push_back(static_cast<unsigned char>(*p));
    bytes.push_back(0);
    bytes.push_back(0);  // partClassification[0]
    bytes.push_back(1);  // partClassification[1]
    bytes.push_back(1);  // useBones

    buf_cursor::Activate(bytes.data(), bytes.size());

    unsigned char *pos = bytes.data();
    buf_cursor::AnchorPos(&pos);

    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    uint16_t numChildBones = Buf_Read<unsigned short>(&pos);
    uint16_t numRootBones = Buf_Read<unsigned short>(&pos);
    CHECK(numChildBones == 1);
    CHECK(numRootBones == 1);

    int numBones = numChildBones + numRootBones;
    for (int i = 0; i < numChildBones; ++i)
    {
        uint8_t idx = buf_cursor::ReadWeight();
        buf_cursor::AnchorPos(&pos);
        CHECK(idx == 1);
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
        // ConsumeQuatNoSwap reads 4 shorts.
        for (int k = 0; k < 4; ++k)
            (void)Buf_Read<unsigned short>(&pos);
    }

    for (int i = 0; i < numBones; ++i)
    {
        char name[128];
        CHECK(buf_cursor::ReadString(name, sizeof(name)));
    }

    for (int i = 0; i < numBones; ++i)
    {
        uint8_t cls = buf_cursor::ReadWeight();
        CHECK(!buf_cursor::Failed());
        CHECK(cls == (i == 0 ? 0 : 1));
    }

    bool useBones = (buf_cursor::ReadWeight() != 0);
    CHECK(useBones);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    return true;
}

int RunAll()
{
    CHECK(TestConfigFileParse());
    CHECK(TestCollisionDataParse());
    CHECK(TestTriBoundaryCheck());
    CHECK(TestFullLoadSequenceSync());
    CHECK(TestXModelPartsParse());

    std::fprintf(stderr, "xmodel_load_test: %d/%d passed\n", g_runs - g_failures, g_runs);
    return g_failures == 0 ? 0 : 1;
}
}  // namespace xmodel_load_test

int main()
{
    return xmodel_load_test::RunAll();
}
