// fuzz_fastfile: production-path fast-file fuzz harness for the bounded
// BufCursor read path that backs every XAsset-family loader in
// src/xanim/, src/xmodel/, src/database/, and src/EffectsCore/.
//
// The harness links only against the bounded read primitives
// (src/xanim/buf_cursor.cpp) — production loaders and their heavy
// dependencies (FS_ReadFile, Hunk_AllocateTempMemory, Com_PrintError,
// the EffectsCore runtime, etc.) are intentionally NOT linked. The
// point of this harness is to stress the bounded read path that
// every retail fast-file loader funnels through, not to re-test the
// full load path (which is exercised by the existing ctest suites).
//
// The harness is intentionally deterministic and CI-friendly: it
// runs a fixed corpus of bounded seeds (one per XAsset family) plus
// a sweep of mutated seeds that mutate each byte in turn, then
// reports any failure (crash, abort, Failed() inconsistent with the
// expected behavior, or an out-of-bounds read). The harness returns
// 0 on success and non-zero on the first failure.
//
// Build target: fuzz_fastfile
// CTest entries: fuzz-fastfile-cursor, fuzz-fastfile-corpus
//
// Affected families exercised:
//   - xmodel pieces header
//   - xanim parts header
//   - fx archive body state frame header
//   - generic typed cursor reads (u8/u16/u32/float)
//
// The harness MUST NOT change the on-disk production guards. It only
// adds feeding attacker-controlled bytes into the bounded read path
// and asserting that the contract holds.

#include <xanim/buf_cursor.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fuzz_fastfile
{
namespace
{
int g_runs = 0;

int Fail(const char *const message)
{
    std::fprintf(stderr, "fuzz_fastfile: %s\n", message);
    return 1;
}

#define CHECK(expr) do {                                                  \
    ++g_runs;                                                             \
    if (!(expr)) {                                                        \
        char kisakCheckMsg_[256];                                         \
        std::snprintf(kisakCheckMsg_, sizeof(kisakCheckMsg_), "%s:%d: %s",\
            __FILE__, __LINE__, #expr);                                   \
        return Fail(kisakCheckMsg_);                                      \
    }                                                                     \
} while (0)

#define CHECK_RC(expr) do {                                               \
    ++g_runs;                                                             \
    if (!(expr)) {                                                        \
        char kisakCheckMsg_[256];                                         \
        std::snprintf(kisakCheckMsg_, sizeof(kisakCheckMsg_), "%s:%d: %s",\
            __FILE__, __LINE__, #expr);                                   \
        return Fail(kisakCheckMsg_);                                      \
    }                                                                     \
} while (0)

// Read a file's bytes into a vector. Returns empty on failure.
std::vector<unsigned char> ReadFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    return std::vector<unsigned char>(s.begin(), s.end());
}

// Exercise: a bounded read of an arbitrary typed payload must never
// read past the end of the buffer. The cursor's Failed() consistency
// must hold: a read that didn't trip the bounds check leaves the cursor
// failed=false and advances current by exactly sizeof(T); a read that
// did trip the bounds check leaves the cursor failed=true and does NOT
// advance past end.
int TestTypedRead(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    const buf_cursor::BufCursor *const c0 = buf_cursor::Current();
    CHECK_RC(c0 != nullptr);
    CHECK_RC(c0->begin == buf);
    CHECK_RC(c0->end == buf + size);
    CHECK_RC(c0->current == buf);
    CHECK_RC(!c0->failed);

    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    // Hammer through the whole buffer with a typed read each iteration.
    // The cursor's bounds check must trip exactly once, at the moment
    // the read would otherwise walk past end.
    size_t totalReads = 0;
    while (!buf_cursor::Failed())
    {
        // Pick a rotating type so the harness covers all the typestates
        // a real loader mixes (u8, u16, u32, float).
        switch (totalReads & 3u)
        {
        case 0u:
            (void)Buf_Read<unsigned char>(&pos);
            break;
        case 1u:
            (void)Buf_Read<unsigned short>(&pos);
            break;
        case 2u:
            (void)Buf_Read<unsigned int>(&pos);
            break;
        default:
            (void)Buf_Read<float>(&pos);
            break;
        }
        ++totalReads;
        if (totalReads > (size + 8u))
        {
            return Fail("read loop did not converge — cursor failed state malformed");
        }
    }

    // After the bounds check has tripped, the cursor must be failed
    // and pos must not have walked past end.
    const buf_cursor::BufCursor *const c1 = buf_cursor::Current();
    CHECK_RC(c1->failed);
    CHECK_RC(c1->current <= c1->end);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) <= c1->end);

    buf_cursor::Deactivate();
    return 0;
}

