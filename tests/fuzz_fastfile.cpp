// fuzz_fastfile: bounded cursor-primitive fuzz harness. It feeds
// synthetic XAsset-shaped byte streams (xmodel pieces header shapes,
// xanim parts header shapes, fx archive body state shapes, plus
// adversarial and truncated variants) through the bounded BufCursor
// read primitives in src/xanim/buf_cursor.cpp.
//
// This is a PRIMITIVE harness, not production-parser coverage. It does
// NOT link or drive the real DB loader, the production XModel/XAnim
// parsers, or FX restore composition — those consumers and their heavy
// dependencies (FS_ReadFile, Hunk_AllocateTempMemory, Com_PrintError,
// the EffectsCore runtime, etc.) are intentionally NOT linked here. The
// byte layouts below are synthetic sketches of the read patterns; a
// green run does not establish production-loader safety. Real parser /
// loader enrollment is tracked under issue #125 (A03) and gated on the
// load-object migration in #124 (A02).
//
// Keeping the harness decoupled from production code is deliberate: it
// stays available on every build target and pins the bounded read
// contract independently of the loader rewrite.
//
// The harness is deterministic and CI-friendly: the `seeds` mode runs
// the built-in inline corpus, and the `corpus <dir>` mode requires a
// generated corpus with a checked manifest and fails closed when the
// corpus is absent, empty, unreadable, or manifest-mismatched (it never
// falls back to the inline seeds). It reports any failure (crash,
// abort, Failed() inconsistent with the expected behavior, or an
// out-of-bounds read) and returns non-zero on the first failure.
//
// Build target: fuzz_fastfile
// CTest entries: fuzz-fastfile-cursor (inline seeds),
//   fuzz-fastfile-corpus-setup / fuzz-fastfile-corpus (manifest corpus),
//   fuzz-fastfile-corpus-rejects-missing and fuzz-fastfile-corpus-gate
//   (fail-closed negative gates)
//
// Synthetic header shapes exercised:
//   - xmodel pieces header
//   - xanim parts header
//   - fx archive body state frame header
//   - generic typed cursor reads (u8/u16/u32/float)
//
// The harness MUST NOT change the on-disk production guards. It only
// feeds attacker-controlled bytes into the bounded read path and
// asserts that the contract holds.

#include <xanim/buf_cursor.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
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
        char fuzzFastfileFailureMessage[256];                             \
        std::snprintf(fuzzFastfileFailureMessage,                         \
            sizeof(fuzzFastfileFailureMessage), "%s:%d: %s",             \
            __FILE__, __LINE__, #expr);                                   \
        (void)Fail(fuzzFastfileFailureMessage);                           \
        return 1;                                                         \
    }                                                                     \
} while (0)

#define CHECK_RC(expr) do {                                               \
    ++g_runs;                                                             \
    if (!(expr)) {                                                        \
        char fuzzFastfileFailureMessage[256];                             \
        std::snprintf(fuzzFastfileFailureMessage,                         \
            sizeof(fuzzFastfileFailureMessage), "%s:%d: %s",             \
            __FILE__, __LINE__, #expr);                                   \
        (void)Fail(fuzzFastfileFailureMessage);                           \
        return 1;                                                         \
    }                                                                     \
} while (0)

// 64-bit FNV-1a over the seed bytes. This is an integrity / drift check
// for the on-disk corpus manifest, not a cryptographic digest: it makes
// a silently truncated or edited seed fail the corpus gate instead of
// being exercised under a stale manifest entry.
uint64_t Fnv1a64(const std::vector<unsigned char> &bytes)
{
    uint64_t h = 14695981039346656037ull; // FNV-1a 64-bit offset basis
    for (const unsigned char b : bytes)
    {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}

// Strict file read that distinguishes "empty file" (success) from
// "missing/unreadable/non-regular file" (failure). The existing ReadFile
// conflates the two, which is exactly the ambiguity the corpus gate must
// not inherit.
bool ReadFileChecked(const std::string &path, std::vector<unsigned char> &out)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec) || ec)
        return false;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad())
        return false;

    const std::string s = ss.str();
    out.assign(s.begin(), s.end());
    return true;
}

// Corpus manifest entry. `name` is always a bare filename; `kind` is the
// case classification the tracker records ("valid" or "malformed").
struct CorpusEntry
{
    std::string name;
    std::string kind;
    uint64_t size = 0;
    uint64_t hash = 0;
};

constexpr const char *kCorpusManifestName = "fuzz_seeds.manifest";

// A manifest entry name must be a bare filename: no separators, no `.` /
// `..`. Without this a crafted manifest could point the corpus reader at
// an arbitrary path.
bool IsSafeEntryName(const std::string &name)
{
    if (name.empty() || name == "." || name == "..")
        return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    const std::filesystem::path p(name);
    return p.filename().string() == name;
}

// The manifest grammar is exact: `fnv1a64-hex size kind name`, one
// space-separated token per field and nothing after the name. The hash is
// a fixed 16-hex-digit FNV-1a value, the size is an unsigned decimal that
// must fit uint64_t, and the kind is one of the tracked classifications.
// Parsing rejects signs, trailing garbage, partial conversions and
// overflow so a mutated manifest cannot slip past the checked corpus gate.

// Digit class predicates shared by the manifest field parsers. Hand-rolled
// ASCII checks keep the classification locale-independent.
bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsDecimalDigit(char c)
{
    return c >= '0' && c <= '9';
}

