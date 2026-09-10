// xmodel_nested_cursor_test: production-call-graph contracts for the
// scoped nested cursor ownership and the checked second-pass seek.
//
// The production loaders nest cursor scopes: XModelLoadFile activates a
// cursor over the xmodel buffer and then calls XModelPartsPrecache /
// XModelSurfsPrecache, which activate their own cursors over different
// file buffers. Before the scoped save/restore landed (ki-okmr / #124),
// the nested Activate overwrote the parent's position, limits, failure
// state and anchored *pos, and the nested Deactivate cleared them
// entirely — the outer parse resumed unbounded (or, on the material
// second pass, at the wrong offset entirely).
//
// These tests drive the real buf_cursor entry points (Activate /
// AnchorPos / Tell / SeekTo / Advance / ReadString / ReadBytes /
// ReadWeight / ReadBone / Buf_Read<T> / Deactivate) in the exact
// sequence the production loaders issue them, over controlled fixtures
// built to the real xmodel / xmodelparts / xmodelsurfs layouts. The
// loader TUs themselves cannot link in a portable test binary
// (DirectX / Miles / ODE header web; engine targets are win32-only in
// this checkout), so full production-harness enrollment of
// XModelLoadFile stays scoped to the corpus stage (#125 / ki-458h).
// tests/xmodel_cursor_source_invariants_test.cmake pins the production
// call sites so the loader-side contract cannot silently regress.
//
// Codacy file-size disposition (tests file >600 non-comment LOC): this
// TU is one consolidated contract suite for a single production
// dependency chain (buf_cursor nested ownership + checked second-pass
// positioning, stage #124/ki-okmr). Its sections deliberately share the
// production-layout fixtures (BuildModelFile / BuildPartsFile /
// BuildSurfsHeaderFile / ByteWriter) and the CHECK harness, and the
// review gate requires the negative / overflow sections to stay
// co-located with the restore contracts they constrain — splitting
// them across TUs would fragment the failure-cleanup evidence CI
// reviews as one unit. The four over-length methods were already split
// into shared helpers (59e92d62, logic unchanged); the residual size
// above the metric threshold is the deliberate cost of extending the
// overflow sections with the 17th-scope failure-latching and
// overflowed-frame re-activation contracts (PR #140 rework). The
// warning is dispositioned as accepted test-fixture scope, not
// production complexity.

#include <xanim/buf_cursor.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace xmodel_nested_cursor_test
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
        std::fprintf(stderr, "xmodel_nested_cursor_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// ---------------------------------------------------------------------------
// Controlled fixtures, built to the production file layouts.
// ---------------------------------------------------------------------------

struct ByteWriter
{
    std::vector<unsigned char> bytes;

    void Push8(unsigned int v) { bytes.push_back(static_cast<unsigned char>(v & 0xFF)); }
    void Push16(uint16_t v)
    {
        bytes.push_back(static_cast<unsigned char>(v & 0xFF));
        bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    }
    void Push32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFF));
    }
    void PushFloat(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        Push32(bits);
    }
    void PushString(const char *s)
    {
        for (const char *p = s; *p; ++p)
            bytes.push_back(static_cast<unsigned char>(*p));
        bytes.push_back(0);
    }
};

// xmodel/<name> body: config header (mirrors XModelLoadConfigFile), no
// collision data, the LOD table (per LOD: numsurfs + NUL-terminated
// surface names), per-bone info (6 floats per bone), then two bytes
// encoding bone index 6 as a limit probe. This is the exact byte order
// XModelLoadFile walks: config -> collision -> LOD table (first pass)
// -> [nested xmodelparts load] -> bone infos -> material second pass
// (checked rewind to the LOD table).
ByteWriter BuildModelFile()
{
    ByteWriter w;
    w.Push16(25);               // config version
    w.Push8(0x00);              // flags
    w.PushFloat(-1.0f);         // mins[0..2]
    w.PushFloat(-1.0f);
    w.PushFloat(-1.0f);
    w.PushFloat(1.0f);          // maxs[0..2]
    w.PushFloat(1.0f);
    w.PushFloat(1.0f);
    w.PushString("phys/x");     // physicsPresetFilename
    w.PushFloat(0.0f);          // entries[0].dist
    w.PushString("lod_a");      // entries[0].filename
    w.PushFloat(150.0f);
    w.PushString("lod_b");
    w.PushFloat(300.0f);
    w.PushString("");
    w.PushFloat(600.0f);
    w.PushString("");
    w.Push32(0);                // collLod
    w.Push32(0);                // numCollSurfs (no collision data)

    // LOD table: two populated LODs so the material second pass walks
    // real surface names. lod_a: 2 surfaces, lod_b: 1 surface.
    w.Push16(2);
    w.PushString("mat_first_a");
    w.PushString("mat_first_b");
    w.Push16(1);
    w.PushString("mat_first_c");

    // Bone info: 1 bone, 6 floats (bounds[0], bounds[1]).
    w.PushFloat(0.25f);
    w.PushFloat(0.5f);
    w.PushFloat(0.75f);
    w.PushFloat(1.25f);
    w.PushFloat(1.5f);
    w.PushFloat(1.75f);

    // Trailing bone-index probe (uint16 little-endian 6): a ReadBone
    // against the restored parent bone limit (4) must latch failed.
    w.Push16(6);
    return w;
}