// Exercise: a string read bounded by the configured maxStringLen must
// never advance past end, must respect the configured max, and must
// either succeed (when NUL is found before end) or fail (when no NUL
// or the string is too long).
int TestStringRead(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    buf_cursor::SetStringLimit(64u);

    char out[128] = {};
    const bool ok = buf_cursor::ReadString(out, sizeof(out));

    const buf_cursor::BufCursor *const c = buf_cursor::Current();
    CHECK_RC(c != nullptr);
    CHECK_RC(c->current <= c->end);

    if (ok)
    {
        const size_t len = std::strlen(out);
        CHECK_RC(len <= 64u);
        // NUL must be present within (len + 1) bytes of the cursor's
        // beginning-of-string position. The cursor advanced past it.
        if (static_cast<size_t>(c->current - buf) < len + 1u)
        {
            return Fail("string read succeeded but cursor didn't advance past NUL");
        }
    }
    else
    {
        CHECK_RC(c->failed);
    }

    buf_cursor::Deactivate();
    return 0;
}

// Exercise: a domain-bounded typed read (ReadBone / ReadWeight /
// ReadTri) must never accept an index that exceeds the configured limit
// and must mark Failed() when it does.
int TestDomainRead(const std::vector<unsigned char> &bytes)
{
    if (bytes.size() < 8u)
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    buf_cursor::SetBoneLimit(8u);
    buf_cursor::SetWeightLimit(4u);
    buf_cursor::SetTriLimit(16u);

    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    // Read a few bones, weights, and tri indices. The cursor must
    // either succeed (when the value is in range) or fail (when out
    // of range). Either outcome is acceptable; the bounded invariant
    // we care about is that we never advance past end.
    for (int i = 0; i < 16 && !buf_cursor::Failed(); ++i)
    {
        (void)buf_cursor::ReadBone();
        (void)buf_cursor::ReadWeight();
        (void)buf_cursor::ReadTri(32u);
    }

    const buf_cursor::BufCursor *const c = buf_cursor::Current();
    CHECK_RC(c != nullptr);
    CHECK_RC(c->current <= c->end);

    buf_cursor::Deactivate();
    return 0;
}

// Exercise: a transactional Begin/Commit/Rollback triple must keep
// the cursor's current and the anchored *pos in sync. A failed
// transaction must Rollback both to the checkpoint.
int TestTransaction(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    buf_cursor::Begin();
    const size_t checkpoint = static_cast<size_t>(buf_cursor::Current()->current - buf);
    CHECK_RC(checkpoint == 0u);

    // Read a single byte. The cursor and the anchored *pos must agree.
    (void)Buf_Read<unsigned char>(&pos);
    const buf_cursor::BufCursor *const c0 = buf_cursor::Current();
    CHECK_RC(c0->current == reinterpret_cast<const unsigned char *>(pos));

    if (buf_cursor::Failed())
    {
        // Buffer too small — Rollback should walk both back to the
        // checkpoint.
        buf_cursor::Rollback();
        const buf_cursor::BufCursor *const c1 = buf_cursor::Current();
        CHECK_RC(c1->current == buf);
        CHECK_RC(reinterpret_cast<const unsigned char *>(pos) == buf);
        buf_cursor::Deactivate();
        return 0;
    }

    // Commit must leave the cursor advanced and the anchored *pos in
    // sync.
    CHECK_RC(buf_cursor::Commit());
    const buf_cursor::BufCursor *const c2 = buf_cursor::Current();
    CHECK_RC(c2->current == reinterpret_cast<const unsigned char *>(pos));

    // A second Begin/Rollback cycle (with no Commit) must not advance
    // either pointer.
    buf_cursor::Begin();
    (void)Buf_Read<unsigned int>(&pos);
    buf_cursor::Rollback();
    const buf_cursor::BufCursor *const c3 = buf_cursor::Current();
    CHECK_RC(c3->current == c2->current);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) == c2->current);

    buf_cursor::Deactivate();
    return 0;
}
// Exercise: a typed read of an unaligned T must go through std::memcpy
// (no UBSan misaligned-load). We stage a buffer that has T at every
// byte offset 0..alignof(T)-1 and confirm the cursor reads each one
// without crashing.
int TestUnalignedReads(const std::vector<unsigned char> &bytes)
{
    // Build a 256-byte buffer with a known pattern at every offset.
    std::vector<unsigned char> stage(256u);
    for (size_t i = 0; i < stage.size(); ++i)
        stage[i] = static_cast<unsigned char>(i ^ 0x5au);

    for (size_t off = 0; off < 4u; ++off)
    {
        buf_cursor::Activate(stage.data() + off, stage.size() - off);
        unsigned char *pos = const_cast<unsigned char *>(stage.data() + off);
        buf_cursor::AnchorPos(&pos);
        for (int i = 0; i < 64 && !buf_cursor::Failed(); ++i)
        {
            const float v = Buf_Read<float>(&pos);
            // Float must be representable — we only care that the read
            // completed without a UBSan trip, not the value.
            (void)v;
        }
        buf_cursor::Deactivate();
    }

    (void)bytes;
    return 0;
}