// Strict fixed-width hex parse for the 64-bit FNV-1a hash field. Requires
// exactly 16 hex digits (the generator always emits `%016llx`), rejects a
// sign or `0x` prefix, and verifies the whole token was consumed.
bool ParseManifestHash(const std::string &token, uint64_t &out)
{
    if (token.size() != 16)
        return false;
    for (const char c : token)
    {
        if (!IsHexDigit(c))
            return false;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long value = std::strtoull(token.c_str(), &end, 16);
    if (end != token.c_str() + token.size() || errno == ERANGE)
        return false;
    out = static_cast<uint64_t>(value);
    return true;
}

// Strict unsigned decimal parse for the size field. Rejects signs, empty
// or non-digit tokens, trailing garbage, and uint64_t overflow that
// strtoull would otherwise silently clamp to ULLONG_MAX.
bool ParseManifestSize(const std::string &token, uint64_t &out)
{
    if (token.empty())
        return false;
    for (const char c : token)
    {
        if (!IsDecimalDigit(c))
            return false;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long value = std::strtoull(token.c_str(), &end, 10);
    if (end != token.c_str() + token.size() || errno == ERANGE)
        return false;
    out = static_cast<uint64_t>(value);
    return true;
}

// The only classifications the manifest tracker records.
bool IsValidCorpusKind(const std::string &kind)
{
    return kind == "valid" || kind == "malformed";
}

// Parse one manifest data line into `entry`, reporting the specific
// violation. Returns false on any grammar or field-value failure; the
// caller fails closed. Every field is validated strictly and a line
// carrying extra tokens is rejected.
bool ParseManifestLine(const std::string &line, CorpusEntry &entry)
{
    std::istringstream ls(line);
    std::string hashHex;
    std::string sizeStr;
    std::string kind;
    std::string name;
    std::string extra;
    if (!(ls >> hashHex >> sizeStr >> kind >> name))
    {
        std::fprintf(stderr, "fuzz_fastfile: malformed manifest line: %s\n", line.c_str());
        return false;
    }
    if (ls >> extra)
    {
        std::fprintf(stderr, "fuzz_fastfile: manifest line has trailing fields: %s\n",
                     line.c_str());
        return false;
    }
    if (!ParseManifestHash(hashHex, entry.hash))
    {
        std::fprintf(stderr, "fuzz_fastfile: malformed manifest hash: %s\n", hashHex.c_str());
        return false;
    }
    if (!ParseManifestSize(sizeStr, entry.size))
    {
        std::fprintf(stderr, "fuzz_fastfile: malformed manifest size: %s\n", sizeStr.c_str());
        return false;
    }
    if (!IsValidCorpusKind(kind))
    {
        std::fprintf(stderr, "fuzz_fastfile: unknown corpus kind: %s\n", kind.c_str());
        return false;
    }
    entry.kind = kind;
    entry.name = name;
    return true;
}

// Load and parse the corpus manifest. Returns false on a missing,
// unreadable, or malformed manifest; the caller fails closed.
bool LoadCorpusManifest(const std::string &dir, std::vector<CorpusEntry> &entries)
{
    const std::string path = dir + "/" + kCorpusManifestName;
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::fprintf(stderr, "fuzz_fastfile: corpus manifest missing: %s\n", path.c_str());
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        CorpusEntry entry;
        if (!ParseManifestLine(line, entry))
            return false;
        entries.push_back(entry);
    }

    if (in.bad())
    {
        std::fprintf(stderr, "fuzz_fastfile: manifest read error: %s\n", path.c_str());
        return false;
    }
    return true;
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

// Exercise: a bulk ReadBytes sequence mirroring the load-object index /
// bitmap copies must never read past end, never write past outCapacity,
// and never expose uninitialized destination bytes. Overruns must zero
// the requested span, mark the cursor failed, and leave the position
// pinned; subsequent reads (typed or bulk) must be no-ops.
int TestBulkRead(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return 0;

    const unsigned char *const buf = bytes.data();
    const size_t size = bytes.size();

    // Padded destination so the harness can prove nothing is written
    // past the capacity handed to ReadBytes.
    unsigned char out[192];
    std::memset(out, 0xA7u, sizeof(out));

    buf_cursor::Activate(buf, size);
    unsigned char *pos = const_cast<unsigned char *>(buf);
    buf_cursor::AnchorPos(&pos);

    // Sweep byte counts around the buffer's edges: exact remainder,
    // remainder +/- 1, and a couple of oversized counts.
    const size_t counts[] = {
        1u, 3u, 8u, size, size ? size - 1u : 0u, size + 1u, size + 16u, 64u};
    size_t totalConsumed = 0;
    for (const size_t count : counts)
    {
        if (buf_cursor::Failed())
            break;
        std::memset(out, 0xA7u, sizeof(out));
        const bool ok = buf_cursor::ReadBytes(out, sizeof(out), count);
        const buf_cursor::BufCursor *const c = buf_cursor::Current();
        CHECK_RC(c != nullptr);
        if (ok)
        {
            // A successful bulk read must advance exactly count bytes
            // and keep the destination prefix identical to the source.
            CHECK_RC(c->failed == false);
            CHECK_RC(static_cast<size_t>(c->current - c->begin) == totalConsumed + count);
            CHECK_RC(c->current <= c->end);
            CHECK_RC(std::memcmp(out, c->begin + totalConsumed, count) == 0);
            totalConsumed += count;
        }
        else
        {
            // A failed bulk read must zero the requested span, pin the
            // cursor, and leave the sentinel past the zeroed region.
            const size_t zeroed = count < sizeof(out) ? count : sizeof(out);
            for (size_t i = 0; i < zeroed; ++i)
                CHECK_RC(out[i] == 0u);
            CHECK_RC(c->failed);
            CHECK_RC(static_cast<size_t>(c->current - c->begin) == totalConsumed);
            if (zeroed < sizeof(out))
                CHECK_RC(out[zeroed] == 0xA7u);
        }
        CHECK_RC(reinterpret_cast<const unsigned char *>(pos) == c->current);
        // Post-failure reads must be no-ops; a success after failure is
        // a contract violation.
        if (c->failed)
        {
            unsigned char post[8];
            std::memset(post, 0x5Eu, sizeof(post));
            CHECK_RC(!buf_cursor::ReadBytes(post, sizeof(post), sizeof(post)));
            CHECK_RC(post[0] == 0u);
            const unsigned int postTyped = Buf_Read<unsigned int>(&pos);
            CHECK_RC(postTyped == 0u);
            CHECK_RC(reinterpret_cast<const unsigned char *>(pos) == c->current);
        }
        if (totalConsumed > size)
            return Fail("bulk read loop consumed past end");
    }

    const buf_cursor::BufCursor *const cEnd = buf_cursor::Current();
    CHECK_RC(cEnd->current <= cEnd->end);
    CHECK_RC(reinterpret_cast<const unsigned char *>(pos) <= cEnd->end);

    // Capacity enforcement: a count above outCapacity must fail closed
    // even when the buffer still has bytes left.
    buf_cursor::Deactivate();
    if (size >= 8u)
    {
        buf_cursor::Activate(buf, size);
        unsigned char small[4];
        std::memset(small, 0x11u, sizeof(small));
        CHECK_RC(!buf_cursor::ReadBytes(small, sizeof(small), 8u));
        for (const unsigned char b : small)
            CHECK_RC(b == 0u);
        const buf_cursor::BufCursor *const cCap = buf_cursor::Current();
        CHECK_RC(cCap->failed);
        CHECK_RC(cCap->current == cCap->begin);
        buf_cursor::Deactivate();
    }

    // Inactive cursor: every bulk read fails closed and reports failure
    // without touching the destination.
    unsigned char inactive[8];
    std::memset(inactive, 0x22u, sizeof(inactive));
    CHECK_RC(!buf_cursor::ReadBytes(inactive, sizeof(inactive), sizeof(inactive)));
    CHECK_RC(inactive[0] == 0x22u);

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
    if (TestBulkRead(bytes) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: BulkRead failed on %s\n", label);
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

// A generated corpus case: the on-disk filename, its classification, and
// the bytes. `kind` is recorded in the manifest so the tracker can tell
// valid wire-shape seeds from malformed / minimized regression inputs.
struct SeedSpec
{
    const char *name;
    const char *kind;
    std::vector<unsigned char> bytes;
};

// The generated corpus: the three synthetic valid header shapes plus the
// cheap adversarial inputs a fuzzer finds first and two minimized
// truncations of the valid headers.
std::vector<SeedSpec> BuildCorpusSeeds()
{
    std::vector<SeedSpec> seeds;
    seeds.push_back({"xmodel_pieces_valid.bin", "valid", BuildXModelPiecesSeed()});
    seeds.push_back({"xanim_parts_valid.bin", "valid", BuildXAnimPartsSeed()});
    seeds.push_back({"fx_archive_body_state_valid.bin", "valid", BuildFxArchiveBodyStateSeed()});

    seeds.push_back({"empty.bin", "malformed", {}});
    seeds.push_back({"single_byte.bin", "malformed", {0xAAu}});
    seeds.push_back({"zeros_64.bin", "malformed", std::vector<unsigned char>(64u, 0u)});
    seeds.push_back({"ones_64.bin", "malformed", std::vector<unsigned char>(64u, 0xFFu)});
    {
        std::vector<unsigned char> alt(64u);
        for (size_t i = 0; i < alt.size(); ++i)
            alt[i] = static_cast<unsigned char>((i & 1u) ? 0xAAu : 0x55u);
        seeds.push_back({"alt_64.bin", "malformed", std::move(alt)});
    }
    seeds.push_back({"ones_256.bin", "malformed", std::vector<unsigned char>(256u, 0xFFu)});

    // Minimized regressions: truncate each valid header so the bounded
    // read path must trip its limit rather than read past end.
    {
        std::vector<unsigned char> xmodel = BuildXModelPiecesSeed();
        if (xmodel.size() > 1u)
            xmodel.resize(xmodel.size() / 2u);
        seeds.push_back({"truncated_xmodel_pieces.bin", "malformed", std::move(xmodel)});

        std::vector<unsigned char> xanim = BuildXAnimPartsSeed();
        if (xanim.size() > 1u)
            xanim.resize(xanim.size() / 2u);
        seeds.push_back({"truncated_xanim_parts.bin", "malformed", std::move(xanim)});
    }

    return seeds;
}

// The corpus path must be an existing, readable directory.
bool IsUsableCorpusDir(const std::string &corpusDir)
{
    std::error_code ec;
    const std::filesystem::path dir(corpusDir);
    if (std::filesystem::is_directory(dir, ec) && !ec)
        return true;
    const std::string detail = ec ? (" (" + ec.message() + ")") : std::string();
    std::fprintf(stderr, "fuzz_fastfile: corpus directory missing or unreadable: %s%s\n",
                 corpusDir.c_str(), detail.c_str());
    return false;
}

// Validate every entry against the manifest and load its payload BEFORE
// any of them is exercised, so a mismatched corpus fails without a
// partial sweep. Returns false (with the violation logged) on any
// mismatch.
bool LoadCorpusPayloads(const std::string &corpusDir, const std::vector<CorpusEntry> &entries,
                        std::vector<std::vector<unsigned char>> &payloads)
{
    payloads.reserve(entries.size());
    for (const CorpusEntry &entry : entries)
    {
        if (!IsSafeEntryName(entry.name))
        {
            std::fprintf(stderr, "fuzz_fastfile: unsafe corpus entry name: %s\n", entry.name.c_str());
            return false;
        }

        std::vector<unsigned char> bytes;
        if (!ReadFileChecked(corpusDir + "/" + entry.name, bytes))
        {
            std::fprintf(stderr, "fuzz_fastfile: corpus entry missing or unreadable: %s\n",
                         entry.name.c_str());
            return false;
        }
        if (bytes.size() != entry.size)
        {
            std::fprintf(stderr,
                         "fuzz_fastfile: corpus entry size mismatch for %s (manifest=%llu actual=%zu)\n",
                         entry.name.c_str(), static_cast<unsigned long long>(entry.size), bytes.size());
            return false;
        }
        if (Fnv1a64(bytes) != entry.hash)
        {
            std::fprintf(stderr, "fuzz_fastfile: corpus entry hash mismatch for %s\n",
                         entry.name.c_str());
            return false;
        }
        payloads.push_back(std::move(bytes));
    }
    return true;
}

// Strict corpus mode. Every failure (absent, non-directory, unreadable,
// missing / malformed manifest, missing / size-mismatched / hash-
// mismatched entry, empty manifest) is terminal: this never falls back
// to the inline seeds.
int RunCorpusDir(const std::string &corpusDir)
{
    if (corpusDir.empty())
    {
        std::fprintf(stderr, "fuzz_fastfile: corpus requires a directory\n");
        return 1;
    }

    if (!IsUsableCorpusDir(corpusDir))
        return 1;

    std::vector<CorpusEntry> entries;
    if (!LoadCorpusManifest(corpusDir, entries))
        return 1;
    if (entries.empty())
    {
        std::fprintf(stderr, "fuzz_fastfile: corpus manifest lists no entries: %s/%s\n",
                     corpusDir.c_str(), kCorpusManifestName);
        return 1;
    }

    std::vector<std::vector<unsigned char>> payloads;
    if (!LoadCorpusPayloads(corpusDir, entries, payloads))
        return 1;

    std::size_t malformed = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].kind == "malformed")
            ++malformed;
        if (ExerciseSeed(payloads[i], entries[i].name.c_str()) != 0)
            return 1;
    }

    std::fprintf(stdout, "fuzz_fastfile: corpus ok (%zu files, %zu malformed, runs=%d)\n",
                 entries.size(), malformed, g_runs);
    return 0;
}

int RunCorpus(const char *corpusDir)
{
    return RunCorpusDir(corpusDir == nullptr ? std::string() : std::string(corpusDir));
}

int GenerateSeeds(const char *outDir);

// Create dir (and parents) if needed, reporting the first filesystem error.
bool EnsureDirectory(const std::filesystem::path &dir, const char *label)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!ec && std::filesystem::is_directory(dir))
        return true;
    const std::string detail = ec ? ec.message() : std::string("not a directory");
    std::fprintf(stderr, "fuzz_fastfile: %s unavailable: %s: %s\n", label,
                 dir.string().c_str(), detail.c_str());
    return false;
}

