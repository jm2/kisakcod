// xmodel_cursor_test_support: shared fixtures and check harness for the
// buf_cursor contract suites (ki-okmr / #124).
//
// xmodel_nested_cursor_test.cpp (restore / rewind contracts) and
// xmodel_cursor_overflow_test.cpp (save-stack fail-closed contracts)
// are separate binaries that each include this header, so the
// controlled xmodel fixtures (ByteWriter, BuildModelFile /
// BuildPartsFile / BuildSurfsHeaderFile) and the CHECK harness are
// defined exactly once and stay identical across the suites.
// Header-only by design: each test binary instantiates its own
// Checker, so counters and failure reports never cross binaries.

#ifndef XMODEL_CURSOR_TEST_SUPPORT_H
#define XMODEL_CURSOR_TEST_SUPPORT_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace xmodel_cursor_test_support
{
// Minimal check harness: counts every CHECK, prints failures prefixed
// with the owning suite's label, and reports the pass tally as the
// process exit code. A test TU instantiates this in its anonymous
// namespace and points its CHECK macro at the instance.
struct Checker
{
    const char *label;
    int runs = 0;
    int failures = 0;

    bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
    {
        ++runs;
        if (!cond)
        {
            std::fprintf(stderr, "%s: %s:%d: %s\n", label, file, line, expr);
            ++failures;
            return false;
        }
        return true;
    }

    int Report() const
    {
        std::fprintf(stderr, "%s: %d/%d passed\n", label, runs - failures, runs);
        return failures == 0 ? 0 : 1;
    }
};

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
inline ByteWriter BuildModelFile()
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
inline ByteWriter BuildPartsFile()
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
inline ByteWriter BuildSurfsHeaderFile(short numsurfs)
{
    ByteWriter w;
    w.Push16(25);  // version
    w.Push16(static_cast<uint16_t>(numsurfs));
    return w;
}

}  // namespace xmodel_cursor_test_support

#endif  // XMODEL_CURSOR_TEST_SUPPORT_H