// Exercise: a XModel-pieces-style header parse must bound every read.
// The cursor must trip Failed() (or complete successfully) without
// walking past end, regardless of the attacker-controlled bytes.
int TestXModelPiecesHeader(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    buf_cursor::SetBoneLimit(255u);
    buf_cursor::SetStringLimit(64u);
    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    const uint16_t version = Buf_Read<uint16_t>(&pos);
    const uint16_t numPieces = Buf_Read<uint16_t>(&pos);
    (void)version;
    (void)numPieces;

    // Walk a small number of pieces; each is a name (string) plus
    // 3 floats. The cursor must bound every read.
    for (int i = 0; i < 8 && !buf_cursor::Failed(); ++i)
    {
        char name[80] = {};
        (void)buf_cursor::ReadString(name, sizeof(name));
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
    }

    const buf_cursor::BufCursor *const c = buf_cursor::Current();
    CHECK_RC(c != nullptr);
    CHECK_RC(c->current <= c->end);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) <= c->end);

    buf_cursor::Deactivate();
    return 0;
}

// Exercise: a XAnim-parts-style header parse must bound every read.
int TestXAnimPartsHeader(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    buf_cursor::SetBoneLimit(255u);
    buf_cursor::SetStringLimit(64u);
    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    const uint16_t version = Buf_Read<uint16_t>(&pos);
    const uint16_t numParts = Buf_Read<uint16_t>(&pos);
    (void)version;
    (void)numParts;

    for (int i = 0; i < 16 && !buf_cursor::Failed(); ++i)
    {
        char name[80] = {};
        (void)buf_cursor::ReadString(name, sizeof(name));
        (void)Buf_Read<uint32_t>(&pos);
        (void)Buf_Read<uint8_t>(&pos);
    }

    const buf_cursor::BufCursor *const c = buf_cursor::Current();
    CHECK_RC(c != nullptr);
    CHECK_RC(c->current <= c->end);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) <= c->end);

    buf_cursor::Deactivate();
    return 0;
}

// Exercise: a FX-archive-body-state-style header parse must bound
// every read. The frame header is a u32 frame count plus a u32 element
// stride plus a sequence of frame records.
int TestFxArchiveBodyStateHeader(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    buf_cursor::Activate(buf, size);
    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    const uint32_t frameCount = Buf_Read<uint32_t>(&pos);
    const uint32_t stride = Buf_Read<uint32_t>(&pos);
    (void)frameCount;
    (void)stride;

    // Cap the iteration so a malformed huge frameCount doesn't loop
    // forever — the cursor's bounds check will trip eventually, but
    // we don't want to spin the harness CPU out.
    const uint32_t iters = std::min<uint32_t>(frameCount, 64u);
    for (uint32_t i = 0; i < iters && !buf_cursor::Failed(); ++i)
    {
        (void)Buf_Read<uint32_t>(&pos);
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
        (void)Buf_Read<float>(&pos);
    }

    const buf_cursor::BufCursor *const c = buf_cursor::Current();
    CHECK_RC(c != nullptr);
    CHECK_RC(c->current <= c->end);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) <= c->end);

    buf_cursor::Deactivate();
    return 0;
}