bool WriteTextFile(const std::filesystem::path &file, const std::string &text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    return static_cast<bool>(out);
}

// Create a regular file holding a single byte at `path`; returns whether
// the file landed. Used both as a gate case (a corpus path that is not a
// directory) and as the deterministic copy blocker in the setup-failure
// probe.
bool CreateBlockerFile(const std::string &path)
{
    std::ofstream blocker(path, std::ios::binary);
    blocker << 'x';
    blocker.flush();
    return static_cast<bool>(blocker);
}

std::string UniqueNonce()
{
    std::random_device rd;
    return std::to_string((static_cast<unsigned long long>(rd()) << 32) ^
                          static_cast<unsigned long long>(rd()));
}

// Caller-supplied parent scratch root plus the exclusively owned scratch
// child and preservation sentinel this gate creates inside it. The
// destructor removes only the child and sentinel this object created —
// never the caller's parent or any of its pre-existing content.
struct CallerScratch
{
    std::filesystem::path parent;
    std::filesystem::path owned;
    std::filesystem::path sentinel;
    std::string sentinelText = "preserve caller content\n";

    CallerScratch() = default;
    CallerScratch(const CallerScratch &) = delete;
    CallerScratch &operator=(const CallerScratch &) = delete;

    ~CallerScratch()
    {
        std::error_code ignore;
        if (!owned.empty())
            std::filesystem::remove_all(owned, ignore);
        if (!sentinel.empty())
            std::filesystem::remove_all(sentinel, ignore);
    }