// xmodelparts/<name> body (mirrors XModelPartsLoadFile): version,
// numChildBones, numRootBones, per-child-bone (parent index byte, 3
// trans floats, 4 quat shorts), NUL-terminated bone names, the
// partClassification bytes, and the trailing useBones byte.
ByteWriter BuildPartsFile()
{
    ByteWriter w;
    w.Push16(25);           // version
    w.Push16(1);            // numChildBones
    w.Push16(1);            // numRootBones
    w.Push8(1);             // parent index for the child bone (relative)
    w.PushFloat(0.0f);      // trans[0..2]
    w.PushFloat(0.0f);
    w.PushFloat(0.0f);
    w.Push16(0);            // quat[0..3]
    w.Push16(0);
    w.Push16(0);
    w.Push16(0x7FFF);
    w.PushString("tag_root");
    w.PushString("tag_child");
    w.Push8(0);             // partClassification[0]
    w.Push8(1);             // partClassification[1]
    w.Push8(1);             // useBones
    return w;
}

// xmodelsurfs/<name> header (mirrors R_XModelSurfsLoadFile up to the
// point the production loader hands off to XModelReadSurfaces). The
// nested windows in these tests only need the header reads that happen
// before the surface body; the bounded surface-body read contracts are
// covered by the cursor-primitive tests.
ByteWriter BuildSurfsHeaderFile(short numsurfs)
{
    ByteWriter w;
    w.Push16(25);  // version
    w.Push16(static_cast<uint16_t>(numsurfs));
    return w;
}

// Config header + collision header: everything XModelLoadFile reads
// before the LOD table.
void RunConfigAndCollisionHeader(unsigned char *&pos)
{
    uint16_t version = Buf_Read<unsigned short>(&pos);
    CHECK(version == 25);
    (void)Buf_Read<unsigned char>(&pos);
    for (int i = 0; i < 6; ++i)
        (void)Buf_Read<float>(&pos);
    char physPreset[64];
    CHECK(buf_cursor::ReadString(physPreset, sizeof(physPreset)));
    CHECK(std::strcmp(physPreset, "phys/x") == 0);
    for (int i = 0; i < 4; ++i)
    {
        (void)Buf_Read<float>(&pos);
        char entry[64];
        CHECK(buf_cursor::ReadString(entry, sizeof(entry)));
    }
    (void)Buf_Read<int>(&pos);
    CHECK(!buf_cursor::Failed());

    int numCollSurfs = Buf_Read<int>(&pos);
    CHECK(numCollSurfs == 0);
    CHECK(!buf_cursor::Failed());
}

// The first LOD walk (per LOD: numsurfs + that many surface names).
void RunLodTableWalk(unsigned char *&pos)
{
    static const char *const kExpectedNames[] = {
        "mat_first_a", "mat_first_b", "mat_first_c"};
    int nameIndex = 0;
    for (int lod = 0; lod < 2; ++lod)
    {
        uint16_t numsurfs = Buf_Read<unsigned short>(&pos);
        CHECK(numsurfs == (lod == 0 ? 2 : 1));
        for (uint16_t s = 0; s < numsurfs; ++s)
        {
            char surfName[128];
            CHECK(buf_cursor::ReadString(surfName, sizeof(surfName)));
            CHECK(std::strcmp(surfName, kExpectedNames[nameIndex]) == 0);
            ++nameIndex;
        }
    }
    CHECK(!buf_cursor::Failed());
}

// The per-child-bone body walk: parent-index read, the mid-loop
// re-anchor the production loader issues after each parent-index read,
// then the trans floats and quat shorts.
void RunNestedPartsBoneBodies(unsigned char *&pos, int numBones, int numRootBones)
{
    for (int i = numRootBones; i < numBones; ++i)
    {
        uint8_t parentIndex = buf_cursor::ReadWeight();
        buf_cursor::AnchorPos(&pos);  // production re-anchor per bone
        CHECK(parentIndex == 1);
        for (int f = 0; f < 3; ++f)
            (void)Buf_Read<float>(&pos);
        for (int q = 0; q < 4; ++q)
            (void)Buf_Read<unsigned short>(&pos);
    }
}