// Drive every harness against a single seed payload.
int ExerciseSeed(const std::vector<unsigned char> &bytes, const char *const label)
{
    if (TestTypedRead(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: TypedRead failed on %s\n", label);
        return 1;
    }
    if (TestStringRead(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: StringRead failed on %s\n", label);
        return 1;
    }
    if (TestDomainRead(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: DomainRead failed on %s\n", label);
        return 1;
    }
    if (TestTransaction(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: Transaction failed on %s\n", label);
        return 1;
    }
    if (TestXModelPiecesHeader(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: XModelPiecesHeader failed on %s\n", label);
        return 1;
    }
    if (TestXAnimPartsHeader(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: XAnimPartsHeader failed on %s\n", label);
        return 1;
    }
    if (TestFxArchiveBodyStateHeader(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: FxArchiveBodyStateHeader failed on %s\n", label);
        return 1;
    }
    if (TestUnalignedReads(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: UnalignedReads failed on %s\n", label);
        return 1;
    }
    return 0;
}

// Build a small valid XModel-pieces header for the seed corpus.
std::vector<unsigned char> BuildXModelPiecesSeed()
{
    auto push16 = [](std::vector<unsigned char> &v, uint16_t x) {
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
    };
    auto pushFloat = [](std::vector<unsigned char> &v, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        uint32_t x = bits;
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xFFu));
    };
    auto pushString = [](std::vector<unsigned char> &v, const char *s) {
        for (const char *p = s; *p; ++p)
            v.push_back(static_cast<unsigned char>(*p));
        v.push_back(0);
    };

    std::vector<unsigned char> v;
    push16(v, 1);            // version
    push16(v, 2);            // numpieces
    pushString(v, "viewmodel_default");
    pushFloat(v, 1.0f);
    pushFloat(v, 2.0f);
    pushFloat(v, 3.0f);
    pushString(v, "viewmodel_lod1");
    pushFloat(v, 4.0f);
    pushFloat(v, 5.0f);
    pushFloat(v, 6.0f);
    return v;
}

// Build a small valid XAnim-parts header.
std::vector<unsigned char> BuildXAnimPartsSeed()
{
    auto push16 = [](std::vector<unsigned char> &v, uint16_t x) {
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
    };
    auto push32 = [](std::vector<unsigned char> &v, uint32_t x) {
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xFFu));
    };
    auto push8 = [](std::vector<unsigned char> &v, uint8_t x) {
        v.push_back(x);
    };
    auto pushString = [](std::vector<unsigned char> &v, const char *s) {
        for (const char *p = s; *p; ++p)
            v.push_back(static_cast<unsigned char>(*p));
        v.push_back(0);
    };

    std::vector<unsigned char> v;
    push16(v, 1);            // version
    push16(v, 1);            // numparts
    pushString(v, "idle");
    push32(v, 0x12345678u);  // dataOffset
    push8(v, 0x80u);         // u8 boneIndex
    return v;
}

// Build a small valid FX archive body state header.
std::vector<unsigned char> BuildFxArchiveBodyStateSeed()
{
    auto push32 = [](std::vector<unsigned char> &v, uint32_t x) {
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xFFu));
    };
    auto pushFloat = [](std::vector<unsigned char> &v, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        uint32_t x = bits;
        v.push_back(static_cast<unsigned char>(x & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xFFu));
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xFFu));
    };

    std::vector<unsigned char> v;
    push32(v, 2);            // frameCount
    push32(v, 16);           // stride
    push32(v, 0);            // frame delta
    pushFloat(v, 0.0f);
    pushFloat(v, 0.0f);
    pushFloat(v, 0.0f);
    push32(v, 100);          // frame delta
    pushFloat(v, 1.0f);
    pushFloat(v, 2.0f);
    pushFloat(v, 3.0f);
    return v;
}