    // Write the preservation sentinel the gate re-checks after running.
    bool WriteCallerSentinel()
    {
        const std::string nonce = UniqueNonce();
        sentinel = parent / ("corpus-gate-sentinel-" + nonce);
        if (!EnsureDirectory(sentinel, "caller sentinel") ||
            !WriteTextFile(sentinel / "keep.txt", sentinelText))
        {
            std::fprintf(stderr, "fuzz_fastfile: could not write caller sentinel %s\n",
                         sentinel.string().c_str());
            return false;
        }
        return true;
    }

    // Allocate an exclusively owned child directory under the caller's
    // parent. Pre-existing content is never touched; allocation gives up
    // after 64 deterministic name collisions or a hard filesystem error.
    bool AllocateOwned()
    {
        const std::string nonce = UniqueNonce();
        for (unsigned int attempt = 0; attempt < 64; ++attempt)
        {
            const std::filesystem::path candidate =
                parent / ("corpus-gate-" + nonce + "-" + std::to_string(attempt));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec))
            {
                owned = candidate;
                return true;
            }
            if (ec)
                return false;
        }
        return false;
    }

    bool Prepare(const char *scratchRoot)
    {
        if (scratchRoot == nullptr || scratchRoot[0] == 0)
        {
            std::fprintf(stderr, "fuzz_fastfile: corpus-gate requires a scratch directory\n");
            return false;
        }
        parent = std::filesystem::path(scratchRoot);
        if (!EnsureDirectory(parent, "scratch parent"))
            return false;

        if (!WriteCallerSentinel())
            return false;
        if (!AllocateOwned())
        {
            std::fprintf(stderr, "fuzz_fastfile: could not allocate owned scratch under %s\n",
                         parent.string().c_str());
            return false;
        }
        return true;
    }

    bool CallerContentPreserved() const
    {
        std::vector<unsigned char> bytes;
        if (!ReadFileChecked((sentinel / "keep.txt").string(), bytes))
            return false;
        return std::string(bytes.begin(), bytes.end()) == sentinelText;
    }
};

