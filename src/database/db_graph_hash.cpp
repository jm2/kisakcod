#include "database/db_graph_hash.h"

#include <cstring>

namespace db::graph_hash
{
namespace
{
// FIPS 180-4 SHA-256 constants.
constexpr std::uint32_t kInitialHash[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint8_t kMarkerRecordBegin = 0x01;
constexpr std::uint8_t kMarkerRecordEnd = 0x02;
constexpr std::uint8_t kMarkerFieldU64 = 0x10;
constexpr std::uint8_t kMarkerFieldI64 = 0x11;
constexpr std::uint8_t kMarkerFieldF32 = 0x12;
constexpr std::uint8_t kMarkerFieldBytes = 0x13;
constexpr std::uint8_t kMarkerFieldString = 0x14;

inline std::uint32_t RotateRight(const std::uint32_t value, const unsigned count) noexcept
{
    return (value >> count) | (value << (32u - count));
}

void CompressBlock(std::uint32_t state[8], const std::uint8_t input[64]) noexcept
{
    std::uint32_t w[64];
    for (std::size_t i = 0; i < 16; ++i)
    {
        // Big-endian word load implemented byte-wise: identical on any host
        // byte order.
        w[i] = (static_cast<std::uint32_t>(input[i * 4 + 0]) << 24)
            | (static_cast<std::uint32_t>(input[i * 4 + 1]) << 16)
            | (static_cast<std::uint32_t>(input[i * 4 + 2]) << 8)
            | static_cast<std::uint32_t>(input[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 = RotateRight(w[i - 15], 7)
            ^ RotateRight(w[i - 15], 18)
            ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = RotateRight(w[i - 2], 17)
            ^ RotateRight(w[i - 2], 19)
            ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i)
    {
        const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

namespace detail
{
void Sha256Core::Init() noexcept
{
    std::memcpy(state, kInitialHash, sizeof(state));
    bitLength = 0;
    blockUsed = 0;
}

void Sha256Core::Update(const std::uint8_t *bytes, std::size_t size) noexcept
{
    bitLength += static_cast<std::uint64_t>(size) * 8u;
    while (size > 0)
    {
        const std::size_t space = 64 - blockUsed;
        const std::size_t take = size < space ? size : space;
        std::memcpy(block + blockUsed, bytes, take);
        blockUsed += take;
        bytes += take;
        size -= take;
        if (blockUsed == 64)
        {
            CompressBlock(state, block);
            blockUsed = 0;
        }
    }
}

void Sha256Core::Finish(Digest &digest) noexcept
{
    // Snapshot the message length BEFORE padding: Update accounts every
    // absorbed byte, so the padding must not inflate the appended length.
    const std::uint64_t messageBits = bitLength;

    const std::uint8_t padOne = 0x80;
    Update(&padOne, 1);
    const std::uint8_t zero = 0;
    while (blockUsed != 56)
        Update(&zero, 1);

    // 64-bit big-endian message length in bits.
    const std::uint8_t lengthBits[8] = {
        static_cast<std::uint8_t>(messageBits >> 56),
        static_cast<std::uint8_t>(messageBits >> 48),
        static_cast<std::uint8_t>(messageBits >> 40),
        static_cast<std::uint8_t>(messageBits >> 32),
        static_cast<std::uint8_t>(messageBits >> 24),
        static_cast<std::uint8_t>(messageBits >> 16),
        static_cast<std::uint8_t>(messageBits >> 8),
        static_cast<std::uint8_t>(messageBits)};
    Update(lengthBits, 8);

    for (std::size_t i = 0; i < 8; ++i)
    {
        digest[i * 4 + 0] = static_cast<std::uint8_t>(state[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
    }
    Init();
}
} // namespace detail

GraphHashBuilder::GraphHashBuilder() noexcept
    : m_core{}
    , m_recordLengths{}
    , m_recordDepth{}
    , m_valid{}
{
    Reset();
}

void GraphHashBuilder::Reset() noexcept
{
    m_core.Init();
    m_recordDepth = 0;
    m_valid = true;

    // Domain separation: length-prefixed domain string, then a framing
    // version marker so future stream revisions cannot alias v1 bytes.
    const std::uint64_t domainLength = sizeof(kHashDomain) - 1;
    AbsorbU64(domainLength);
    m_core.Update(reinterpret_cast<const std::uint8_t *>(kHashDomain), domainLength);
    const std::uint8_t framingVersion = 1;
    m_core.Update(&framingVersion, 1);
}

void GraphHashBuilder::AbsorbTagged(const std::uint8_t marker, const std::uint32_t tag) noexcept
{
    const std::uint8_t bytes[5] = {
        marker,
        static_cast<std::uint8_t>(tag),
        static_cast<std::uint8_t>(tag >> 8),
        static_cast<std::uint8_t>(tag >> 16),
        static_cast<std::uint8_t>(tag >> 24)};
    Absorb(bytes, 5);
}

void GraphHashBuilder::AbsorbU64(const std::uint64_t value) noexcept
{
    const std::uint8_t bytes[8] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24),
        static_cast<std::uint8_t>(value >> 32),
        static_cast<std::uint8_t>(value >> 40),
        static_cast<std::uint8_t>(value >> 48),
        static_cast<std::uint8_t>(value >> 56)};
    Absorb(bytes, 8);
}

// Every absorbed byte is charged to the innermost open record so EndRecord
// can frame the payload with an explicit length. Bytes absorbed while no
// record is open (the domain preamble) belong to no record.
void GraphHashBuilder::Absorb(const std::uint8_t *bytes, const std::size_t size) noexcept
{
    m_core.Update(bytes, size);
    if (m_recordDepth > 0)
        m_recordLengths[m_recordDepth - 1] += static_cast<std::uint64_t>(size);
}

void GraphHashBuilder::BeginRecord(const std::uint32_t typeTag) noexcept
{
    if (m_recordDepth >= kMaxRecordDepth)
    {
        m_valid = false;
        return;
    }
    m_recordLengths[m_recordDepth] = 0;
    ++m_recordDepth;
    AbsorbTagged(kMarkerRecordBegin, typeTag);
}

void GraphHashBuilder::EndRecord() noexcept
{
    if (m_recordDepth == 0)
    {
        m_valid = false;
        return;
    }
    const std::uint64_t payloadLength = m_recordLengths[m_recordDepth - 1];
    --m_recordDepth;
    // The close framing and length are charged to the parent record (or to
    // no record when this was the outermost one), so emit them after the pop.
    AbsorbTagged(kMarkerRecordEnd, 0);
    AbsorbU64(payloadLength);
}

void GraphHashBuilder::FieldU64(const std::uint32_t tag, const std::uint64_t value) noexcept
{
    AbsorbTagged(kMarkerFieldU64, tag);
    AbsorbU64(value);
}

void GraphHashBuilder::FieldI64(const std::uint32_t tag, const std::int64_t value) noexcept
{
    // Zigzag mapping is canonical across sign conventions and widths.
    const std::uint64_t zigzag =
        (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
    AbsorbTagged(kMarkerFieldI64, tag);
    AbsorbU64(zigzag);
}

void GraphHashBuilder::FieldF32(const std::uint32_t tag, const float value) noexcept
{
    std::uint32_t bits = 0x7fc00000u; // canonical quiet NaN
    if (value == value)
        std::memcpy(&bits, &value, sizeof(bits));
    // NaN payloads collapse onto the single quiet NaN so x87/SSE
    // NaN-generation differences cannot split otherwise-identical graphs.
    // Signed zero is preserved: +/-0 are distinct for parity purposes.
    AbsorbTagged(kMarkerFieldF32, tag);
    AbsorbU64(bits);
}

void GraphHashBuilder::FieldBytes(
    const std::uint32_t tag,
    const void *data,
    const std::size_t size) noexcept
{
    AbsorbTagged(kMarkerFieldBytes, tag);
    AbsorbU64(static_cast<std::uint64_t>(size));
    if (size > 0 && data)
        Absorb(static_cast<const std::uint8_t *>(data), size);
}

void GraphHashBuilder::FieldString(const std::uint32_t tag, const char *text) noexcept
{
    const std::size_t length = text ? std::strlen(text) : 0;
    AbsorbTagged(kMarkerFieldString, tag);
    AbsorbU64(static_cast<std::uint64_t>(length));
    if (length > 0)
        Absorb(reinterpret_cast<const std::uint8_t *>(text), length);
}

Digest GraphHashBuilder::Finish() noexcept
{
    if (m_recordDepth != 0)
        m_valid = false;

    Digest digest{};
    m_core.Finish(digest);
    Reset();
    return digest;
}

void FormatDigestHex(const Digest &digest, char *out) noexcept
{
    static constexpr char kHexDigits[] = "0123456789abcdef";
    if (!out)
        return;
    for (std::size_t i = 0; i < kDigestBytes; ++i)
    {
        out[i * 2 + 0] = kHexDigits[digest[i] >> 4];
        out[i * 2 + 1] = kHexDigits[digest[i] & 0x0fu];
    }
    out[kDigestBytes * 2] = '\0';
}

Digest HashBytes(const void *data, const std::size_t size) noexcept
{
    // One-shot raw SHA-256 sharing the block core but NOT the capture-stream
    // domain separation.
    detail::Sha256Core core;
    core.Init();
    core.Update(static_cast<const std::uint8_t *>(data), size);
    Digest digest{};
    core.Finish(digest);
    return digest;
}

} // namespace db::graph_hash