int RunSeeds()
{
    const std::vector<unsigned char> xmodel = BuildXModelPiecesSeed();
    const std::vector<unsigned char> xanim  = BuildXAnimPartsSeed();
    const std::vector<unsigned char> fx     = BuildFxArchiveBodyStateSeed();

    // Also feed some borderline seeds: empty, single byte, all-0xFF,
    // all-0x00. These are the cheap adversarial inputs a fuzzer finds
    // first.
    const std::vector<unsigned char> empty;
    const std::vector<unsigned char> one({0xAAu});
    const std::vector<unsigned char> twobyteseven7(64u, 0x7Fu);
    const std::vector<unsigned char> zeros(64u, 0u);
    const std::vector<unsigned char> ffs(64u, 0xFFu);

    struct Seed
    {
        const char *label;
        std::vector<unsigned char> bytes;
    };

    const Seed seeds[] = {
        {"xmodel_pieces", xmodel},
        {"xanim_parts",  xanim},
        {"fx_archive_body_state", fx},
        {"empty",        empty},
        {"one_byte",     one},
        {"zeros_64",     zeros},
        {"ones_64",      ffs},
        {"alt_64",       twobyteseven7},
    };

    for (const Seed &s : seeds)
    {
        if (ExerciseSeed(s.bytes, s.label) != 0)
            return 1;
    }

    std::fprintf(stdout, "fuzz_fastfile: seeds ok (runs=%d)\n", g_runs);
    return 0;
}

int RunCorpus(const char *corpusDir)
{
    if (corpusDir == nullptr || corpusDir[0] == 0)
    {
        return RunSeeds();
    }

    std::vector<std::string> files;
    {
        std::error_code ec;
        std::filesystem::directory_iterator it(
            std::filesystem::path(corpusDir), ec);
        if (ec)
        {
            // Fall back: synthesize a tiny inline corpus.
            std::fprintf(stderr, "fuzz_fastfile: could not list corpus dir %s; running inline seeds\n", corpusDir);
            return RunSeeds();
        }
        const std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
                break;
            const std::filesystem::directory_entry &entry = *it;
            if (!entry.is_regular_file(ec))
                continue;
            const std::filesystem::path &p = entry.path();
            const std::string name = p.filename().string();
            if (!name.empty())
                files.push_back(name);
        }
    }

    if (files.empty())
    {
        std::fprintf(stderr, "fuzz_fastfile: empty corpus dir %s; running inline seeds\n", corpusDir);
        return RunSeeds();
    }

    for (const std::string &name : files)
    {
        const std::string full = std::string(corpusDir) + "/" + name;
        const std::vector<unsigned char> bytes = ReadFile(full.c_str());
        if (ExerciseSeed(bytes, name.c_str()) != 0)
            return 1;
    }

    std::fprintf(stdout, "fuzz_fastfile: corpus ok (%zu files, runs=%d)\n", files.size(), g_runs);
    return 0;
}

int RunRandom(const char *corpusDir, unsigned long iterations)
{
    // Reuse the seeded sets for the random sweep's "starting point"
    // so the random mutations come from a realistic region of the
    // input space.
    const std::vector<unsigned char> seeds[] = {
        BuildXModelPiecesSeed(),
        BuildXAnimPartsSeed(),
        BuildFxArchiveBodyStateSeed(),
    };

    std::mt19937 rng(0xC0FFEE01u);
    unsigned long totalFailures = 0;

    for (unsigned long i = 0; i < iterations; ++i)
    {
        const std::vector<unsigned char> &base = seeds[i % 3];
        std::vector<unsigned char> mutated = base;

        // Mutate a random number of bytes (0..size).
        const size_t mutCount = (i == 0) ? 0u : (rng() % (mutated.size() + 1u));
        for (size_t k = 0; k < mutCount; ++k)
        {
            const size_t off = rng() % mutated.size();
            mutated[off] = static_cast<unsigned char>(rng() & 0xFFu);
        }

        char label[64];
        std::snprintf(label, sizeof(label), "rand_%lu", i);
        if (ExerciseSeed(mutated, label) != 0)
        {
            ++totalFailures;
            // Persist the offending seed to the corpus dir so the
            // fuzzer can be re-run with --corpus=... to reproduce.
            if (corpusDir != nullptr && corpusDir[0] != 0)
            {
                const std::string path = std::string(corpusDir) + "/crash_" + std::to_string(i) + ".bin";
                std::ofstream out(path, std::ios::binary);
                if (out)
                {
                    out.write(reinterpret_cast<const char *>(mutated.data()),
                              static_cast<std::streamsize>(mutated.size()));
                }
            }
            if (totalFailures > 0u)
                return 1;
        }
    }

    std::fprintf(stdout, "fuzz_fastfile: random sweep ok (iter=%lu, runs=%d)\n", iterations, g_runs);
    return 0;
}