// Copy the generated valid corpus tree to `dest`, logging (but not
// counting) a copy failure. Failure accounting belongs to the case-runner
// (`ExpectFailAfterSetup`), so a setup failure is always recorded as a
// gate failure and a negative case can never be silently skipped.
bool CopyCorpusTree(const std::string &valid, const std::string &dest)
{
    std::error_code copyEc;
    std::filesystem::copy(std::filesystem::path(valid), std::filesystem::path(dest),
                          std::filesystem::copy_options::recursive, copyEc);
    if (copyEc)
    {
        std::fprintf(stderr, "fuzz_fastfile: gate: could not copy corpus to %s: %s\n",
                     dest.c_str(), copyEc.message().c_str());
        return false;
    }
    return true;
}

// Copy the accepted corpus and rewrite its manifest through `transform`,
// returning whether the mutated corpus is in place. The payload files are
// left untouched so only the manifest field under test differs. Pure
// setup: failures are logged here and counted by the case-runner.
template <typename Transform>
bool MutateCorpusManifest(const std::string &valid, const std::string &dest, Transform transform)
{
    if (!CopyCorpusTree(valid, dest))
        return false;

    const std::string manifestPath = dest + "/" + kCorpusManifestName;
    std::vector<std::string> lines;
    {
        std::ifstream in(manifestPath);
        if (!in.is_open())
        {
            std::fprintf(stderr, "fuzz_fastfile: gate: could not read manifest %s\n",
                         manifestPath.c_str());
            return false;
        }
        std::string line;
        while (std::getline(in, line))
            lines.push_back(line);
    }

    std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        std::fprintf(stderr, "fuzz_fastfile: gate: could not rewrite manifest %s\n",
                     manifestPath.c_str());
        return false;
    }
    for (const std::string &line : lines)
        out << transform(line) << "\n";
    out.flush();
    if (!out)
    {
        std::fprintf(stderr, "fuzz_fastfile: gate: manifest rewrite failed for %s\n",
                     manifestPath.c_str());
        return false;
    }
    return true;
}

// Expect the corpus runner to reject `dir` as-is (no setup can fail).
void ExpectRejectNoSetup(int &failures, const std::string &label, const std::string &dir)
{
    if (RunCorpusDir(dir) == 0)
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: gate: expected rejection but corpus passed: %s\n",
                     label.c_str());
        ++failures;
        return;
    }
    std::fprintf(stdout, "fuzz_fastfile: gate: %s rejected\n", label.c_str());
}

// Run one negative corpus case: `setup` materializes the malformed corpus
// at `dir`, then the corpus runner must reject it. A setup failure is
// recorded as a gate failure in its own right — the previous pattern
// (`if (setup()) expectFail(...)`) silently skipped the negative case on
// a copy/mutation/read/write error and exited green, which the refinery
// demonstrated as a false-green coverage gap.
template <typename Setup>
void ExpectFailAfterSetup(int &failures, const std::string &label,
                          const std::string &dir, Setup setup)
{
    if (!setup())
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: gate: setup failed for negative case '%s'; "
                     "recorded as failure (negative cases must not be skipped)\n",
                     label.c_str());
        ++failures;
        return;
    }
    ExpectRejectNoSetup(failures, label, dir);
}