// The bone-name scan. Returns false where the production loader would
// reject the file (a failed name read); the caller owns the Deactivate.
bool RunNestedPartsBoneNames(int numBones)
{
    static const char *const kExpectedBoneNames[] = {"tag_root", "tag_child"};
    for (int i = 0; i < numBones; ++i)
    {
        char nameBuf[128];
        if (!buf_cursor::ReadString(nameBuf, sizeof(nameBuf)))
            return false;
        CHECK(std::strcmp(nameBuf, kExpectedBoneNames[i]) == 0);
    }
    return true;
}

// partClassification room pre-check + bulk read + useBones byte — the
// production tail sequence of XModelPartsLoadFile. Returns false where
// the production loader would reject the file (short classification
// room); the caller owns the Deactivate.
bool RunNestedPartsClassification(int numBones, bool &useBones)
{
    const buf_cursor::BufCursor *cursor = buf_cursor::Current();
    CHECK(cursor != nullptr);
    CHECK(static_cast<size_t>(cursor->end - cursor->current) >= static_cast<size_t>(numBones + 1));
    unsigned char classification[2] = {0xEE, 0xEE};
    if (!buf_cursor::ReadBytes(classification, sizeof(classification), numBones))
        return false;
    CHECK(classification[0] == 0 && classification[1] == 1);
    useBones = (buf_cursor::ReadWeight() != 0);
    CHECK(useBones);
    CHECK(!buf_cursor::Failed());
    return true;
}

// The production XModelPartsLoadFile window: activate over the parts
// buffer, anchor, read the full body, deactivate. Sets no domain
// limits — exactly like production, where Activate resets the nested
// scope's limits to the defaults. Returns false exactly where the
// production loader would reject the file.
bool RunNestedPartsWindow(const ByteWriter &partsFile, bool &useBones)
{
    unsigned char *pos = const_cast<unsigned char *>(partsFile.bytes.data());
    buf_cursor::Activate(partsFile.bytes.data(), partsFile.bytes.size());
    buf_cursor::AnchorPos(&pos);

    uint16_t version = Buf_Read<unsigned short>(&pos);
    if (version != 25)
    {
        buf_cursor::Deactivate();
        return false;
    }
    uint16_t numChildBones = Buf_Read<unsigned short>(&pos);
    uint16_t numRootBones = Buf_Read<unsigned short>(&pos);
    const int numBones = numChildBones + numRootBones;
    CHECK(numBones == 2);

    RunNestedPartsBoneBodies(pos, numBones, numRootBones);
    if (!RunNestedPartsBoneNames(numBones))
    {
        buf_cursor::Deactivate();
        return false;
    }
    if (!RunNestedPartsClassification(numBones, useBones))
    {
        buf_cursor::Deactivate();
        return false;
    }

    buf_cursor::Deactivate();
    return true;
}
}  // namespace