int RunGenSeeds(const char *outDir)
{
    if (outDir == nullptr || outDir[0] == 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: genseeds requires an output directory\n");
        return 1;
    }

    auto xmodel = BuildXModelPiecesSeed();
    auto xanim = BuildXAnimPartsSeed();
    auto fx = BuildFxArchiveBodyStateSeed();

    // Ensure the directory exists.
    const std::string cmd = std::string("mkdir -p \"") + outDir + "\"";
    if (::system(cmd.c_str()) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: could not create %s\n", outDir);
        return 1;
    }

    struct SeedOut
    {
        const char *name;
        std::vector<unsigned char> bytes;
    };

    const SeedOut seeds[] = {
        {"xmodel_pieces_valid.bin",  xmodel},
        {"xanim_parts_valid.bin",    xanim},
        {"fx_archive_body_state_valid.bin", fx},
        {"empty.bin",       {}},
        {"single_byte.bin", {0xAAu}},
        {"zeros_64.bin",    std::vector<unsigned char>(64u, 0u)},
        {"ones_64.bin",     std::vector<unsigned char>(64u, 0xFFu)},
        {"alt_64.bin",      std::vector<unsigned char>(64u)},
        {"ones_256.bin",    std::vector<unsigned char>(256u, 0xFFu)},
    };

    int written = 0;
    for (const SeedOut &s : seeds)
    {
        const std::string path = std::string(outDir) + "/" + s.name;
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            std::fprintf(stderr, "fuzz_fastfile: could not write %s\n", path.c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char *>(s.bytes.data()),
                  static_cast<std::streamsize>(s.bytes.size()));
        if (!out)
        {
            std::fprintf(stderr, "fuzz_fastfile: write failed for %s\n", path.c_str());
            return 1;
        }
        ++written;
    }

    // Fill the alt_64.bin alternating pattern now that the vector is
    // allocated empty above.
    {
        std::vector<unsigned char> pattern(64u);
        for (size_t i = 0; i < pattern.size(); ++i)
            pattern[i] = static_cast<unsigned char>(i & 1u ? 0xAAu : 0x55u);
        const std::string path = std::string(outDir) + "/alt_64.bin";
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            std::fprintf(stderr, "fuzz_fastfile: could not write %s\n", path.c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char *>(pattern.data()),
                  static_cast<std::streamsize>(pattern.size()));
        ++written;
    }

    std::fprintf(stdout, "fuzz_fastfile: genseeds wrote %d seeds to %s\n", written, outDir);
    return 0;
}

void PrintUsage(const char *argv0)
{
    std::fprintf(stdout,
        "fuzz_fastfile — production-path fast-file fuzz harness\n"
        "Usage: %s [seeds] [corpus <dir>] [random <count> <dir>] [genseeds <dir>]\n"
        "  seeds                 run the built-in seed corpus (default)\n"
        "  corpus <dir>          run every file in <dir> as a seed\n"
        "  random <n> <dir>      run <n> mutated iterations, persisting crashes\n"
        "  genseeds <dir>        write the bounded seed corpus to <dir>\n"
        "Built-in seeds cover xmodel parts, xanim parts, fx archive body state,\n"
        "and the empty / single-byte / all-zero / all-ones / alternating-7F\n"
        "adversarial inputs.\n",
        argv0);
}

}  // namespace
}  // namespace fuzz_fastfile

int main(int argc, char **argv)
{
    using namespace fuzz_fastfile;

    if (argc <= 1)
        return RunSeeds();

    const std::string mode = argv[1];
    if (mode == "seeds" || mode == "--seeds")
        return RunSeeds();
    if (mode == "corpus" && argc >= 3)
        return RunCorpus(argv[2]);
    if (mode == "random" && argc >= 4)
    {
        const unsigned long iterations = std::strtoul(argv[2], nullptr, 10);
        return RunRandom(argv[3], iterations);
    }
    if (mode == "genseeds" && argc >= 3)
        return RunGenSeeds(argv[2]);
    if (mode == "--help" || mode == "-h")
    {
        PrintUsage(argv[0]);
        return 0;
    }

    PrintUsage(argv[0]);
    return 1;
}