// Deterministic setup-failure regression: a failed negative-case setup
// must be recorded as a gate failure, never silently skipped. The forced
// failure is portable and deterministic — copying a corpus directory to a
// path whose parent is a regular file fails with a not-a-directory error
// on every platform — instead of permission bits (defeated by root) or
// an LD_PRELOAD interposer (not available in CI). Exits 0 only when the
// setup failure reached the gate's failure count.
int RunGateSetupFailureProbe(const char *scratchRoot)
{
    CallerScratch scratch;
    if (!scratch.Prepare(scratchRoot))
        return 1;

    const std::string root = scratch.owned.string();
    const std::string valid = root + "/valid";
    if (GenerateSeeds(valid.c_str()) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: probe: could not generate the valid corpus\n");
        return 1;
    }

    // A regular file where the negative case's corpus directory would be
    // created; any copy beneath it fails deterministically.
    const std::string dest = root + "/blocker/manifest_size_garbage";
    if (!CreateBlockerFile(root + "/blocker"))
    {
        std::fprintf(stderr, "fuzz_fastfile: probe: could not create blocker file\n");
        return 1;
    }

    // Premise check: the copy must actually fail. If it ever succeeds the
    // probe cannot assert propagation and must fail loudly instead of
    // passing on a broken premise.
    if (CopyCorpusTree(valid, dest))
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: probe: premise broken — copy under a regular file "
                     "unexpectedly succeeded at %s\n",
                     dest.c_str());
        return 1;
    }

    int failures = 0;
    ExpectFailAfterSetup(failures, "setup-failure propagation (copy under regular file)",
                         dest, [&]() -> bool { return CopyCorpusTree(valid, dest); });

    if (failures != 1)
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: probe: setup failure was not propagated to the gate "
                     "outcome (recorded failures: %d, expected 1)\n",
                     failures);
        return 1;
    }

    // Sentinel preservation holds for the probe scratch too.
    if (!scratch.CallerContentPreserved())
    {
        std::fprintf(stderr, "fuzz_fastfile: probe: caller sentinel was removed or altered\n");
        return 1;
    }

    std::fprintf(stdout, "fuzz_fastfile: corpus gate setup-failure probe ok\n");
    return 0;
}

// Expect the corpus runner to accept `dir` (the positive gate case).
void ExpectAccept(int &failures, const std::string &label, const std::string &dir)
{
    if (RunCorpusDir(dir) != 0)
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: gate: expected acceptance but corpus failed: %s\n",
                     label.c_str());
        ++failures;
        return;
    }
    std::fprintf(stdout, "fuzz_fastfile: gate: %s accepted\n", label.c_str());
}

// Negative-case setup: materialize a corpus directory whose manifest
// lists no entries.
bool SetupEmptyManifest(const std::string &dest)
{
    if (!EnsureDirectory(std::filesystem::path(dest), "empty-manifest corpus"))
        return false;
    std::ofstream m(dest + "/" + kCorpusManifestName);
    m << "# no entries\n";
    m.flush();
    return static_cast<bool>(m);
}

// Negative-case setup: copy the accepted corpus and remove a listed
// entry from disk. A failed removal would leave a valid corpus behind
// and exercise the wrong condition, so it is reported instead of
// ignored.
bool SetupMissingEntry(const std::string &valid, const std::string &dest)
{
    if (!CopyCorpusTree(valid, dest))
        return false;
    std::error_code rmEc;
    std::filesystem::remove(std::filesystem::path(dest + "/xanim_parts_valid.bin"), rmEc);
    if (rmEc)
    {
        std::fprintf(stderr, "fuzz_fastfile: gate: could not remove corpus entry: %s\n",
                     rmEc.message().c_str());
        return false;
    }
    return true;
}

// Negative-case setup: copy the accepted corpus and append a byte to a
// listed entry. The append stream is scoped so it is flushed to disk
// before the corpus re-reads it.
bool SetupSizeMismatch(const std::string &valid, const std::string &dest)
{
    if (!CopyCorpusTree(valid, dest))
        return false;
    std::ofstream append(dest + "/xmodel_pieces_valid.bin", std::ios::binary | std::ios::app);
    append << 'X';
    append.flush();
    return static_cast<bool>(append);
}

// Negative-case setup: copy the accepted corpus and flip a byte of a
// listed entry without changing its size.
bool SetupHashMismatch(const std::string &valid, const std::string &dest)
{
    if (!CopyCorpusTree(valid, dest))
        return false;
    const std::string target = dest + "/xmodel_pieces_valid.bin";
    std::vector<unsigned char> bytes;
    if (!ReadFileChecked(target, bytes) || bytes.empty())
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: gate: could not read corpus entry for hash mutation: %s\n",
                     target.c_str());
        return false;
    }
    bytes[0] = static_cast<unsigned char>(bytes[0] ^ 0xFFu);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return static_cast<bool>(out);
}

// The four grammar tokens of a manifest data line.
struct ManifestTokens
{
    std::string hash;
    std::string size;
    std::string kind;
    std::string name;
};

// Split a manifest data line into its grammar tokens. The generator only
// writes four-token data lines, so a short read leaves the missing
// fields empty and the rebuilt line is rejected downstream like any
// malformed line.
void TokenizeManifestLine(const std::string &line, ManifestTokens &tokens)
{
    std::istringstream ls(line);
    ls >> tokens.hash;
    ls >> tokens.size;
    ls >> tokens.kind;
    ls >> tokens.name;
}

// Rewrite one manifest line through `mutate`, leaving comments and blank
// lines untouched. Data lines are rebuilt from their four grammar tokens
// so the mutation targets a single field.
template <typename Mutate>
std::string RewriteManifestLine(const std::string &line, Mutate mutate)
{
    if (line.empty() || line[0] == '#')
        return line;
    ManifestTokens tokens;
    TokenizeManifestLine(line, tokens);
    mutate(tokens);
    return tokens.hash + " " + tokens.size + " " + tokens.kind + " " + tokens.name;
}

// Negative-case setup factory: copy the accepted corpus to `dest` and
// rewrite every manifest line through `mutate` (comments pass through).
// The payload files are left untouched so only the manifest field under
// test differs.
template <typename Mutate>
auto ManifestGrammarCase(const std::string &valid, const std::string &dest, Mutate mutate)
{
    return [valid, dest, mutate]() -> bool {
        return MutateCorpusManifest(valid, dest, [&mutate](const std::string &line) {
            return RewriteManifestLine(line, mutate);
        });
    };
}