// ---------------------------------------------------------------------------
// Contract: a nested parts load must not destroy the parent cursor.
// Position, anchored *pos, domain limits and failure state must all be
// restored exactly when the nested window deactivates — cold (first
// nested window) and warm (a second window on the same thread).
// ---------------------------------------------------------------------------
bool TestNestedPartsLoadRestoresParent()
{
    ByteWriter modelFile = BuildModelFile();
    ByteWriter partsFile = BuildPartsFile();

    for (int pass = 0; pass < 2; ++pass)  // cold, then warm
    {
        unsigned char *pos = modelFile.bytes.data();
        buf_cursor::Activate(modelFile.bytes.data(), modelFile.bytes.size());
        buf_cursor::AnchorPos(&pos);
        buf_cursor::SetStringLimit(64);
        buf_cursor::SetBoneLimit(4);  // parent-level bone bound

        RunConfigAndCollisionHeader(pos);
        RunLodTableWalk(pos);

        // Checkpoint the bone-info start exactly like the production
        // loader does around the nested parts load.
        const unsigned char *meshCheckpoint = buf_cursor::Tell();
        CHECK(meshCheckpoint != nullptr);
        CHECK(meshCheckpoint == pos);
        const uint32_t parentBoneLimit = buf_cursor::Current()->maxBoneIdx;
        CHECK(parentBoneLimit == 4);

        // Nested window: the production XModelPartsPrecache call.
        bool useBones = false;
        CHECK(RunNestedPartsWindow(partsFile, useBones));

        // Parent scope restored exactly: active, positioned at the
        // checkpoint, un-failed, parent limits back (the nested window
        // ran on default limits; nothing leaked in either direction).
        const buf_cursor::BufCursor *parent = buf_cursor::Current();
        CHECK(parent != nullptr);
        CHECK(parent->current == meshCheckpoint);
        CHECK(pos == meshCheckpoint);
        CHECK(!buf_cursor::Failed());
        CHECK(parent->maxBoneIdx == parentBoneLimit);
        CHECK(parent->maxStringLen == 64);

        // Parent continues bounded: the bone-info floats read through
        // the restored cursor.
        const float expectedBoneInfo[6] = {0.25f, 0.5f, 0.75f, 1.25f, 1.5f, 1.75f};
        for (int f = 0; f < 6; ++f)
        {
            float value = Buf_Read<float>(&pos);
            CHECK(value == expectedBoneInfo[f]);
        }
        CHECK(!buf_cursor::Failed());

        // The restored parent bone limit is live: the trailing probe
        // encodes bone index 6 >= 4, so ReadBone must latch failed even
        // though the nested window raised its own limit to 128.
        (void)buf_cursor::ReadBone();
        CHECK(buf_cursor::Failed());
        CHECK(pos == modelFile.bytes.data() + modelFile.bytes.size());

        buf_cursor::Deactivate();

        // Cleanup contract: nothing active, nothing failed.
        CHECK(buf_cursor::Current() == nullptr);
        CHECK(!buf_cursor::Failed());
    }
    return true;
}

// Direction 1: nested parse fails (truncated parts body), parent must
// come back clean.
void ExpectNestedFailureLeavesParentClean(const ByteWriter &modelFile)
{
    unsigned char *pos = const_cast<unsigned char *>(modelFile.bytes.data());
    buf_cursor::Activate(modelFile.bytes.data(), modelFile.bytes.size());
    buf_cursor::AnchorPos(&pos);
    RunConfigAndCollisionHeader(pos);
    RunLodTableWalk(pos);
    const unsigned char *checkpoint = buf_cursor::Tell();

    // Truncated parts fixture: version+counts only, no body.
    ByteWriter truncated;
    truncated.Push16(25);
    truncated.Push16(1);
    truncated.Push16(1);
    unsigned char *truncPos = truncated.bytes.data();
    buf_cursor::Activate(truncated.bytes.data(), truncated.bytes.size());
    buf_cursor::AnchorPos(&truncPos);
    (void)Buf_Read<unsigned short>(&truncPos);
    (void)Buf_Read<unsigned short>(&truncPos);
    (void)Buf_Read<unsigned short>(&truncPos);
    // Body reads now overrun: the bone-name scan hits the end.
    char nameBuf[128];
    CHECK(!buf_cursor::ReadString(nameBuf, sizeof(nameBuf)));
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();

    // Parent restored: clean, positioned, readable.
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(!buf_cursor::Failed());
    CHECK(buf_cursor::Tell() == checkpoint);
    CHECK(pos == checkpoint);
    float value = Buf_Read<float>(&pos);
    CHECK(value == 0.25f);
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
}

// Direction 2: parent fails BEFORE the nested window; the failure must
// survive the nested window's Deactivate.
void ExpectNestedFailureLatchedParent()
{
    // 4-byte buffer: exactly one float read fits; the second read
    // overruns and latches failure, and the third is a second overrun
    // that stays latched (returns zero, moves nothing).
    unsigned char tiny[4] = {0, 0, 0, 0};
    unsigned char *pos = tiny;
    buf_cursor::Activate(tiny, sizeof(tiny));
    buf_cursor::AnchorPos(&pos);
    (void)Buf_Read<float>(&pos);
    CHECK(!buf_cursor::Failed());
    (void)Buf_Read<float>(&pos);
    (void)Buf_Read<float>(&pos);  // overruns: parent fails
    CHECK(buf_cursor::Failed());

    ByteWriter partsFile = BuildPartsFile();
    bool useBones = false;
    CHECK(RunNestedPartsWindow(partsFile, useBones));

    // The nested window succeeded on its own buffer; the parent
    // must come back exactly as it was: failed.
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(buf_cursor::Failed());
    CHECK(pos == tiny + sizeof(tiny));

    // A failed parent's reads stay latched at zero (no unbounded
    // fallback after restore).
    (void)Buf_Read<float>(&pos);
    CHECK(buf_cursor::Failed());
    CHECK(pos == tiny + sizeof(tiny));
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
}

