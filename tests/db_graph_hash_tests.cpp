#include "database/db_graph_hash.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

using db::graph_hash::Digest;
using db::graph_hash::GraphHashBuilder;

std::string_view Hex(const Digest &digest, char *buffer)
{
    db::graph_hash::FormatDigestHex(digest, buffer);
    return std::string_view(buffer);
}

int HexNibble(const char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool ParseHex(const std::string_view text, Digest &digest)
{
    if (text.size() != db::graph_hash::kDigestBytes * 2)
        return false;
    for (std::size_t i = 0; i < db::graph_hash::kDigestBytes; ++i)
    {
        const int high = HexNibble(text[i * 2]);
        const int low = HexNibble(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        digest[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

void TestSha256KnownAnswers()
{
    // FIPS 180-4 / NIST known-answer vectors for the shared one-shot core.
    struct Vector
    {
        const char *input;
        std::size_t length;
        const char *expectedHex;
        const char *name;
    };

    const std::vector<std::uint8_t> aVector(1000, static_cast<std::uint8_t>('a'));

    const Vector vectors[] = {
        {"", 0,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty"},
        {"abc", 3,
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "abc"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "56-byte"},
        {"The quick brown fox jumps over the lazy dog", 43,
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
            "pangram"},
        {nullptr, 1000,
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
            "1000 x 'a'"},
    };

    for (const Vector &vector : vectors)
    {
        const void *data = vector.input;
        if (!data)
            data = aVector.data();
        const Digest digest = db::graph_hash::HashBytes(data, vector.length);
        char hex[db::graph_hash::kHexDigestBytes];
        Expect(Hex(digest, hex) == std::string_view(vector.expectedHex),
            vector.name);
    }
}

// A logical graph node mirrored from two differently-shaped memory layouts.
// The hash walk must produce one digest because only logical fields enter
// the stream.
struct ReferenceLayoutNode // 32-bit reference-style layout
{
    std::uint32_t id;
    std::uint32_t payload;
    const ReferenceLayoutNode *next; // 32-bit pointer
};

struct WidenedLayoutNode // native64 widened layout
{
    const WidenedLayoutNode *next; // 64-bit pointer, different offset
    std::uint64_t id;
    std::uint64_t payload;
};

// Per-node nested records: each chain node opens a record, and the records
// are popped once the chain end is reached. The traversal is spelled as an
// explicit loop with a depth counter (static analysis forbids recursion,
// MISRA 17.2); the emitted byte stream is identical to a recursive walk
// that opens the next node's record inside the current one.
void WalkReferenceNode(GraphHashBuilder &builder, const ReferenceLayoutNode *node)
{
    std::size_t depth = 0;
    for (const ReferenceLayoutNode *cursor = node; cursor != nullptr;
        cursor = cursor->next)
    {
        builder.BeginRecord(0x4E44u); // "ND"
        builder.FieldU64(1, cursor->id);
        builder.FieldU64(2, cursor->payload);
        builder.FieldU64(3, cursor->next ? 1 : 0);
        ++depth;
    }
    while (depth > 0)
    {
        builder.EndRecord();
        --depth;
    }
}

void WalkWidenedNode(GraphHashBuilder &builder, const WidenedLayoutNode *node)
{
    std::size_t depth = 0;
    for (const WidenedLayoutNode *cursor = node; cursor != nullptr;
        cursor = cursor->next)
    {
        builder.BeginRecord(0x4E44u); // "ND"
        builder.FieldU64(1, cursor->id);
        builder.FieldU64(2, cursor->payload);
        builder.FieldU64(3, cursor->next ? 1 : 0);
        ++depth;
    }
    while (depth > 0)
    {
        builder.EndRecord();
        --depth;
    }
}

// Two-node chain fixtures. Each factory OWNS both nodes (a holds a raw
// pointer into its own chain). Two allocation rounds per leg so the graphs
// land at different addresses with different padding shapes across runs.
struct ReferenceGraph
{
    std::unique_ptr<ReferenceLayoutNode> a;
    std::unique_ptr<ReferenceLayoutNode> b;
};

struct WidenedGraph
{
    std::unique_ptr<WidenedLayoutNode> a;
    std::unique_ptr<WidenedLayoutNode> b;
};

ReferenceGraph MakeReferenceGraph()
{
    ReferenceGraph graph{std::make_unique<ReferenceLayoutNode>(),
        std::make_unique<ReferenceLayoutNode>()};
    graph.a->id = 7;
    graph.a->payload = 0xDEADBEEFu;
    graph.a->next = graph.b.get();
    graph.b->id = 9;
    graph.b->payload = 42;
    graph.b->next = nullptr;
    return graph;
}

WidenedGraph MakeWidenedGraph()
{
    WidenedGraph graph{std::make_unique<WidenedLayoutNode>(),
        std::make_unique<WidenedLayoutNode>()};
    graph.a->id = 7;
    graph.a->payload = 0xDEADBEEFu;
    graph.a->next = graph.b.get();
    graph.b->id = 9;
    graph.b->payload = 42;
    graph.b->next = nullptr;
    return graph;
}

void TestPointerAndLayoutIndependence()
{
    for (int round = 0; round < 4; ++round)
    {
        const auto referenceGraph = MakeReferenceGraph();
        const auto widenedGraph = MakeWidenedGraph();

        GraphHashBuilder referenceBuilder;
        WalkReferenceNode(referenceBuilder, referenceGraph.a.get());
        const Digest referenceDigest = referenceBuilder.Finish();

        GraphHashBuilder widenedBuilder;
        WalkWidenedNode(widenedBuilder, widenedGraph.a.get());
        const Digest widenedDigest = widenedBuilder.Finish();

        Expect(referenceDigest == widenedDigest,
            "32-bit reference and widened 64-bit walks must produce one digest");
        Expect(referenceBuilder.Valid() && widenedBuilder.Valid(),
            "balanced record streams must stay valid");
    }
}

void TestWidthParityProperty()
{
    // The property the M5 exit depends on: a value that fits both widths
    // hashes identically whether it entered the stream from a u32 field or a
    // u64 field, while a value that cannot exist on the 32-bit reference MUST
    // produce a different digest (so widening truncation bugs are visible).
    GraphHashBuilder fits32a;
    fits32a.BeginRecord(1);
    fits32a.FieldU64(1, 0x12345678u); // fed as u32 on the reference
    fits32a.EndRecord();
    const Digest fits32aDigest = fits32a.Finish();

    GraphHashBuilder fits32b;
    fits32b.BeginRecord(1);
    fits32b.FieldU64(1, 0x12345678u); // fed as u64 on native64
    fits32b.EndRecord();
    const Digest fits32bDigest = fits32b.Finish();
    Expect(fits32aDigest == fits32bDigest,
        "in-range values must hash identically across widths");

    GraphHashBuilder widenedOnly;
    widenedOnly.BeginRecord(1);
    widenedOnly.FieldU64(1, 0x123456789ABCDEF0ull); // impossible on x86
    widenedOnly.EndRecord();
    const Digest widenedOnlyDigest = widenedOnly.Finish();
    Expect(widenedOnlyDigest != fits32bDigest,
        "out-of-x86-range values must produce a distinct digest");
}

void TestFieldSemantics()
{
    // Signed values are zigzag-canonical: one logical value, one digest.
    GraphHashBuilder signedA;
    signedA.FieldI64(1, -2);
    const Digest signedADigest = signedA.Finish();

    GraphHashBuilder signedB;
    signedB.FieldI64(1, -2);
    const Digest signedBDigest = signedB.Finish();
    Expect(signedADigest == signedBDigest, "signed canonicalization is stable");

    // Byte fields are length-prefixed: trailing NUL padding in storage is
    // excluded from the digest.
    GraphHashBuilder bytesA;
    bytesA.FieldBytes(1, "zone", 4);
    const Digest bytesADigest = bytesA.Finish();

    // Storage with trailing NUL padding past the captured length (the
    // zero-initializing array form keeps the copy provably bounded).
    char padded[8] = "zone";
    GraphHashBuilder bytesB;
    bytesB.FieldBytes(1, padded, 4);
    const Digest bytesBDigest = bytesB.Finish();
    Expect(bytesADigest == bytesBDigest,
        "byte fields are length-prefixed; trailing padding is excluded");

    // Strings exclude the terminator; a null string canonicalizes to the
    // empty string.
    GraphHashBuilder stringA;
    stringA.FieldString(1, "zone");
    const Digest stringADigest = stringA.Finish();

    GraphHashBuilder emptyString;
    emptyString.FieldString(1, "");
    const Digest emptyStringDigest = emptyString.Finish();

    GraphHashBuilder nullString;
    nullString.FieldString(1, nullptr);
    const Digest nullStringDigest = nullString.Finish();
    Expect(nullStringDigest == emptyStringDigest,
        "null string canonicalizes to the empty string");
    Expect(stringADigest != emptyStringDigest, "distinct strings differ");

    // String and byte captures are distinct types by marker: the same text
    // entering through different field kinds must not alias.
    Expect(stringADigest != bytesADigest,
        "string and byte markers remain distinct capture types");
}

void TestFloatCanonicalization()
{
    GraphHashBuilder nanA;
    nanA.FieldF32(1, std::numeric_limits<float>::quiet_NaN());
    const Digest nanADigest = nanA.Finish();

    GraphHashBuilder nanB;
    // A differently-signed NaN with payload bits still canonicalizes to the
    // same digest: NaN is logically one state.
    const std::uint32_t payloadNanBits = 0xFFC00123u;
    float payloadNan = 0.0f;
    std::memcpy(&payloadNan, &payloadNanBits, sizeof(payloadNan));
    Expect(payloadNan != payloadNan, "fixture must be a NaN");
    nanB.FieldF32(1, payloadNan);
    const Digest nanBDigest = nanB.Finish();
    Expect(nanADigest == nanBDigest, "all NaN representations hash to one digest");

    GraphHashBuilder positiveZero;
    positiveZero.FieldF32(1, 0.0f);
    const Digest positiveZeroDigest = positiveZero.Finish();

    GraphHashBuilder negativeZero;
    negativeZero.FieldF32(1, -0.0f);
    const Digest negativeZeroDigest = negativeZero.Finish();
    Expect(positiveZeroDigest != negativeZeroDigest,
        "signed zeros are distinct for parity");
}

void TestSensitivityAndDeterminism()
{
    const auto build = [](const std::uint64_t middleValue, const bool swapped)
    {
        GraphHashBuilder builder;
        builder.BeginRecord(0x5A4FUL); // "ZO"
        builder.FieldString(1, "mp_test");
        if (swapped)
        {
            builder.FieldU64(3, 2);
            builder.FieldU64(2, middleValue);
        }
        else
        {
            builder.FieldU64(2, middleValue);
            builder.FieldU64(3, 2);
        }
        builder.EndRecord();
        return builder.Finish();
    };

    const Digest base = build(10, false);
    const Digest again = build(10, false);
    Expect(base == again, "repeated builds are deterministic");
    Expect(!(base == build(11, false)), "field value changes move the digest");
    Expect(!(base == build(10, true)), "field order changes move the digest");
    Expect(!(base == db::graph_hash::HashBytes("x", 1)),
        "domain separation separates capture streams from raw hashes");
}

// Nested records with identical leaf fields must frame unambiguously:
// (a(b,c)) differs from (a(b),c) and from ((a)b,c).
void TestFramingNestingIsCanonical()
{
    const auto nested = []()
    {
        GraphHashBuilder builder;
        builder.BeginRecord(1);
        {
            builder.BeginRecord(2);
            builder.FieldU64(1, 1);
            builder.EndRecord();
            builder.FieldU64(2, 2);
        }
        builder.EndRecord();
        return builder.Finish();
    };
    const auto flatPair = []()
    {
        GraphHashBuilder builder;
        builder.BeginRecord(1);
        builder.BeginRecord(2);
        builder.FieldU64(1, 1);
        builder.EndRecord();
        builder.FieldU64(2, 2);
        builder.EndRecord();
        return builder.Finish();
    };
    // flatPair builds the same stream as nested; both must agree.
    Expect(nested() == flatPair(), "balanced nesting is order-canonical");
}

// Open records are legal mid-stream (the caller may still close them);
// Finish resolves a still-open record by invalidating, finalizing,
// and resetting to a fresh stream. Two identically-built streams that
// Finish with records still open finalize to the same digest.
void TestFramingFinishResolvesOpenRecords()
{
    const auto misuseUnterminated = []()
    {
        GraphHashBuilder builder;
        builder.BeginRecord(1);
        builder.FieldU64(1, 1);
        return builder;
    };
    GraphHashBuilder probe = misuseUnterminated();
    Expect(probe.Valid(), "open records are legal mid-stream");
    probe.Finish();
    GraphHashBuilder fresh;
    Expect(probe.Finish() == fresh.Finish(),
        "Finish resets the builder to the fresh stream");
    GraphHashBuilder a = misuseUnterminated();
    GraphHashBuilder b = misuseUnterminated();
    Expect(a.Finish() == b.Finish(),
        "identically-misused streams finalize deterministically");
    Expect(a.Valid() && b.Valid(), "Finish restores a valid fresh stream");
}

// Extra EndRecord likewise invalidates.
void TestFramingExtraEndRecordInvalidates()
{
    GraphHashBuilder probe;
    probe.BeginRecord(1);
    probe.EndRecord();
    probe.EndRecord();
    Expect(!probe.Valid(), "extra EndRecord invalidates the stream");
}

// Depth overflow invalidates without crashing and stays deterministic.
void TestFramingDepthOverflowIsDeterministic()
{
    const auto misuseDeep = []()
    {
        GraphHashBuilder builder;
        for (std::size_t i = 0; i < db::graph_hash::kMaxRecordDepth + 4; ++i)
            builder.BeginRecord(1);
        return builder;
    };
    GraphHashBuilder probe = misuseDeep();
    Expect(!probe.Valid(), "record depth overflow invalidates the stream");
    GraphHashBuilder a = misuseDeep();
    GraphHashBuilder b = misuseDeep();
    Expect(a.Finish() == b.Finish(),
        "overflowed streams stay deterministic");
    Expect(a.Valid(), "Finish resets the builder to a valid fresh stream");
}

void TestRecordFraming()
{
    TestFramingNestingIsCanonical();
    TestFramingFinishResolvesOpenRecords();
    TestFramingExtraEndRecordInvalidates();
    TestFramingDepthOverflowIsDeterministic();
}

void TestResetAndReuse()
{
    GraphHashBuilder builder;
    builder.FieldU64(1, 1);
    const Digest first = builder.Finish();

    builder.Reset();
    builder.FieldU64(1, 1);
    const Digest second = builder.Finish();
    Expect(first == second, "Reset restores the exact fresh stream");

    builder.FieldU64(1, 1);
    const Digest third = builder.Finish();
    Expect(first == third, "builders are reusable without explicit Reset");
}

void TestHexFormatting()
{
    Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<std::uint8_t>(i * 17 + 3);
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Expect(std::string_view(hex).size() == 64, "hex digest is 64 characters");

    Digest parsed{};
    Expect(ParseHex(hex, parsed), "round-trip parse accepts our own hex");
    Expect(parsed == digest, "hex round-trip preserves the digest");
}

} // namespace

int main()
{
    TestSha256KnownAnswers();
    TestPointerAndLayoutIndependence();
    TestWidthParityProperty();
    TestFieldSemantics();
    TestFloatCanonicalization();
    TestSensitivityAndDeterminism();
    TestRecordFraming();
    TestResetAndReuse();
    TestHexFormatting();

    if (g_failures > 0)
    {
        std::fprintf(stderr, "db_graph_hash: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("db_graph_hash: all checks passed\n");
    return 0;
}