// Structural rejections that need no generated corpus: an absent
// directory, a directory without a manifest, a regular file where a
// corpus directory is expected, and a manifest that lists no entries.
// Every directory/file creation is part of the case setup, so a failure
// there is recorded instead of silently skipping the case.
void RunGateStructuralCases(int &failures, const std::string &root)
{
    // Absent corpus directory. No setup to fail: the absence is the case.
    ExpectRejectNoSetup(failures, "absent corpus", root + "/absent");

    const std::string noManifest = root + "/no_manifest";
    ExpectFailAfterSetup(
        failures, "directory without manifest", noManifest,
        [&]() -> bool {
            return EnsureDirectory(std::filesystem::path(noManifest), "no-manifest corpus");
        });

    // Corpus path is a regular file, not a directory.
    const std::string notADir = root + "/not_a_dir";
    ExpectFailAfterSetup(failures, "corpus path is a regular file", notADir,
                         [&]() -> bool { return CreateBlockerFile(notADir); });

    // Manifest present but lists no entries.
    const std::string emptyManifest = root + "/empty_manifest";
    ExpectFailAfterSetup(failures, "manifest with no entries", emptyManifest,
                         [&]() -> bool { return SetupEmptyManifest(emptyManifest); });
}

// Entry-level mutations of a copied valid corpus: a removed entry, an
// entry whose size no longer matches the manifest, and an entry whose
// bytes changed without changing size.
void RunGateEntryCases(int &failures, const std::string &root, const std::string &valid)
{
    const std::string missingEntry = root + "/missing_entry";
    ExpectFailAfterSetup(failures, "removed manifest entry", missingEntry,
                         [&]() -> bool { return SetupMissingEntry(valid, missingEntry); });

    const std::string sizeMismatch = root + "/size_mismatch";
    ExpectFailAfterSetup(failures, "size-mismatched entry", sizeMismatch,
                         [&]() -> bool { return SetupSizeMismatch(valid, sizeMismatch); });

    const std::string hashMismatch = root + "/hash_mismatch";
    ExpectFailAfterSetup(failures, "hash-mismatched entry", hashMismatch,
                         [&]() -> bool { return SetupHashMismatch(valid, hashMismatch); });
}

// Strict-manifest regressions: a mutated manifest must be rejected
// instead of silently accepting clamped, signed, or trailing fields.
// Each case copies the accepted corpus and rewrites the manifest in
// place. These cover the previously lenient strtoull/kind/field-count
// paths; a copy or manifest rewrite failure is recorded, never skipped.
void RunGateManifestGrammarCases(int &failures, const std::string &root,
                                 const std::string &valid)
{
    const std::string sizeGarbage = root + "/manifest_size_garbage";
    ExpectFailAfterSetup(failures, "size token with garbage suffix", sizeGarbage,
                         ManifestGrammarCase(valid, sizeGarbage,
                                             [](ManifestTokens &t) { t.size += "x"; }));

    // A size that overflows uint64_t: strtoull clamps to ULLONG_MAX, so
    // without the ERANGE check the old parser silently accepted it.
    const std::string sizeOverflow = root + "/manifest_size_overflow";
    ExpectFailAfterSetup(failures, "overflowing size token", sizeOverflow,
                         ManifestGrammarCase(valid, sizeOverflow, [](ManifestTokens &t) {
                             t.size = "99999999999999999999999999";
                         }));

    // A signed size: strtoull accepts a leading '-' and wraps the value.
    const std::string sizeSigned = root + "/manifest_size_signed";
    ExpectFailAfterSetup(failures, "signed size token", sizeSigned,
                         ManifestGrammarCase(valid, sizeSigned,
                                             [](ManifestTokens &t) { t.size = "-" + t.size; }));

    // A hash whose token is not exactly 16 hex digits.
    const std::string hashGarbage = root + "/manifest_hash_garbage";
    ExpectFailAfterSetup(failures, "non-hex hash token", hashGarbage,
                         ManifestGrammarCase(valid, hashGarbage,
                                             [](ManifestTokens &t) { t.hash = "zz" + t.hash; }));

    // An unknown classification outside the valid/malformed enum.
    const std::string bogusKind = root + "/manifest_bogus_kind";
    ExpectFailAfterSetup(failures, "unknown corpus kind", bogusKind,
                         ManifestGrammarCase(valid, bogusKind,
                                             [](ManifestTokens &t) { t.kind = "bogus"; }));

    // A fifth token beyond the four grammar fields.
    const std::string extraToken = root + "/manifest_extra_token";
    ExpectFailAfterSetup(failures, "manifest line with trailing token", extraToken,
                         [&]() -> bool {
                             return MutateCorpusManifest(valid, extraToken,
                                                         [](const std::string &line) {
                                                             return line + " UNPARSED";
                                                         });
                         });
}