// ---------------------------------------------------------------------------
// Contract: a nested load that FAILS must not poison the parent scope,
// and a parent that already failed must stay failed across a nested
// window (failure state restores in both directions).
// ---------------------------------------------------------------------------
bool TestNestedFailureStateIsolation()
{
    ByteWriter modelFile = BuildModelFile();
    ExpectNestedFailureLeavesParentClean(modelFile);
    ExpectNestedFailureLatchedParent();
    return true;
}

// The material second pass: per-LOD Advance(2) past numsurfs (surfaces
// come from the separate xmodelsurfs file), then the surface names
// re-read at the rewound offset.
void RunSecondPassLodNameReread()
{
    static const char *const kExpectedSecondPass[] = {
        "mat_first_a", "mat_first_b", "mat_first_c"};
    int nameIndex = 0;
    for (int lod = 0; lod < 2; ++lod)
    {
        buf_cursor::Advance(2);  // skip numsurfs; surfaces come from the
                                 // separate xmodelsurfs file
        CHECK(!buf_cursor::Failed());
        const uint16_t numsurfs = (lod == 0 ? 2 : 1);
        for (uint16_t s = 0; s < numsurfs; ++s)
        {
            char materialName[128];
            CHECK(buf_cursor::ReadString(materialName, sizeof(materialName)));
            CHECK(std::strcmp(materialName, kExpectedSecondPass[nameIndex]) == 0);
            ++nameIndex;
        }
    }
    CHECK(!buf_cursor::Failed());
}

// Checked-seek rejection: a null checkpoint, an out-of-range target
// and an inactive cursor all fail closed without moving the position.
void ExpectSeekRejectsBadCheckpoints()
{
    unsigned char buffer[8] = {};
    unsigned char *probe = buffer;

    // A null checkpoint (no active cursor at Tell() time) must be
    // rejected — and because it is out of range, it latches failed.
    buf_cursor::Activate(buffer, sizeof(buffer));
    buf_cursor::AnchorPos(&probe);
    CHECK(!buf_cursor::SeekTo(nullptr));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::Tell() == buffer);  // did not move
    buf_cursor::Deactivate();

    // Out-of-range target: rejected, latched, position unmoved; a
    // failed cursor rejects further seeks. The bad checkpoint is
    // derived from the cursor's recorded end (one past the legal
    // one-past-end sentinel) rather than pointer arithmetic on the
    // array, which GCC -Warray-bounds rightly rejects.
    buf_cursor::Activate(buffer, sizeof(buffer));
    buf_cursor::AnchorPos(&probe);
    const unsigned char *pastEndRecorded = buf_cursor::Current()->end;
    const unsigned char *pastEnd = pastEndRecorded + 1;
    CHECK(!buf_cursor::SeekTo(pastEnd));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::Tell() == buffer);
    CHECK(!buf_cursor::SeekTo(buffer));
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();

    // Inactive cursor: rejected without touching anything.
    CHECK(!buf_cursor::SeekTo(buffer));
    CHECK(buf_cursor::Current() == nullptr);
}

// Boundary checkpoints are legal (begin and one-past-end), and a
// successful seek re-syncs the anchor.
void ExpectSeekAcceptsBoundaryCheckpoints()
{
    unsigned char buffer[8] = {};
    unsigned char *probe = buffer;
    buf_cursor::Activate(buffer, sizeof(buffer));
    buf_cursor::AnchorPos(&probe);
    buf_cursor::Advance(4);
    CHECK(probe == buffer + 4);
    CHECK(buf_cursor::SeekTo(buffer + sizeof(buffer)));
    CHECK(probe == buffer + sizeof(buffer));
    CHECK(!buf_cursor::Failed());
    CHECK(buf_cursor::SeekTo(buffer));
    CHECK(probe == buffer);
    CHECK(!buf_cursor::Failed());
    buf_cursor::Deactivate();
}

// ---------------------------------------------------------------------------
// Contract: the material second pass must rewind through the checked
// cursor seek — the cursor, the bounded reads and the anchored *pos
// move together, names re-read at the right offset, and bad checkpoints
// fail closed without moving the position.
// ---------------------------------------------------------------------------
bool TestSecondPassCheckedSeek()
{
    ByteWriter modelFile = BuildModelFile();
    unsigned char *pos = modelFile.bytes.data();
    buf_cursor::Activate(modelFile.bytes.data(), modelFile.bytes.size());
    buf_cursor::AnchorPos(&pos);
    buf_cursor::SetStringLimit(64);

    RunConfigAndCollisionHeader(pos);

    // Production checkpoint: v36 = Tell() right after the collision
    // header, BEFORE the first LOD walk.
    const unsigned char *lodTableStart = buf_cursor::Tell();
    CHECK(lodTableStart != nullptr);
    CHECK(lodTableStart == pos);

    // First pass walks the LOD table.
    RunLodTableWalk(pos);
    CHECK(pos != lodTableStart);

    // Second pass: checked rewind, then per-LOD Advance(2) + names.
    CHECK(buf_cursor::SeekTo(lodTableStart));
    CHECK(buf_cursor::Tell() == lodTableStart);
    CHECK(pos == lodTableStart);  // anchored *pos followed the cursor
    RunSecondPassLodNameReread();

    // A third pass rewinds and re-reads identically (warm-cache repeat
    // of the second pass on the same window).
    CHECK(buf_cursor::SeekTo(lodTableStart));
    buf_cursor::Advance(2);
    char repeatName[128];
    CHECK(buf_cursor::ReadString(repeatName, sizeof(repeatName)));
    CHECK(std::strcmp(repeatName, "mat_first_a") == 0);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(buf_cursor::Tell() == nullptr);

    // Checked-seek rejection contracts.
    ExpectSeekRejectsBadCheckpoints();
    ExpectSeekAcceptsBoundaryCheckpoints();
    return true;
}

// Stage: config header truncated mid phys-preset string. The read
// fails through the bounded path and the Deactivate cleanup leaves a
// clean slate.
void ExpectTruncatedConfigHeaderFailsBounded()
{
    ByteWriter truncated;
    truncated.Push16(25);
    truncated.Push8(0x00);
    for (int i = 0; i < 6; ++i)
        truncated.PushFloat(0.0f);
    truncated.Push8('p');  // unterminated string
    unsigned char *pos = truncated.bytes.data();
    buf_cursor::Activate(truncated.bytes.data(), truncated.bytes.size());
    buf_cursor::AnchorPos(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)Buf_Read<unsigned char>(&pos);
    for (int i = 0; i < 6; ++i)
        (void)Buf_Read<float>(&pos);
    char physPreset[64];
    CHECK(!buf_cursor::ReadString(physPreset, sizeof(physPreset)));
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
}

// Stage: parts body truncated inside the classification bulk read —
// the zero-fill contract keeps the destination free of
// uninitialized bytes (the production loader keeps parsing with
// zero-valued reads instead of unwinding).
void ExpectTruncatedClassificationZeroFills()
{
    ByteWriter truncated;
    truncated.Push16(25);
    truncated.Push16(1);
    truncated.Push16(1);
    truncated.Push8(1);
    for (int f = 0; f < 3; ++f)
        truncated.PushFloat(0.0f);
    for (int q = 0; q < 4; ++q)
        truncated.Push16(0);
    truncated.PushString("tag_root");
    truncated.PushString("tag_child");
    truncated.Push8(0);  // one classification byte, need two
    unsigned char *pos = truncated.bytes.data();
    buf_cursor::Activate(truncated.bytes.data(), truncated.bytes.size());
    buf_cursor::AnchorPos(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)Buf_Read<unsigned short>(&pos);
    (void)buf_cursor::ReadWeight();
    buf_cursor::AnchorPos(&pos);
    for (int f = 0; f < 3; ++f)
        (void)Buf_Read<float>(&pos);
    for (int q = 0; q < 4; ++q)
        (void)Buf_Read<unsigned short>(&pos);
    char nameBuf[128];
    CHECK(buf_cursor::ReadString(nameBuf, sizeof(nameBuf)));
    CHECK(std::strcmp(nameBuf, "tag_root") == 0);
    CHECK(buf_cursor::ReadString(nameBuf, sizeof(nameBuf)));
    CHECK(std::strcmp(nameBuf, "tag_child") == 0);
    CHECK(!buf_cursor::Failed());
    unsigned char classification[2] = {0xAB, 0xCD};
    CHECK(!buf_cursor::ReadBytes(classification, sizeof(classification), 2));
    CHECK(buf_cursor::Failed());
    CHECK(classification[0] == 0 && classification[1] == 0);  // zero-filled
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
}

// Cleanup leak check: after all the failure windows above, a fresh
// load must see default limits (a leaked nested limit of 128 would
// wrongly accept bone index 6 here).
void ExpectFreshLoadSeesDefaultLimits()
{
    unsigned char boneBytes[2] = {0x06, 0x00};
    unsigned char *pos = boneBytes;
    buf_cursor::Activate(boneBytes, sizeof(boneBytes));
    buf_cursor::AnchorPos(&pos);
    (void)buf_cursor::ReadBone();
    CHECK(!buf_cursor::Failed());  // default limit accepts 6
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
}