// Negative gate: prove the corpus contract rejects every malformed
// corpus condition and accepts only the generated manifest corpus.
//
// The supplied path is a caller-owned PARENT scratch root (CTest passes
// one under the build tree, never a hardcoded /tmp path; the CLI may
// pass any path). The gate NEVER removes that parent or any pre-existing
// content in it: it allocates a freshly created, exclusively owned child
// directory underneath and uses only that, then removes only the child it
// created. A sentinel written into the parent before the gate runs is
// re-checked afterwards, so a regression that deletes caller content
// fails the test.
int RunCorpusGate(const char *scratchRoot)
{
    CallerScratch scratch;
    if (!scratch.Prepare(scratchRoot))
        return 1;

    const std::string root = scratch.owned.string();
    int failures = 0;

    RunGateStructuralCases(failures, root);

    // Valid generated corpus is accepted.
    const std::string valid = root + "/valid";
    if (GenerateSeeds(valid.c_str()) != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: gate: could not generate the valid corpus\n");
        return 1;
    }
    ExpectAccept(failures, "generated corpus", valid);

    RunGateEntryCases(failures, root, valid);
    RunGateManifestGrammarCases(failures, root, valid);

    // Sentinel preservation: caller content under the supplied parent must
    // survive the gate untouched.
    if (!scratch.CallerContentPreserved())
    {
        std::fprintf(stderr,
                     "fuzz_fastfile: gate: caller sentinel %s was removed or altered\n",
                     scratch.sentinel.string().c_str());
        ++failures;
    }
    else
    {
        std::fprintf(stdout, "fuzz_fastfile: gate: caller content preserved\n");
    }

    if (failures != 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: corpus gate FAILED (%d unmet expectations)\n", failures);
        return 1;
    }

    std::fprintf(stdout, "fuzz_fastfile: corpus gate ok\n");
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
            // Persist the offending seed next to the corpus dir so it can
            // be minimized and added to a manifest-checked corpus to
            // reproduce.
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
// Write one seed payload and append its manifest record. Returns false
// (with the failure logged) when the payload cannot be written.
bool WriteSeedPayload(const std::string &outDir, const SeedSpec &seed,
                      std::ostringstream &manifest)
{
    const std::string path = outDir + "/" + seed.name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        std::fprintf(stderr, "fuzz_fastfile: could not write %s\n", path.c_str());
        return false;
    }
    if (!seed.bytes.empty())
    {
        out.write(reinterpret_cast<const char *>(seed.bytes.data()),
                  static_cast<std::streamsize>(seed.bytes.size()));
    }
    out.flush();
    if (!out)
    {
        std::fprintf(stderr, "fuzz_fastfile: write failed for %s\n", path.c_str());
        return false;
    }

    char hashHex[17];
    std::snprintf(hashHex, sizeof(hashHex), "%016llx",
                  static_cast<unsigned long long>(Fnv1a64(seed.bytes)));
    manifest << hashHex << " " << seed.bytes.size() << " " << seed.kind << " " << seed.name
             << "\n";
    return true;
}

// Write the accumulated manifest text next to the corpus payloads.
bool WriteCorpusManifestFile(const std::string &outDir, const std::string &manifestText)
{
    const std::string manifestPath = outDir + "/" + kCorpusManifestName;
    std::ofstream mout(manifestPath, std::ios::binary | std::ios::trunc);
    if (!mout.is_open())
    {
        std::fprintf(stderr, "fuzz_fastfile: could not write manifest %s\n", manifestPath.c_str());
        return false;
    }
    mout.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
    mout.flush();
    if (!mout)
    {
        std::fprintf(stderr, "fuzz_fastfile: manifest write failed for %s\n",
                     manifestPath.c_str());
        return false;
    }
    return true;
}

int GenerateSeeds(const char *outDir)
{
    if (outDir == nullptr || outDir[0] == 0)
    {
        std::fprintf(stderr, "fuzz_fastfile: genseeds requires an output directory\n");
        return 1;
    }

    std::error_code mkdirEc;
    std::filesystem::create_directories(std::filesystem::path(outDir), mkdirEc);
    if (mkdirEc)
    {
        std::fprintf(stderr, "fuzz_fastfile: could not create %s: %s\n",
                     outDir, mkdirEc.message().c_str());
        return 1;
    }

    const std::vector<SeedSpec> seeds = BuildCorpusSeeds();
    std::ostringstream manifest;
    manifest << "# fuzz_fastfile corpus manifest v1\n";
    manifest << "# fields: fnv1a64-hex size kind name\n";
    for (const SeedSpec &seed : seeds)
    {
        if (!WriteSeedPayload(outDir, seed, manifest))
            return 1;
    }

    if (!WriteCorpusManifestFile(outDir, manifest.str()))
        return 1;

    std::fprintf(stdout, "fuzz_fastfile: genseeds wrote %zu seeds + manifest to %s\n",
                 seeds.size(), outDir);
    return 0;
}

int RunGenSeeds(const char *outDir)
{
    return GenerateSeeds(outDir);
}

void PrintUsage(const char *argv0)
{
    std::fprintf(stdout,
        "fuzz_fastfile — bounded cursor-primitive fuzz harness\n"
        "Usage: %s [seeds] [corpus <dir>] [corpus-gate <dir>] "
        "[corpus-gate-failing-setup <dir>] [random <count> <dir>] [genseeds <dir>]\n"
        "  seeds                 run the built-in inline seed corpus (default)\n"
        "  corpus <dir>          run the manifest-checked corpus in <dir>;\n"
        "                        fails closed on absent/empty/mismatched input\n"
        "  corpus-gate <dir>     self-check the corpus contract (negative gate)\n"
        "  corpus-gate-failing-setup <dir>\n"
        "                        prove a failed negative-case setup is recorded\n"
        "                        as a gate failure, never silently skipped\n"
        "  random <n> <dir>      run <n> mutated iterations, persisting crashes\n"
        "  genseeds <dir>        write the bounded corpus + manifest to <dir>\n"
        "The generated corpus covers the synthetic xmodel pieces, xanim parts\n"
        "and fx archive body state header shapes plus empty / single-byte /\n"
        "all-zero / all-ones / alternating / truncated malformed inputs.\n",
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
    if (mode == "corpus-gate" && argc >= 3)
        return RunCorpusGate(argv[2]);
    if (mode == "corpus-gate-failing-setup" && argc >= 3)
        return RunGateSetupFailureProbe(argv[2]);
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