// ---------------------------------------------------------------------------
// Contract: truncated input at each production stage fails through the
// bounded path and the Deactivate cleanup leaves a clean slate — the
// next load on the thread starts from an inactive, un-failed cursor
// with default limits (no leaked failure, no leaked limits, no leaked
// checkpoint).
// ---------------------------------------------------------------------------
bool TestTruncatedInputCleanupContract()
{
    ExpectTruncatedConfigHeaderFailsBounded();
    ExpectTruncatedClassificationZeroFills();
    ExpectFreshLoadSeesDefaultLimits();
    return true;
}

// ---------------------------------------------------------------------------
// Contract: three-level nesting restores strictly LIFO with interleaved
// anchors — the pieces-inside-surfs-inside-model shape. Each level's
// own *pos slot must receive its own restored position.
// ---------------------------------------------------------------------------
bool TestDeepNestedLifoRestoration()
{
    ByteWriter modelFile = BuildModelFile();
    ByteWriter surfsFile = BuildSurfsHeaderFile(1);
    ByteWriter pieceFile;
    pieceFile.Push16(1);   // XModelPieces version
    pieceFile.Push16(0);   // numpieces
    pieceFile.PushString("tag_root");

    unsigned char *modelPos = modelFile.bytes.data();
    buf_cursor::Activate(modelFile.bytes.data(), modelFile.bytes.size());
    buf_cursor::AnchorPos(&modelPos);
    (void)Buf_Read<unsigned short>(&modelPos);  // config version
    const unsigned char *modelCheckpoint = buf_cursor::Tell();

    unsigned char *surfsPos = surfsFile.bytes.data();
    buf_cursor::Activate(surfsFile.bytes.data(), surfsFile.bytes.size());
    buf_cursor::AnchorPos(&surfsPos);
    // Production R_XModelSurfsLoadFile sets these domain bounds on its
    // own scope right after activation.
    buf_cursor::SetBoneLimit(128);
    buf_cursor::SetWeightLimit(4);
    (void)Buf_Read<unsigned short>(&surfsPos);  // surfs version
    const unsigned char *surfsCheckpoint = buf_cursor::Tell();

    unsigned char *piecePos = pieceFile.bytes.data();
    buf_cursor::Activate(pieceFile.bytes.data(), pieceFile.bytes.size());
    buf_cursor::AnchorPos(&piecePos);
    (void)Buf_Read<unsigned short>(&piecePos);  // pieces version
    (void)Buf_Read<unsigned short>(&piecePos);  // numpieces
    char pieceName[64];
    CHECK(buf_cursor::ReadString(pieceName, sizeof(pieceName)));
    CHECK(std::strcmp(pieceName, "tag_root") == 0);
    CHECK(!buf_cursor::Failed());

    // Unwind: pieces -> surfs -> model, each restoring its own parent.
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(buf_cursor::Tell() == surfsCheckpoint);
    CHECK(surfsPos == surfsCheckpoint);
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(buf_cursor::Tell() == modelCheckpoint);
    CHECK(modelPos == modelCheckpoint);
    CHECK(!buf_cursor::Failed());
    // The surfs scope's tightened limits did not leak into the model
    // scope: the restored parent is back on the cursor defaults.
    CHECK(buf_cursor::Current()->maxBoneIdx == 0xFFFFFFFFu);
    CHECK(buf_cursor::Current()->maxWeightIdx == 0xFFFFFFFFu);

    // The model window continues bounded after two nested round trips.
    (void)Buf_Read<unsigned char>(&modelPos);  // config flags
    CHECK(!buf_cursor::Failed());

    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// Contract: the save stack is bounded. One scope past the bound is
// installed pre-failed; while any overflow scope is open the depth is
// pinned at the bound, so every deeper activation overflows too and the
// open-overflow count tracks the frames. An overflow scope's Deactivate
// pops nothing, but it must NOT drop its caller into the unbounded
// legacy fallback either: the overflowed caller's own cursor state was
// destroyed, so its Deactivate leaves it active and pre-failed —
// failure latched, bounded zero reads — until its own Deactivate
// restores the nearest pushed ancestor. The scopes below unwind in
// order and every parent restores its own state, including after a
// sibling re-activation inside the overflowed frame.
// ---------------------------------------------------------------------------
bool TestScopeOverflowFailsClosed()
{
    // Activate 17 nested scopes: the first pushes nothing (top level),
    // scopes 2..17 fill the 16-slot stack exactly.
    struct Probe
    {
        unsigned char buffer[4];
        unsigned char *pos;
    };
    constexpr int kScopes = 17;
    Probe probes[kScopes] = {};
    for (int i = 0; i < kScopes; ++i)
    {
        std::memset(probes[i].buffer, 0, sizeof(probes[i].buffer));
        probes[i].pos = probes[i].buffer;
        buf_cursor::Activate(probes[i].buffer, sizeof(probes[i].buffer));
        buf_cursor::AnchorPos(&probes[i].pos);
        CHECK(buf_cursor::Current() != nullptr);
        // Scopes 1..17 must all be live and un-failed; only the next
        // one (past the bound) is pre-failed.
        CHECK(!buf_cursor::Failed());
    }

    // Scope 18: the save stack is full — installed pre-failed, pops
    // nothing on Deactivate.
    unsigned char overflowBuffer[4] = {};
    buf_cursor::Activate(overflowBuffer, sizeof(overflowBuffer));
    CHECK(buf_cursor::Failed());  // installed pre-failed
    char out[8];
    CHECK(!buf_cursor::ReadString(out, sizeof(out)));  // fails closed

    // Scope 19, nested inside overflow scope 18: the depth is pinned
    // at the bound while an overflow scope is open (overflow scopes
    // push no slot), so this activation overflows too and the open-
    // overflow count tracks both frames.
    unsigned char overflowBuffer2[4] = {};
    buf_cursor::Activate(overflowBuffer2, sizeof(overflowBuffer2));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::ReadWeight() == 0);  // bounded zero read
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();

    // Scope 18's Deactivate pops nothing and must NOT go inactive: the
    // overflowed caller (scope 17) stays active and pre-failed, so
    // Failed() remains latched and its reads stay bounded zeros instead
    // of decaying into the unbounded legacy fallback.
    buf_cursor::Deactivate();
    const buf_cursor::BufCursor *overflowedCaller = buf_cursor::Current();
    CHECK(overflowedCaller != nullptr);  // still active — no unbounded fallback
    CHECK(buf_cursor::Failed());         // failure latched through the caller
    float zero = Buf_Read<float>(&probes[kScopes - 1].pos);
    CHECK(zero == 0.0f);
    CHECK(buf_cursor::Failed());
    // The anchored *pos write-back stays on a real pointer (the pre-
    // failed cursor's empty domain), never nullptr.
    CHECK(probes[kScopes - 1].pos != nullptr);

    // Sibling re-activation accounting: the surviving (overflowed)
    // frame starts another parse window, exactly like a production
    // loader parsing a sibling sub-asset after a failed one. The save
    // depth is still pinned at the bound, so this activation must
    // overflow again — NOT take the top-level path, which would push
    // no save and let its Deactivate steal scope 17's pop, shifting
    // every remaining restore one level (parent receiving grandparent
    // state).
    unsigned char siblingBuffer[4] = {};
    buf_cursor::Activate(siblingBuffer, sizeof(siblingBuffer));
    CHECK(buf_cursor::Failed());  // overflow again, not a fresh top-level cursor
    buf_cursor::Deactivate();
    // Still the same fail-closed caller frame; the save stack did not
    // move.
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(buf_cursor::Failed());

    // Unwind the 17 real scopes: each Deactivate restores its own
    // parent, in order, all un-failed and at their own buffer start.
    for (int i = kScopes - 1; i >= 0; --i)
    {
        buf_cursor::Deactivate();
        if (i == 0)
        {
            // The bottom scope's Deactivate clears the thread-local.
            CHECK(buf_cursor::Current() == nullptr);
            break;
        }
        const buf_cursor::BufCursor *parent = buf_cursor::Current();
        CHECK(parent != nullptr);
        CHECK(parent->begin == probes[i - 1].buffer);
        CHECK(parent->current == probes[i - 1].buffer);
        CHECK(probes[i - 1].pos == probes[i - 1].buffer);
        CHECK(!buf_cursor::Failed());
    }
    CHECK(buf_cursor::Current() == nullptr);
    return true;
}

int RunAll()
{
    CHECK(TestNestedPartsLoadRestoresParent());
    CHECK(TestNestedFailureStateIsolation());
    CHECK(TestSecondPassCheckedSeek());
    CHECK(TestTruncatedInputCleanupContract());
    CHECK(TestDeepNestedLifoRestoration());
    CHECK(TestScopeOverflowFailsClosed());

    std::fprintf(stderr, "xmodel_nested_cursor_test: %d/%d passed\n", g_runs - g_failures, g_runs);
    return g_failures == 0 ? 0 : 1;
}
}  // namespace xmodel_nested_cursor_test

int main()
{
    return xmodel_nested_cursor_test::RunAll();
}
